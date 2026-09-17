// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/blob — strict parse/serialize of the
// `cosmetic-blob-v1` format (P12-T1).
//
// The contract is docs/contracts/cosmetic-blob-v1.md. This module is the C++
// implementation of its envelope; the refusal vocabulary is shared with the
// selector parser, and the Python reference consumer is fakes/cosmetic.py. The
// two must agree byte-for-byte — tools/cosmetic_vectors_check.py is what
// enforces that, and a divergence is a red vector rather than a quiet drift.
//
// THREE STRICTNESS LAWS, all of them the P11 `bundle-load` law applied here:
//
//   1. UNKNOWN FIELD DENY. An unrecognized key is a refusal, not an ignored
//      key. Ignoring it would let a producer ship a field this consumer has
//      never seen and have it silently do nothing, which is how a contract
//      acquires undeclared behaviour.
//   2. DIGEST VERIFIED BEFORE USE. `sha256` covers the canonical bytes of
//      everything except itself, computed with the SINGLE shared SHA-256 in
//      //xr/common (ADR-0043). A blob whose digest does not match is refused;
//      there is no "verify and continue anyway" path.
//   3. SCHEMA ID AND VERSION ARE CONST. A mismatch is refused, never migrated.
//      Migration is an amendment-RFC act, not something a parser does quietly.
#pragma once

#include <string>
#include <vector>

#include "common/core/json.h"
#include "renderer/cosmetic/core/keyset.h"

namespace xr::cosmetic {

inline constexpr const char* kCosmeticSchemaId = "xr-cosmetic-blob-v1";
inline constexpr int64_t kCosmeticSchemaVersion = 1;

struct BlobScope {
  std::string site;
  std::string identity_class;  // anonymous | authenticated | enterprise
};

struct BlobRefusal {
  int64_t rule_index = -1;
  std::string reason;
};

struct BlobRule {
  std::string id;
  std::string selector;
  std::string action;
  std::vector<std::pair<std::string, std::string>> style;
  std::vector<std::string> exception_sites;
  bool enabled = true;
};

struct CosmeticBlob {
  std::string blob_id;
  int64_t generated_epoch = 0;
  BlobScope scope;
  std::vector<BlobRule> rules;
  std::vector<BlobRefusal> refusals;
  std::string sha256;
};

enum class BlobError {
  kOk = 0,
  kMalformed,            // not JSON, or a field of the wrong type
  kSchemaMismatch,       // wrong schema id or version
  kUnknownField,         // a key this consumer has never heard of
  kScopeMismatch,        // the blob is not for this frame
  kSha256Mismatch,       // the digest does not cover these bytes
  kTooManyRules,
  kUnknownRefusalReason,  // the producer named a reason we do not have
  kDuplicateRuleId,
  kRuleRejected,          // one rule failed; `reason`/`at` say which and why
  kEmptyBlobId,
};

const char* BlobErrorName(BlobError e);

struct BlobResult {
  CosmeticBlob blob;
  // The compiled key set, filled on success. A caller never compiles a blob
  // itself: compilation is the key set's job and happens exactly once.
  KeySetResult key_set;
  BlobError error = BlobError::kOk;
  std::string reason;      // from the closed vocabulary, on refusal
  int64_t failed_index = -1;
  bool valid = false;
};

// Parses and validates a blob from canonical JSON text, then compiles its key
// set. `frame_scope`, when non-null, is the FRAME's own scope — supplied by the
// caller, never derived from the blob and never from an embedder.
//
// Total: no throw, no I/O, no clock. A refusal leaves `valid == false` and
// never returns a partially applied blob.
BlobError ParseBlob(const std::string& json, const BlobScope* frame_scope,
                    BlobResult* out);

// The canonical bytes the digest covers: the blob minus its own `sha256` field,
// serialized with the shared canonicalizer (sorted keys, no whitespace).
// Exposed so the producer and the test vectors can compute the same digest.
std::string BlobDigestInput(const CosmeticBlob& blob);

// Computes the digest over BlobDigestInput(). Uses the single shared SHA-256.
std::string BlobDigest(const CosmeticBlob& blob);

// Serializes a whole blob INCLUDING its digest field, canonically.
std::string BlobToCanonicalJson(const CosmeticBlob& blob);

}  // namespace xr::cosmetic
