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
//  * P15-CLOSE C6: every row of xr-browser's
//    docs/qa/keyboard-tasks-permissions.yaml (buttons, focus_first, sensitive,
//    scope_copy_key) equals this core, and every capability has exactly one row.
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>

#include "permissions/core/present.h"
#include "policy/tests/harness.h"

using namespace xr::permissions;

namespace {

std::string TasksPath() {
  const char* env = std::getenv("XR_BROWSER_ROOT");
  std::string root = (env != nullptr && *env) ? env : "../../../xr-browser";
  return root + "/docs/qa/keyboard-tasks-permissions.yaml";
}

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t");
  size_t b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// The data file is a flat list of `- id:` rows with `key: value` lines; this
// reads exactly the keys the law needs (the python checker owns the schema).
std::vector<std::map<std::string, std::string>> ReadTasks(bool* readable) {
  std::vector<std::map<std::string, std::string>> rows;
  std::ifstream in(TasksPath());
  *readable = in.good();
  std::string line;
  while (std::getline(in, line)) {
    std::string t = Trim(line);
    if (t.empty() || t[0] == '#') continue;
    if (t.rfind("- id:", 0) == 0) {
      rows.emplace_back();
      rows.back()["id"] = Trim(t.substr(5));
      continue;
    }
    if (rows.empty()) continue;
    size_t colon = t.find(':');
    if (colon == std::string::npos) continue;
    rows.back()[Trim(t.substr(0, colon))] = Trim(t.substr(colon + 1));
  }
  return rows;
}

std::string ButtonsText(const std::vector<Button>& order) {
  std::string out = "[";
  for (size_t i = 0; i < order.size(); ++i) {
    if (i) out += ", ";
    out += ButtonName(order[i]);
  }
  return out + "]";
}

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

  // --- P15-CLOSE C6: the keyboard-task data equals the core -------------------
  {
    bool readable = false;
    const auto rows = ReadTasks(&readable);
    XR_EXPECT_MSG(readable, "keyboard-tasks-permissions.yaml readable (XR_BROWSER_ROOT set?)");
    std::map<std::string, int> per_cap;
    for (const auto& r : rows) {
      const std::string id = r.count("id") ? r.at("id") : "?";
      Capability c{};
      const bool known = r.count("capability") && ParseCapability(r.at("capability"), &c);
      XR_EXPECT_MSG(known, id + ": capability is a core capability");
      if (!known) continue;
      ++per_cap[r.at("capability")];
      const PromptModel m = BuildPrompt(c, true);
      XR_EXPECT_MSG(r.count("buttons") && r.at("buttons") == ButtonsText(m.buttons),
                    id + ": buttons equal ButtonOrder");
      XR_EXPECT_MSG(r.count("focus_first") && r.at("focus_first") == ButtonName(m.buttons[0]),
                    id + ": focus lands on the core's first button");
      XR_EXPECT_MSG(r.count("sensitive") && r.at("sensitive") == (IsSensitive(c) ? "true" : "false"),
                    id + ": sensitive equals IsSensitive");
      XR_EXPECT_MSG(r.count("scope_copy_key") && r.at("scope_copy_key") == std::string(m.scope_copy_key),
                    id + ": scope copy key equals BuildPrompt's");
    }
    for (Capability c : {Capability::kGeolocation, Capability::kCamera, Capability::kMicrophone,
                         Capability::kNotifications}) {
      XR_EXPECT_MSG(per_cap[CapabilityName(c)] == 1,
                    std::string("exactly one keyboard task for ") + CapabilityName(c));
    }
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
