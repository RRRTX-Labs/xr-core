// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/apply — activation bookkeeping for signed list
// bundles (P11-T2; the T3 manifest pipeline feeds it). The laws, mirrored
// from the update core's monotonic-seq design:
//   * bundle_version is monotonic per bundle_id: a downgrade is refused,
//     and an EQUAL re-offer is refused too (no silent re-activation);
//   * the previous active slot becomes last-known-good when a newer
//     bundle activates — a rollback story, never a silent swap;
//   * pins keep the last TWO activated slots (hot pin-in / pin-out): the
//     state invariant is that `pins` MUST contain the active slot; an
//     input state violating it is a hot-pin-out refusal, not a repair;
//   * apply timestamps are caller-supplied monotonic integers; an apply
//     with now_mono BEFORE the recorded last_apply_mono is a stale-clock
//     refusal.
#pragma once

#include <string>
#include <vector>

#include "common/core/json.h"
#include "shield/core/bundle.h"

namespace xr::shield {

struct BundleSlot {
  bool present = false;
  std::string bundle_id;
  std::string digest;
  long long version = -1;
};

struct ApplyState {
  BundleSlot active;
  BundleSlot lkg;  // last-known-good: the slot the active replaced
  std::vector<BundleSlot> pins;  // chronological, ≤2, MUST contain active
  long long last_apply_mono = -1;
};

enum class ApplyResult {
  kOk = 0,
  kRejectedDowngrade,
  kRejectedEqualReoffer,
  kRejectedStaleClock,
  kMalformed,
};

enum class StateInvariant {
  kOk = 0,
  kHotPinOut,      // pins missing the active slot
  kTooManyPins,    // more than two pins
  kLkgEqualsActiveSameSlot,  // lkg duplicate of active in the same slot run
};

StateInvariant CheckInvariants(const ApplyState& state, std::string* detail);

// Strict parse of {"state":{...}} — the host's apply/posture arg shape.
bool ParseApplyState(const common::JsonValue& args, ApplyState* out,
                     std::string* detail);

// Candidate activation. `candidate` must be a successfully parsed bundle.
// On kOk, `out` is the NEW state (the old one is not mutated).
ApplyResult ApplyBundle(const NormalizedBundle& candidate,
                        const ApplyState& state, long long now_mono,
                        ApplyState* out, std::string* detail);

common::JsonValue SlotToJson(const BundleSlot& slot);
common::JsonValue ApplyStateToJson(const ApplyState& state);

}  // namespace xr::shield
