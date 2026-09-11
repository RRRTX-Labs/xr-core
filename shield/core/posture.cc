// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/posture — the single decision point (see posture.h).
// The reason vocabulary below is CLOSED: every string here is pinned by the
// golden vectors and mirrored by the Python fake byte-for-byte.
#include "shield/core/posture.h"

namespace xr::shield {

Posture DecidePosture(const PostureInputs& in) {
  // 1. the route law is absolute: fail-CLOSED, whatever else is true.
  if (!in.route_bound) {
    return Posture{PostureMode::kFailClosed, Chip::kRed, "route-loss"};
  }
  // 2. engine death/poison: fail-OPEN, amber. Browsing continues; the chip
  //    says "not protecting" so the state is never invisible.
  if (!in.engine_alive) {
    return Posture{PostureMode::kFailOpen, Chip::kAmber, "engine-dead"};
  }
  if (in.engine_poisoned) {
    return Posture{PostureMode::kFailOpen, Chip::kAmber, "engine-poisoned"};
  }
  // 3. deliberate kill switch: fail-open, amber (off is a visible state,
  //    never a green lie). T6 owns the surfaces that flip it.
  if (in.kill_switch_on) {
    return Posture{PostureMode::kFailOpen, Chip::kAmber, "kill-switch"};
  }
  // 4. normal: live engine, bound route, switch off.
  return Posture{PostureMode::kNormal, Chip::kGreen, "normal"};
}

const char* PostureModeName(PostureMode m) {
  switch (m) {
    case PostureMode::kNormal: return "normal";
    case PostureMode::kFailOpen: return "fail-open";
    case PostureMode::kFailClosed: return "fail-closed";
  }
  return "fail-closed";  // unreachable; the safe name if it ever is
}

const char* ChipName(Chip c) {
  switch (c) {
    case Chip::kGreen: return "green";
    case Chip::kAmber: return "amber";
    case Chip::kRed: return "red";
  }
  return "red";
}

}  // namespace xr::shield
