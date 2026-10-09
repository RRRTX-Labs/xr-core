// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P15 envelope (ADR-0051 decision (b)(i)): extras are deny-only. Exhaustive
// over the 9 state pairs: the narrowed answer is never wider than either input,
// and no record can carry kAllow for an extra.
#include <string>

#include "permissions/core/envelope.h"
#include "policy/tests/harness.h"

using namespace xr::permissions;

int main() {
  // --- the four extras, and only those, are extras ---------------------------
  for (const char* n : {"clipboard-read-write", "sensors", "midi", "pics"}) {
    XR_EXPECT_MSG(IsExtraCapability(n), std::string("extra: ") + n);
  }
  XR_EXPECT_MSG(!IsExtraCapability("camera"), "a frozen capability is not an extra");
  XR_EXPECT_MSG(!IsExtraCapability("sensorz"), "near-miss names are not extras");
  XR_EXPECT_MSG(!IsExtraCapability(""), "empty is not an extra");

  // --- an extra can be recorded as deny or ask, never as allow ---------------
  CapState st = CapState::kAllow;
  XR_EXPECT_MSG(!ParseExtraState("kAllow", &st), "kAllow refused for an extra (deny-only envelope)");
  XR_EXPECT_MSG(ParseExtraState("kDeny", &st) && st == CapState::kDeny, "kDeny accepted");
  XR_EXPECT_MSG(ParseExtraState("kAsk", &st) && st == CapState::kAsk, "kAsk accepted");
  XR_EXPECT_MSG(!ParseExtraState("kAllowAll", &st), "unknown state refused");

  // --- monotone narrowing over all nine pairs --------------------------------
  const CapState all[] = {CapState::kDeny, CapState::kAsk, CapState::kAllow};
  int violations = 0;
  int cells = 0;
  for (CapState u : all) {
    for (CapState o : all) {
      ++cells;
      CapState n = NarrowExtra(u, o);
      if (static_cast<int>(n) > static_cast<int>(u)) ++violations;  // never wider than upstream
      if (static_cast<int>(n) > static_cast<int>(o)) ++violations;  // never wider than the overlay
      const bool is_min = (n == u) || (n == o);
      if (!is_min) ++violations;
    }
  }
  XR_EXPECT_MSG(cells == 9, "all nine pairs covered");
  XR_EXPECT_MSG(violations == 0, "NarrowExtra never widens: result <= upstream and <= overlay");
  XR_EXPECT_MSG(NarrowExtra(CapState::kAllow, CapState::kAsk) == CapState::kAsk,
                "upstream allow narrowed by an overlay ask is ask");
  XR_EXPECT_MSG(NarrowExtra(CapState::kDeny, CapState::kAllow) == CapState::kDeny,
                "an overlay allow cannot widen an upstream deny");

  return xrtest::Report("test_envelope");
}
