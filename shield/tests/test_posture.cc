// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the fail-open/fail-closed asymmetry as a PROPERTY, proven by
// EXHAUSTIVE enumeration of all 16 input combinations (P11-T2 acceptance).
// The laws: route loss is fail-CLOSED and absolute (nothing un-fail-closes
// it — not the kill switch, not a live engine); engine death/poison and the
// kill switch are fail-OPEN with an amber chip (degraded is never
// invisible); precedence route-loss > engine-dead > poisoned > kill-switch
// > normal. The chip is a pure function of the mode.
#include <string>

#include "shield/core/posture.h"

#include "harness.h"

using namespace xr::shield;

int main() {
  int fail_closed_seen = 0;
  int fail_open_seen = 0;
  // exhaustive: every subset of the four booleans
  for (int mask = 0; mask < 16; ++mask) {
    PostureInputs in;
    in.engine_alive = (mask & 1) != 0;
    in.engine_poisoned = (mask & 2) != 0;
    in.route_bound = (mask & 4) != 0;
    in.kill_switch_on = (mask & 8) != 0;
    Posture p = DecidePosture(in);
    const std::string ctx = "mask=" + std::to_string(mask);

    // PROPERTY 1 — the route law is absolute: !route_bound ⇒ fail-closed,
    // whatever else is true (including kill_switch and a healthy engine)
    if (!in.route_bound) {
      XR_EXPECT_MSG(p.mode == PostureMode::kFailClosed,
                    ctx + ": route loss is fail-closed");
      XR_EXPECT_MSG(p.chip == Chip::kRed, ctx + ": route loss is red");
      XR_EXPECT_MSG(std::string(p.reason) == "route-loss",
                    ctx + ": route-loss reason");
      ++fail_closed_seen;
      continue;
    }
    // PROPERTY 2 — engine death/poison ⇒ fail-OPEN (browsing continues),
    // amber (never a green lie), and death outranks poison in the reason
    if (!in.engine_alive) {
      XR_EXPECT_MSG(p.mode == PostureMode::kFailOpen,
                    ctx + ": engine death is fail-open");
      XR_EXPECT_MSG(p.chip == Chip::kAmber, ctx + ": engine death is amber");
      XR_EXPECT_MSG(std::string(p.reason) == "engine-dead",
                    ctx + ": engine-dead reason (outranks poisoned/kill)");
      ++fail_open_seen;
      continue;
    }
    if (in.engine_poisoned) {
      XR_EXPECT_MSG(p.mode == PostureMode::kFailOpen,
                    ctx + ": poison is fail-open");
      XR_EXPECT_MSG(p.chip == Chip::kAmber, ctx + ": poison is amber");
      XR_EXPECT_MSG(std::string(p.reason) == "engine-poisoned",
                    ctx + ": engine-poisoned reason (outranks kill)");
      ++fail_open_seen;
      continue;
    }
    // PROPERTY 3 — the kill switch is fail-open + amber: deliberate off is
    // a VISIBLE state, never green, never closed
    if (in.kill_switch_on) {
      XR_EXPECT_MSG(p.mode == PostureMode::kFailOpen,
                    ctx + ": kill switch is fail-open");
      XR_EXPECT_MSG(p.chip == Chip::kAmber, ctx + ": kill switch is amber");
      XR_EXPECT_MSG(std::string(p.reason) == "kill-switch",
                    ctx + ": kill-switch reason");
      ++fail_open_seen;
      continue;
    }
    // PROPERTY 4 — only the fully healthy combination is normal/green
    XR_EXPECT_MSG(p.mode == PostureMode::kNormal, ctx + ": healthy is normal");
    XR_EXPECT_MSG(p.chip == Chip::kGreen, ctx + ": healthy is green");
    XR_EXPECT_MSG(std::string(p.reason) == "normal", ctx + ": normal reason");
  }
  // the enumeration actually covered both sides of the asymmetry:
  // 8 route-loss combos closed, 7 degraded-open combos, 1 normal
  XR_EXPECT_MSG(fail_closed_seen == 8, "8/16 combinations fail closed");
  XR_EXPECT_MSG(fail_open_seen == 7, "7/16 combinations fail open");

  // ASYMMETRY, stated directly: the SAME kill switch that opens everything
  // else cannot open a lost route.
  {
    PostureInputs a{true, false, false, true};   // route lost, engine fine
    PostureInputs b{true, false, true, true};    // route bound, switch on
    XR_EXPECT(DecidePosture(a).mode == PostureMode::kFailClosed);
    XR_EXPECT(DecidePosture(b).mode == PostureMode::kFailOpen);
  }
  // determinism: same inputs, same output, repeatedly (no hidden state)
  {
    PostureInputs in{false, true, true, true};
    Posture p1 = DecidePosture(in);
    Posture p2 = DecidePosture(in);
    XR_EXPECT(p1.mode == p2.mode && p1.chip == p2.chip &&
              std::string(p1.reason) == std::string(p2.reason));
  }
  // name tables are total and stable (host output bytes)
  XR_EXPECT_STREQ(PostureModeName(PostureMode::kNormal), "normal");
  XR_EXPECT_STREQ(PostureModeName(PostureMode::kFailOpen), "fail-open");
  XR_EXPECT_STREQ(PostureModeName(PostureMode::kFailClosed), "fail-closed");
  XR_EXPECT_STREQ(ChipName(Chip::kGreen), "green");
  XR_EXPECT_STREQ(ChipName(Chip::kAmber), "amber");
  XR_EXPECT_STREQ(ChipName(Chip::kRed), "red");

  return xrtest::Report("test_posture");
}
