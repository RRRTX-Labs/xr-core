// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: strict parse/validate of the FROZEN update-manifest contract
// (docs/contracts/update-manifest-31.schema.json — a strict subset of the
// Omaha 3.1 update-check response, never redefined here) wrapped in the
// XR update envelope v1 (a LIVING contract, docs/contracts/
// xr-update-envelope-v1.md) that carries the signature + epoch our threat
// model requires on top of 3.1.
//
// Deny-on-unknown is the law: any field outside the profile at any level is
// a parse failure, not a warning. The frozen schema's `response` object is
// validated exactly; the envelope adds no fields beyond those specified.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "update/core/json.h"

namespace xr::update {

// Canonical dotted-quad version: four dot-separated integers in
// [0, 2^32), no leading zeros (a strictness Chromium's Version also
// keeps), compare left-to-right numerically.
struct Version {
  uint32_t part[4] = {0, 0, 0, 0};

  bool operator<(const Version& o) const;
  bool operator==(const Version& o) const;
  bool operator>(const Version& o) const;
  // Strict canonical parse; rejects "1.0", "01.2.3.4", "1.0.0.0.0", junk.
  static bool Parse(const std::string& text, Version* out);
  std::string ToString() const;
};

// The XR update envelope v1 (living contract) around the frozen 3.1
// response. Field sets are closed; see Parse below for the deny-on-unknown
// behavior.
struct UpdateEnvelope {
  // --- envelope level (xr-update-envelope-v1) ---
  std::string schema;          // const "xr-update-envelope"
  int schema_version = 0;      // const 1
  std::string canonical_response;  // canonical bytes of `response` (the signed message)
  std::string manifest_id;     // client-side recomputed: sha256(canonical_response), 64-hex
  // epoch block
  std::string epoch_id;
  std::string epoch_key_id;
  long long epoch_seq = -1;
  // signature block
  std::string sig_alg;         // const "minisign-ed25519" (format work, not a primitive)
  std::string sig_key_id;
  std::string sig_value;       // base64 from the publisher; verified via the injected primitive

  // --- frozen 3.1 subset (validated, consumed) ---
  std::string protocol;        // const "3.1"
  std::string app_id;          // our appid
  std::string app_status;      // "ok" required for an offer
  std::string updatecheck_status;  // "ok" | "noupdate" | "error-..."
  Version version;             // required when updatecheck status == ok
  std::string package_name;
  long long package_size = -1;
  std::string package_hash_sha256;  // 64-hex
  std::string package_fp;
  std::string codebase;        // https:// only (law, tested)
};

enum class ParseResult {
  kOk,
  kMalformed,        // not JSON / wrong shape / missing required field
  kUnknownField,     // deny-on-unknown at any level
  kWrongProtocol,    // response.protocol != "3.1"
  kBadVersion,       // non-canonical version string
  kOversize,         // document above the 64 KiB cap
  kInsecureUrl,      // codebase not https
  kBadDigest,        // hash_sha256 not 64-hex / manifest_id mismatch
};

const char* ToString(ParseResult r);

// Strict parse + validate. Any unknown field (envelope, response, app,
// updatecheck, manifest, packages, package, urls, url, epoch, signature)
// yields kUnknownField. `raw` is the wire bytes; canonicalization uses the
// core JSON model (sorted keys, \uXXXX) so the signed message is stable.
ParseResult ParseEnvelope(const std::string& raw, UpdateEnvelope* out);

}  // namespace xr::update
