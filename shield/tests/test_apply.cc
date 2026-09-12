// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/apply (P11-T2) — monotonic activation (downgrade
// AND equal re-offer refused), LKG retention, the last-two pins with the
// hot-pin-out invariant, the stale-clock refusal, and strict state parse.
#include <string>

#include "common/core/json.h"
#include "shield/core/apply.h"
#include "shield/core/bundle.h"

#include "harness.h"

using namespace xr::shield;
using xr::common::ParseJson;

namespace {

NormalizedBundle Bundle(long long version) {
  NormalizedBundle b;
  b.name = "xr-default";
  b.bundle_version = version;
  b.digest = std::string(64, 'a');
  return b;
}

ApplyState Fresh() { return ApplyState{}; }

std::string StateJson(const ApplyState& s) {
  return ApplyStateToJson(s).Canonical();
}

bool ParseState(const std::string& json, ApplyState* out, std::string* d) {
  auto pr = ParseJson(R"({"state":)" + json + "}");
  if (!pr.ok) {
    *d = "bad-json";
    return false;
  }
  return ParseApplyState(pr.value, out, d);
}

}  // namespace

int main() {
  std::string d;
  // fresh activation
  ApplyState s1;
  {
    ApplyState s0 = Fresh();
    XR_EXPECT_MSG(ApplyBundle(Bundle(3), s0, 100, &s1, &d) == ApplyResult::kOk, d);
    XR_EXPECT(s1.active.present && s1.active.version == 3 &&
              s1.active.bundle_id == "xr-default");
    XR_EXPECT(!s1.lkg.present);              // nothing to remember yet
    XR_EXPECT(s1.pins.size() == 1);          // pin-in on activation
    XR_EXPECT(s1.last_apply_mono == 100);
  }
  // equal re-offer refused (no silent re-activation)
  {
    ApplyState out;
    XR_EXPECT(ApplyBundle(Bundle(3), s1, 200, &out, &d) ==
              ApplyResult::kRejectedEqualReoffer);
    XR_EXPECT(d == "bundle-version-equal-reoffer");
    XR_EXPECT(out.last_apply_mono == 100);   // refusal does not touch state
  }
  // downgrade refused
  {
    ApplyState out;
    XR_EXPECT(ApplyBundle(Bundle(2), s1, 200, &out, &d) ==
              ApplyResult::kRejectedDowngrade);
    XR_EXPECT(d == "bundle-version-downgrade");
  }
  // upgrade: old active becomes LKG; pins hold last-two
  ApplyState s2;
  {
    XR_EXPECT(ApplyBundle(Bundle(4), s1, 200, &s2, &d) == ApplyResult::kOk);
    XR_EXPECT(s2.active.version == 4);
    XR_EXPECT(s2.lkg.present && s2.lkg.version == 3);
    XR_EXPECT(s2.pins.size() == 2 && s2.pins[0].version == 3 &&
              s2.pins[1].version == 4);
    XR_EXPECT(s2.last_apply_mono == 200);
  }
  // second upgrade: LKG rotates, pins stay <= 2 (v3 pin-out)
  {
    ApplyState s3;
    XR_EXPECT(ApplyBundle(Bundle(5), s2, 300, &s3, &d) == ApplyResult::kOk);
    XR_EXPECT(s3.active.version == 5);
    XR_EXPECT(s3.lkg.version == 4);
    XR_EXPECT(s3.pins.size() == 2 && s3.pins[0].version == 4 &&
              s3.pins[1].version == 5);
  }
  // a DIFFERENT bundle_id starts its own monotonic line (fresh active)
  {
    NormalizedBundle other = Bundle(1);
    other.name = "other-bundle";
    ApplyState out;
    XR_EXPECT(ApplyBundle(other, s2, 250, &out, &d) == ApplyResult::kOk);
    XR_EXPECT(out.active.bundle_id == "other-bundle" && out.lkg.version == 4);
  }
  // stale clock refused
  {
    ApplyState out;
    XR_EXPECT(ApplyBundle(Bundle(9), s2, 150, &out, &d) ==
              ApplyResult::kRejectedStaleClock);
    XR_EXPECT(d == "stale-apply-clock");
    // boundary: equal clock is allowed (not stale)
    XR_EXPECT(ApplyBundle(Bundle(9), s2, 200, &out, &d) == ApplyResult::kOk);
  }
  // invariants
  {
    std::string det;
    XR_EXPECT(CheckInvariants(s2, &det) == StateInvariant::kOk);
    ApplyState bad = s2;
    bad.pins.clear();  // hot pin-out: active unpinned
    XR_EXPECT(CheckInvariants(bad, &det) == StateInvariant::kHotPinOut);
    XR_EXPECT(det == "pins-missing-active");
    bad = s2;
    bad.pins.push_back(bad.active);
    bad.pins.push_back(bad.active);  // 4 pins
    XR_EXPECT(CheckInvariants(bad, &det) == StateInvariant::kTooManyPins);
    bad = s2;
    bad.lkg = bad.active;  // lkg duplicating active
    XR_EXPECT(CheckInvariants(bad, &det) ==
              StateInvariant::kLkgEqualsActiveSameSlot);
    ApplyState empty = Fresh();
    XR_EXPECT(CheckInvariants(empty, &det) == StateInvariant::kOk);
  }
  // state parse round-trip
  {
    ApplyState back;
    XR_EXPECT_MSG(ParseState(StateJson(s2), &back, &d), d);
    XR_EXPECT(StateJson(back) == StateJson(s2));
  }
  // state parse strictness
  struct { const char* json; const char* frag; } bad[] = {
      {R"({"active":{"present":true},"lkg":{"present":false},"pins":[],)"
       R"("last_apply_mono":-1})", "bad-slot-fields:active"},
      {R"({"active":{"present":"yes"},"lkg":{"present":false},"pins":[],)"
       R"("last_apply_mono":-1})", "bad-present:active"},
      // P11-T4 mutation-matrix find: the slot-level refusals below were
      // UNTESTED (4 survivors in ParseSlot, 2 of them deny-guards) — the
      // full matrix at 20260912 reddened and these rows are the fix.
      {R"({"active":5,"lkg":{"present":false},"pins":[],)"
       R"("last_apply_mono":-1})", "slot-not-object:active"},
      {R"({"active":{"present":false,"bogus":1},"lkg":{"present":false},)"
       R"("pins":[],"last_apply_mono":-1})", "unknown-field:bogus"},
      {R"({"active":{"present":false},"lkg":{"present":false},"pins":[],)"
       R"("last_apply_mono":-1,"x":1})", "unknown-field:x"},
      {R"({"active":{"present":false},"lkg":{"present":false},"pins":[],)"
       R"("last_apply_mono":-2})", "bad-last-apply-mono"},
      {R"({"active":{"present":false},"lkg":{"present":false},)"
       R"("pins":"x","last_apply_mono":-1})", "pins-not-array"},
      {R"({"active":{"present":false},"lkg":{"present":false},)"
       R"("pins":[{"present":true,"bundle_id":"b","digest":"short",)"
       R"("version":1}],"last_apply_mono":-1})", "bad-slot-fields:pins[0]"},
      {R"({"active":{"present":true,"bundle_id":"b","version":1},)"
       R"("lkg":{"present":false},"pins":[],"last_apply_mono":-1})",
       "bad-slot-fields:active"},
  };
  for (const auto& c : bad) {
    ApplyState out;
    std::string det;
    bool ok = ParseState(c.json, &out, &det);
    XR_EXPECT_MSG(!ok && det.find(c.frag) != std::string::npos,
                  std::string("state refusal ") + c.frag + " (got " + det + ")");
  }
  // a valid full slot with 64-hex digest parses
  {
    ApplyState out;
    std::string j = R"({"active":{"present":true,"bundle_id":"b","digest":")" +
                    std::string(64, '0') + R"(","version":1},)" +
                    R"("lkg":{"present":false},"pins":[],"last_apply_mono":5})";
    XR_EXPECT_MSG(ParseState(j, &out, &d), d);
    XR_EXPECT(out.active.present && out.last_apply_mono == 5);
  }
  return xrtest::Report("test_apply");
}
