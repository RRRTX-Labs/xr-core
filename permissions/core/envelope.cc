// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — extras envelope (see envelope.h). Deny-only by construction:
// ParseExtraState has no path that yields kAllow.
#include "permissions/core/envelope.h"

namespace xr::permissions {

bool IsExtraCapability(std::string_view name) {
  return name == "clipboard-read-write" || name == "sensors" || name == "midi" ||
         name == "pics";
}

bool ParseExtraState(std::string_view s, CapState* out) {
  if (s == "kDeny") { *out = CapState::kDeny; return true; }
  if (s == "kAsk") { *out = CapState::kAsk; return true; }
  return false;  // kAllow is refused here, by construction
}

CapState NarrowExtra(CapState upstream, CapState overlay) {
  return MostRestrictive(upstream, overlay);
}

}  // namespace xr::permissions
