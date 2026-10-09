// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — presentation tables and the T3 ceiling (see present.h).
#include "permissions/core/present.h"

namespace xr::permissions {

const char* ButtonName(Button b) {
  switch (b) {
    case Button::kOnce: return "once";
    case Button::kSession: return "session";
    case Button::k7d: return "7d";
    case Button::kDeny: return "deny";
  }
  return "deny";
}

std::vector<Button> ButtonOrder(Capability c) {
  if (IsSensitive(c)) {
    return {Button::kOnce, Button::kSession, Button::k7d, Button::kDeny};
  }
  return {Button::kDeny, Button::kOnce, Button::kSession, Button::k7d};
}

PromptModel BuildPrompt(Capability c, bool identity_scoped) {
  PromptModel m;
  m.capability = c;
  m.buttons = ButtonOrder(c);
  m.scope_copy_key = identity_scoped ? "perm.scope.only_in_identity" : "";
  return m;
}

CeilingDecision AttentionCeiling(int64_t prompts_in_last_hour, bool prompt_pending) {
  CeilingDecision d;
  if (prompt_pending) {
    // Never stacked: a second prompt while one is on screen is demoted, logged.
    d.verdict = PromptVerdict::kDemoteT2;
    d.logged = true;
    d.tier_after = "T2";
    return d;
  }
  if (prompts_in_last_hour >= kT3PromptsPerHour) {
    d.verdict = PromptVerdict::kDemoteT2;
    d.logged = true;
    d.tier_after = "T2";
    return d;
  }
  d.verdict = PromptVerdict::kShowT3;
  d.logged = false;
  d.tier_after = "T3";
  return d;
}

}  // namespace xr::permissions
