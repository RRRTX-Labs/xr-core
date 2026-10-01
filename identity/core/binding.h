// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — window/tab IDENTITY BINDING (P14-T3, the core half; the
// UI half — the confirm dialog, the right-click menu — consumes this and
// can add nothing the laws here forbid). Laws (plan §"T3"):
//   * every window carries a DEFAULT identity; a tab's identity is its
//     window's default at creation (moves are explicit events);
//   * MOVE-ACROSS-IDENTITY IS DESTRUCTIVE: confirm + reload. A move onto a
//     tab that has already navigated (after_nav) is REFUSED — the P4 timing
//     finding: attaching to a navigated tab requires a destructive reload,
//     so a non-destructive attach does not exist and the API says so
//     (kNotPermitted), never silently reloads;
//   * a move that would SPLIT A TAB GROUP across identities is refused
//     (group coherence is law; the group moves together or not at all);
//   * SITE→IDENTITY IS A SUGGESTION, NEVER A SWITCH: suggestions are
//     recorded and surfaced; NO code path may change a tab's identity with
//     cause "suggestion" — the binding model records every change with its
//     cause, and a suggestion-caused change is structurally refused here
//     (the planted-auto-switch negative reddens in test_binding.cc).
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/identity.h"

namespace xr::identity {

enum class ChangeCause { kWindowDefault, kUserConfirmedMove, kRestore, kSuggestion };

struct BindingChange {
  uint64_t tab_id = 0;
  std::string from;
  std::string to;
  ChangeCause cause = ChangeCause::kWindowDefault;
};

struct Suggestion {
  std::string site;      // the site that suggested it
  std::string domain;    // suggested identity (opaque domain)
  bool surfaced = false; // shown to the user; NEVER applied
};

class BindingModel {
 public:
  // Window default identity (a window's tabs are born here).
  CallResult SetWindowDefault(std::string_view window, std::string_view domain);

  // Open a tab in `window` at its default identity. Returns the tab's
  // binding. Records the change with cause kWindowDefault.
  CallResult OpenTab(std::string_view window, uint64_t tab_id,
                     std::string_view opener_domain);

  // The destructive move: `after_nav` = the tab already navigated ⇒ refuse
  // (confirm + reload is the caller's contract; the API never reloads on
  // its own). `group_peers` = the OTHER tab ids in the tab's group with
  // their domains AFTER the pending move (the UI moves a group as one
  // confirmed action and names the group's target); a peer ending in a
  // different identity than `to` ⇒ refuse (group-split law). On success
  // the change is recorded with cause kUserConfirmedMove (the UI
  // confirmed — this call IS the confirmed action).
  CallResult MoveTab(std::string_view from, std::string_view to,
                     uint64_t tab_id, bool after_nav,
                     const std::map<uint64_t, std::string>& group_peers);

  // Restore (session restore, P14-T8): records with cause kRestore.
  CallResult RestoreTab(uint64_t tab_id, std::string_view domain);

  // Site→identity suggestion: RECORDED, SURFACED, NEVER APPLIED. There is
  // deliberately no API that applies a suggestion — auto-switch is
  // unrepresentable (see TestPlantedAutoSwitch for the probe).
  CallResult RecordSuggestion(std::string_view site, std::string_view domain);
  const std::vector<Suggestion>& suggestions() const { return suggestions_; }

  // Audit trail: every identity change ever made, with its cause. The
  // never-auto-switch law is a property of this list: no entry may carry
  // kSuggestion (TestPlantedAutoSwitch proves the refusal).
  const std::vector<BindingChange>& changes() const { return changes_; }
  std::optional<std::string> TabIdentity(uint64_t tab_id) const;

  // TEST-ONLY probe: attempt to change a tab's identity with the forbidden
  // cause. MUST return kNotPermitted — the test asserts it (the planted
  // auto-switch; if a future refactor makes this succeed, the law broke).
  CallResult TestPlantedAutoSwitch(uint64_t tab_id, std::string_view to);

 private:
  // The ONLY mutation point for a tab's identity. Structurally refuses the
  // suggestion cause — auto-switch is unrepresentable, not merely untested.
  CallResult ChangeTabIdentity(uint64_t tab_id, std::string_view to,
                               ChangeCause cause);
  std::map<uint64_t, std::string> tab_identity_;
  std::map<std::string, std::string> window_default_;
  std::vector<BindingChange> changes_;
  std::vector<Suggestion> suggestions_;
};

}  // namespace xr::identity
