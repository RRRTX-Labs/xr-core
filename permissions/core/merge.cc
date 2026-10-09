// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — view merge + prompt suppression (see merge.h). Precedence is
// fixed and total: Fortress deny, then the identity's live state, then the
// global fallback. Every branch carries exactly one source and one label key.
#include "permissions/core/merge.h"

namespace xr::permissions {

SiteVerdict MergeSiteView(bool fortress_denied, std::optional<CapState> identity_state,
                          CapState global_state) {
  if (fortress_denied) {
    return {CapState::kDeny, SiteSource::kFortressDeny, "perm.source.fortress_deny"};
  }
  if (identity_state.has_value()) {
    return {*identity_state, SiteSource::kIdentityOverlay, "perm.source.identity"};
  }
  return {global_state, SiteSource::kGlobalFallback, "perm.source.global_fallback"};
}

bool PromptSuppressed(CapState effective) {
  return effective != CapState::kAsk;
}

}  // namespace xr::permissions
