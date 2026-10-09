// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the capability ENVELOPE for capabilities that have no
// PermissionState slot (ADR-0051 decision (b)(i)). The frozen four are
// widenable by the overlay because they have a slot. An extra can only be
// recorded as kDeny or kAsk, and its effect is only to NARROW upstream's
// answer. A monotone-negating input cannot grant, which keeps one brain
// intact: nothing here can produce an allow that upstream did not give.
#pragma once

#include <string_view>

#include "permissions/core/types.h"

namespace xr::permissions {

// The four extras named by plan T1 (clipboard-read-write, sensors, midi, pics).
bool IsExtraCapability(std::string_view name);

// Accepts only kDeny | kAsk. kAllow (and any unknown name) is REFUSED, so a
// record that claims an allow for an extra is a corrupt record (store.cc).
bool ParseExtraState(std::string_view s, CapState* out);

// Narrowing only: the result is the more restrictive of upstream and overlay.
// Property (tested exhaustively): NarrowExtra(u, o) <= u and <= o.
CapState NarrowExtra(CapState upstream, CapState overlay);

}  // namespace xr::permissions
