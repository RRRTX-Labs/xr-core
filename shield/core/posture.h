// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/posture — THE fail-open/fail-closed decision in one
// place (P11-T2). Nothing else in the tree decides what an engine death or
// a route loss means; every consumer (match.cc, the host, later the seam)
// asks posture. The asymmetry is the law and it is PROPERTY-TESTED by
// exhaustive enumeration of the finite input space (test_posture):
//
//   * engine death (dead OR poisoned) while the route is bound =>
//     FAIL-OPEN: browsing continues, the chip turns amber, matched
//     requests carry why_code engine-dead-fail-open. An ad engine is a
//     comfort feature; it never gets to brick the network stack.
//   * Guard route loss => FAIL-CLOSED, ALWAYS: chip red, requests are
//     refused with the frozen kFailClosed error code. Route integrity is
//     a safety property (P18 owns Guard; shield consumes route_bound and
//     must never conflate the two — research item 9).
//   * precedence is total and fixed: route law > engine law > kill switch
//     > normal. A kill switch cannot un-fail-close a lost route.
#pragma once

namespace xr::shield {

struct PostureInputs {
  bool engine_alive = true;     // BlockingEngine::alive()
  bool engine_poisoned = false; // mutex-poisoned / OOM-during-apply state
  bool route_bound = true;      // Guard route integrity (P18's signal)
  bool kill_switch_on = false;  // deliberate shield-off (P11-T6)
};

enum class PostureMode {
  kNormal = 0,     // deciding with a live engine on a bound route
  kFailOpen = 1,   // browsing continues WITHOUT blocking (amber)
  kFailClosed = 2, // requests refused (red) — the route law
};

enum class Chip {
  kGreen = 0,  // protecting
  kAmber = 1,  // NOT protecting, degraded or deliberately off — visible
  kRed = 2,    // fail-closed: refused
};

struct Posture {
  PostureMode mode = PostureMode::kNormal;
  Chip chip = Chip::kGreen;
  const char* reason = "normal";  // closed vocabulary, pinned by vectors
};

// The decision. Pure, total, deterministic — the same inputs always give
// the same Posture (no clock, no state, no I/O).
Posture DecidePosture(const PostureInputs& in);

const char* PostureModeName(PostureMode m);
const char* ChipName(Chip c);

}  // namespace xr::shield
