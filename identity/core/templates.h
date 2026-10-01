// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the identity TEMPLATES engine (P14-T2): Personal / Work /
// Research / Banking / Shopping / Disposable / Tor. A template is a named,
// reviewable set of OVERLAY ROWS (prefs + policy + visual) applied at
// creation. The Spec-A §3.9 transparency amendment, made MACHINE-CHECKED:
// every default a template applies is listed in its CEREMONY INVENTORY —
// `Ceremony()` returns exactly the rows that would be applied, and the
// completeness test diffs the two (a default that applies without being
// listed — or a listed default that never applies — reddens; the negative
// plants both).
//
// Tor is a template NOW with its route binding deferred to P31 (the plan's
// own carve-out): the shape exists — violet border glyph/color rows and a
// route=tor row that the route manager will bind — and `route_bound` stays
// false (surfaced in the ceremony text) until P31 binds it. Nothing here
// CLAIMS Tor routing works.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xr::identity {

struct TemplateRow {
  std::string ns;     // "prefs" | "policy" | "visual"
  std::string key;
  std::string value;
};

struct IdentityTemplate {
  std::string id;             // "personal", "work", ...
  std::string label;
  std::string color;          // hex; the plan's palette
  std::string glyph;          // short mark
  bool disposable = false;    // in-memory partition (Disposable)
  bool route_bound = false;   // Tor only: false until P31 binds the route
  std::string route;          // "" | "tor"
  std::vector<TemplateRow> rows;
};

// The template registry, in creation-menu order.
std::vector<IdentityTemplate> AllTemplates();

std::optional<IdentityTemplate> FindTemplate(std::string_view id);

// The CEREMONY INVENTORY for a template id: one human-readable line per
// applied default, in the order rows are applied — exactly the rows
// ApplyTemplate would write (the completeness test asserts set equality and
// order; a divergence is the §3.9 red). Unknown id ⇒ nullopt.
std::optional<std::vector<std::string>> Ceremony(std::string_view id);

// Apply the template's rows onto `out` (prefs/policy rows go to the prefs
// namespace; visual rows set color/glyph). When `applied` is non-null it
// receives EXACTLY the rows written, in order — the ceremony completeness
// test diffs this against Ceremony() (the §3.9 law, machine-checked).
// Returns false for unknown id.
bool ApplyTemplate(std::string_view id,
                   std::map<std::string, std::string>* prefs_rows,
                   std::string* color, std::string* glyph,
                   std::vector<TemplateRow>* applied = nullptr);

}  // namespace xr::identity
