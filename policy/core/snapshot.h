// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — versioned-blob codec for snapshot distribution to the
// network service + renderer (Plan P6-T3): full and incremental-diff
// encodings of the (identity, site, trust) → EffectivePolicy state, budget-
// enforced at ≤ 32 KB per blob (a larger legal state is a STOP-condition
// RFC, never a truncation), SHA-256 integrity field (corruption DETECTION —
// authenticity comes from the transport/signing layers), and a strict
// decoder that denies on unknown/malformed input: decode failure yields a
// typed error and the consumer applies deny defaults. The wire format
// carries EffectivePolicy v1 values only — the frozen contract's runtime
// embodiment; changing the EffectivePolicy surface requires an RFC, the
// envelope around it is P6's to design.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "policy/core/effective_policy.h"

namespace xr::policy {

inline constexpr size_t kSnapshotBudgetBytes = 32 * 1024;  // §1.11 budget

struct SnapshotEntry {
  std::string identity;
  std::string site;
  std::string trust;
  EffectivePolicy policy;

  bool SameKey(const SnapshotEntry& o) const {
    return identity == o.identity && site == o.site && trust == o.trust;
  }
  // Canonical key ordering for deterministic diffs.
  bool KeyLess(const SnapshotEntry& o) const {
    if (identity != o.identity) return identity < o.identity;
    if (site != o.site) return site < o.site;
    return trust < o.trust;
  }
};

enum class SnapshotError {
  kNone = 0,
  kMalformedInput,   // not JSON / wrong shape / unknown schema
  kBudgetExceeded,   // encoded blob would exceed 32 KB (encoder-side stop)
  kHashMismatch,     // integrity field does not match payload
  kVersionMismatch,  // unknown schema version
  kInvalidEntry,     // an entry failed strict EffectivePolicy validation
};

const char* ToString(SnapshotError e);

struct SnapshotEncodeResult {
  bool ok = false;
  std::string blob;         // canonical JSON
  SnapshotError error = SnapshotError::kNone;
  std::string error_detail;
};

struct SnapshotDecodeResult {
  bool ok = false;
  std::vector<SnapshotEntry> entries;  // the state this blob represents
  uint64_t seq = 0;
  uint64_t base_seq = 0;
  SnapshotError error = SnapshotError::kNone;
  std::string error_detail;
};

// Deterministic ordering (diff stability); sorts in place.
void SortEntries(std::vector<SnapshotEntry>* entries);

// Full snapshot of `entries` at sequence `seq`.
SnapshotEncodeResult EncodeFullSnapshot(uint64_t seq, std::vector<SnapshotEntry> entries);

// Incremental diff from base (at base_seq) to `entries` (at seq).
// Deterministic: same inputs => same bytes.
SnapshotEncodeResult EncodeDiffSnapshot(uint64_t seq, uint64_t base_seq,
                                        const std::vector<SnapshotEntry>& base,
                                        std::vector<SnapshotEntry> entries);

// Strict decode of a full snapshot blob.
SnapshotDecodeResult DecodeFullSnapshot(const std::string& blob);

// Applies a diff blob to `base` and returns the reconstructed state.
// Verifies the result hash (the diff's hash covers the RECONSTRUCTED
// state), so a diff that does not reconstruct to the intended bytes fails.
SnapshotDecodeResult ApplyDiffSnapshot(const std::string& blob,
                                       const std::vector<SnapshotEntry>& base);

}  // namespace xr::policy
