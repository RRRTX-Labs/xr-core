// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — prompt PRESENTATION as data (P15 T2) and the Attention Budget
// ceiling for permission prompts (P15 T7). Both are pure: the order of
// buttons, the copy KEY for the identity scope, and the ceiling verdict are
// values a test can read. No CSS folklore, no string literals for copy here
// (user-visible strings go through the l10n pipeline, not through this core).
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "permissions/core/types.h"

namespace xr::permissions {

enum class Button { kOnce = 0, kSession = 1, k7d = 2, kDeny = 3 };

// Stable button ids. The l10n key for a button is derived from its id.
const char* ButtonName(Button b);

// Sensitive capabilities put ONE-TIME first (plan line 808). Others put deny
// first. The non-sensitive split is a UX choice awaiting review (research log).
std::vector<Button> ButtonOrder(Capability c);

// T3 = an anchored prompt (docs/state/attention-budget.md). A permission prompt
// is never stacked: max_stacked is 1.
struct PromptModel {
  Capability capability = Capability::kGeolocation;
  std::vector<Button> buttons;
  std::string_view scope_copy_key;  // "" unless identity-scoped
  int tier = 3;
  int max_stacked = 1;
};

PromptModel BuildPrompt(Capability c, bool identity_scoped);

// ---- T7: the attention ceiling for permission prompts ----------------------
inline constexpr int64_t kT3PromptsPerHour = 3;  // plan line 240: T3 <= 3/hour

enum class PromptVerdict { kShowT3 = 0, kDemoteT2 = 1 };

struct CeilingDecision {
  PromptVerdict verdict = PromptVerdict::kShowT3;
  bool logged = false;          // a demotion is ALWAYS logged, never dropped
  std::string_view tier_after;  // "T3" when shown, "T2" when demoted one tier
};

// prompts_in_last_hour: the host's LOCAL counter (never uploaded).
// prompt_pending: a permission prompt is already on screen (never stack).
// Demotion goes one tier (T3 to T2), so the request is still shown, just less
// interruptively; nothing is silently dropped.
CeilingDecision AttentionCeiling(int64_t prompts_in_last_hour, bool prompt_pending);

}  // namespace xr::permissions
