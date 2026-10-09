// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P15 T2 (presentation as data) and T7 (the attention ceiling for prompts).
// The prompt contract, as checks a reader can run:
//  * sensitive capabilities put ONE-TIME first (plan line 808);
//  * every prompt is a permutation of the four buttons (none missing, none twice);
//  * identity-scoped prompts carry the inline scope-copy key (plan line 809);
//  * a permission prompt is anchored (T3) and never stacked;
//  * T3 is capped at three per hour; overflow is demoted one tier and LOGGED,
//    never dropped; a second prompt while one is pending is demoted, logged.
#include <cstdint>
#include <set>
#include <string>

#include "permissions/core/present.h"
#include "policy/tests/harness.h"

using namespace xr::permissions;

namespace {

bool IsPermutationOfAll(const std::vector<Button>& order) {
  std::set<int> seen;
  for (Button b : order) seen.insert(static_cast<int>(b));
  return order.size() == 4 && seen.size() == 4;
}

}  // namespace

int main() {
  // --- button order ----------------------------------------------------------
  for (Capability c : {Capability::kGeolocation, Capability::kCamera, Capability::kMicrophone}) {
    std::vector<Button> order = ButtonOrder(c);
    XR_EXPECT_MSG(!order.empty() && order[0] == Button::kOnce,
                  std::string("one-time is the first button for sensitive ") + CapabilityName(c));
    XR_EXPECT_MSG(IsPermutationOfAll(order), std::string("four buttons, each once: ") + CapabilityName(c));
  }
  {
    std::vector<Button> order = ButtonOrder(Capability::kNotifications);
    XR_EXPECT_MSG(IsPermutationOfAll(order), "notifications: four buttons, each once");
    XR_EXPECT_MSG(order[0] == Button::kDeny, "notifications (not sensitive): deny first (UX review item)");
  }
  XR_EXPECT_MSG(std::string(ButtonName(Button::kOnce)) == "once" && std::string(ButtonName(Button::k7d)) == "7d",
                "stable button ids (the l10n keys derive from them)");

  // --- prompt model ----------------------------------------------------------
  {
    PromptModel m = BuildPrompt(Capability::kCamera, true);
    XR_EXPECT_MSG(m.tier == 3, "a permission prompt is an anchored T3 surface");
    XR_EXPECT_MSG(m.max_stacked == 1, "a permission prompt is never stacked");
    XR_EXPECT_MSG(m.scope_copy_key == "perm.scope.only_in_identity", "identity-scoped prompt carries the scope copy");
    XR_EXPECT_MSG(BuildPrompt(Capability::kCamera, false).scope_copy_key.empty(), "no identity scope, no scope copy");
    XR_EXPECT_MSG(m.buttons == ButtonOrder(Capability::kCamera), "the model's buttons are the order table");
  }

  // --- attention ceiling -----------------------------------------------------
  XR_EXPECT_MSG(kT3PromptsPerHour == 3, "plan line 240: T3 is capped at three per hour");
  {
    for (int64_t n = 0; n < 3; ++n) {
      CeilingDecision d = AttentionCeiling(n, false);
      XR_EXPECT_MSG(d.verdict == PromptVerdict::kShowT3 && !d.logged && d.tier_after == "T3",
                    "under the ceiling: shown as T3, nothing logged");
    }
    for (int64_t n = 3; n < 6; ++n) {
      CeilingDecision d = AttentionCeiling(n, false);
      XR_EXPECT_MSG(d.verdict == PromptVerdict::kDemoteT2 && d.logged && d.tier_after == "T2",
                    "over the ceiling: demoted one tier to T2, and LOGGED (never dropped)");
    }
    CeilingDecision stacked = AttentionCeiling(0, true);
    XR_EXPECT_MSG(stacked.verdict == PromptVerdict::kDemoteT2 && stacked.logged,
                  "a second prompt while one is pending is demoted and logged (never stacked)");
  }
  // Property over a grid: a demotion is always logged; a shown prompt never is.
  {
    int bad = 0;
    for (int64_t n = 0; n < 10; ++n) {
      for (bool pending : {false, true}) {
        CeilingDecision d = AttentionCeiling(n, pending);
        const bool demoted = d.verdict == PromptVerdict::kDemoteT2;
        if (demoted != d.logged) ++bad;
        if (demoted != (pending || n >= kT3PromptsPerHour)) ++bad;
      }
    }
    XR_EXPECT_MSG(bad == 0, "ceiling property: demoted iff (pending or over the cap), logged iff demoted");
  }

  return xrtest::Report("test_present");
}
