// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the site permission VIEW merge (P15 T6, "hidden grants"
// honesty) and the prompt-suppression predicate (P15 T1, the plan's Manual
// row "Fortress identity = zero prompts? deny means no prompt").
//
// The merge is data in, verdict out. It never decides policy: the resolver
// decided the effective state; this function only reports WHICH layer that
// state came from, so the Panel can label it. The label is the whole point:
// the default must be visible, not favourable.
#pragma once

#include <optional>
#include <string_view>

#include "permissions/core/types.h"

namespace xr::permissions {

enum class SiteSource { kIdentityOverlay = 0, kGlobalFallback = 1, kFortressDeny = 2 };

struct SiteVerdict {
  CapState effective = CapState::kDeny;
  SiteSource source = SiteSource::kGlobalFallback;
  std::string_view label_key;  // l10n key the Panel renders next to the state
};

// fortress_denied   : the identity is on the Fortress deny-list (wins outright)
// identity_state    : the identity's live state, or nullopt when unset OR its
//                     temporary grant has expired (the caller decides expiry)
// global_state      : the upstream/tier answer the fallback would give
SiteVerdict MergeSiteView(bool fortress_denied, std::optional<CapState> identity_state,
                          CapState global_state);

// A prompt is shown only for kAsk. Deny and allow never prompt: deny is silent,
// allow is already granted. Fortress identities resolve to deny, so they never
// prompt for the frozen four.
bool PromptSuppressed(CapState effective);

}  // namespace xr::permissions
