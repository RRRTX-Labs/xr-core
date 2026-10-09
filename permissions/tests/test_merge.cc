// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P15 T6 — the labelled global-fallback merge, checked exhaustively over the
// full input cube (fortress x identity-state x global-state), plus the
// Fortress zero-prompt property (plan Manual row: "deny means no prompt").
//
// Properties checked on EVERY cell:
//  (1) no silent widening: if the effective state is wider than the global
//      answer, the verdict's source is the identity overlay, never the fallback;
//  (2) every verdict carries a non-empty label key, and a verdict that came
//      from the global fallback carries the global-fallback label;
//  (3) an identity-sourced verdict requires a live identity state;
//  (4) the Fortress deny-list is final.
// A planted merge that mislabels an unset identity as overlay-sourced must be
// caught by property (3): the check has teeth.
#include <optional>
#include <string>

#include "permissions/core/merge.h"
#include "policy/tests/harness.h"

using namespace xr::permissions;

namespace {

const CapState kStates[] = {CapState::kDeny, CapState::kAsk, CapState::kAllow};

// Returns the list of violated properties for one cell (empty = clean).
std::string Violations(bool fortress, std::optional<CapState> ident, CapState global,
                       const SiteVerdict& v) {
  std::string bad;
  if (static_cast<int>(v.effective) > static_cast<int>(global) && v.source != SiteSource::kIdentityOverlay) {
    bad += "silent-widening;";
  }
  if (v.label_key.empty()) bad += "no-label;";
  if (v.source == SiteSource::kGlobalFallback && v.label_key != "perm.source.global_fallback") {
    bad += "fallback-unlabelled;";
  }
  if (v.source == SiteSource::kIdentityOverlay && !ident.has_value()) bad += "overlay-without-identity;";
  if (fortress && (v.effective != CapState::kDeny || v.source != SiteSource::kFortressDeny)) {
    bad += "fortress-not-final;";
  }
  return bad;
}

}  // namespace

int main() {
  int cells = 0;
  int clean = 0;
  for (bool fortress : {false, true}) {
    for (int ident_idx = -1; ident_idx < 3; ++ident_idx) {
      std::optional<CapState> ident;
      if (ident_idx >= 0) ident = kStates[ident_idx];
      for (CapState global : kStates) {
        ++cells;
        SiteVerdict v = MergeSiteView(fortress, ident, global);
        std::string bad = Violations(fortress, ident, global, v);
        XR_EXPECT_MSG(bad.empty(), "merge property violated: " + bad);
        if (bad.empty()) ++clean;
      }
    }
  }
  XR_EXPECT_MSG(cells == 2 * 4 * 3, "the full cube is covered");
  XR_EXPECT_MSG(clean == cells, "every cell is clean");

  // Precedence, spelled out: fortress > identity > global.
  {
    SiteVerdict f = MergeSiteView(true, CapState::kAllow, CapState::kAllow);
    XR_EXPECT_MSG(f.effective == CapState::kDeny && f.source == SiteSource::kFortressDeny,
                  "fortress wins over an identity allow and a global allow");
    SiteVerdict i = MergeSiteView(false, CapState::kDeny, CapState::kAllow);
    XR_EXPECT_MSG(i.effective == CapState::kDeny && i.source == SiteSource::kIdentityOverlay,
                  "a live identity deny beats a global allow (visible as identity)");
    SiteVerdict g = MergeSiteView(false, std::nullopt, CapState::kAllow);
    XR_EXPECT_MSG(g.effective == CapState::kAllow && g.source == SiteSource::kGlobalFallback &&
                      g.label_key == "perm.source.global_fallback",
                  "unset identity falls back to global, and is labelled as the fallback");
    SiteVerdict e = MergeSiteView(false, std::nullopt, CapState::kAsk);
    XR_EXPECT_MSG(e.source == SiteSource::kGlobalFallback,
                  "an expired one-time grant (identity unset) reads as global, labelled");
  }

  // The planted defect: an unset identity mislabelled as overlay-sourced.
  {
    SiteVerdict honest = MergeSiteView(false, std::nullopt, CapState::kAllow);
    SiteVerdict mutant = honest;
    mutant.source = SiteSource::kIdentityOverlay;
    mutant.label_key = "perm.source.identity";
    XR_EXPECT_MSG(Violations(false, std::nullopt, CapState::kAllow, mutant).find("overlay-without-identity") !=
                      std::string::npos,
                  "a mislabelled fallback is caught (the merge check has teeth)");
    XR_EXPECT_MSG(Violations(false, std::nullopt, CapState::kAllow, honest).empty(), "the honest verdict is clean");
  }

  // --- Fortress zero prompts: the deny-list suppresses every prompt ----------
  {
    bool all_suppressed = true;
    for (CapState ident_or_global : kStates) {
      for (CapState global : kStates) {
        SiteVerdict v = MergeSiteView(true, ident_or_global, global);
        all_suppressed = all_suppressed && PromptSuppressed(v.effective);
      }
    }
    XR_EXPECT_MSG(all_suppressed, "a Fortress identity never reaches a prompt, whatever the other layers say");
  }

  // --- prompts: only kAsk prompts --------------------------------------------
  XR_EXPECT_MSG(PromptSuppressed(CapState::kDeny), "deny is silent");
  XR_EXPECT_MSG(PromptSuppressed(CapState::kAllow), "allow is already granted");
  XR_EXPECT_MSG(!PromptSuppressed(CapState::kAsk), "ask is the only prompting state");

  return xrtest::Report("test_merge");
}
