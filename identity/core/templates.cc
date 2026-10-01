// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the template set (header has the laws). Every row below is a
// REAL default a real identity receives — the ceremony text is generated
// FROM the same rows (single source of truth), so the inventory cannot
// drift from the application.
#include "core/templates.h"

#include <utility>

namespace xr::identity {
namespace {

// Row shorthand used only in this file's table construction.
TemplateRow P(const std::string& k, const std::string& v) {
  return {"prefs", k, v};
}
TemplateRow Y(const std::string& k, const std::string& v) {
  return {"policy", k, v};
}
TemplateRow V(const std::string& k, const std::string& v) {
  return {"visual", k, v};
}

std::vector<IdentityTemplate> BuildTemplates() {
  std::vector<IdentityTemplate> t;
  t.push_back({"personal", "Personal", "#4285f4", "P", false, false, "", {
      P("browser.startup.homepage", "xr://newtab"),
      Y("shield.trust_level", "standard"),
      Y("history.ledger_tagging", "on"),
      V("tab.color_bar", "#4285f4"), V("tab.glyph", "P"),
  }});
  t.push_back({"work", "Work", "#1a73e8", "W", false, false, "", {
      P("browser.startup.homepage", "xr://newtab"),
      Y("shield.trust_level", "standard"),
      Y("history.ledger_tagging", "on"),
      Y("net.route_hint", "default"),  // labelled in Network tab (§5 note)
      V("tab.color_bar", "#1a73e8"), V("tab.glyph", "W"),
  }});
  t.push_back({"research", "Research", "#0f9d58", "R", false, false, "", {
      P("browser.startup.homepage", "xr://newtab"),
      Y("shield.trust_level", "standard"),
      Y("history.ledger_tagging", "on"),
      Y("net.prefetch", "off"),        // research posture: no prefetch
      V("tab.color_bar", "#0f9d58"), V("tab.glyph", "R"),
  }});
  t.push_back({"banking", "Banking", "#f4b400", "B", false, false, "", {
      P("browser.startup.homepage", "xr://newtab"),
      Y("shield.trust_level", "strict"),
      Y("history.ledger_tagging", "on"),
      Y("session.persistent_cookies", "off"),  // no durable auth cookies
      Y("net.prefetch", "off"),
      Y("net.webrtc", "disable_non_proxied_udp"),
      V("tab.color_bar", "#f4b400"), V("tab.glyph", "B"),
  }});
  t.push_back({"shopping", "Shopping", "#db4437", "S", false, false, "", {
      P("browser.startup.homepage", "xr://newtab"),
      Y("shield.trust_level", "standard"),
      Y("history.ledger_tagging", "on"),
      V("tab.color_bar", "#db4437"), V("tab.glyph", "S"),
  }});
  // Disposable: in-memory partition; close => zero-bytes (T6 wires the
  // FS-diff assertion); "no vault access" policy row active from birth.
  t.push_back({"disposable", "Disposable", "#f9ab00", "D", true, false, "", {
      P("browser.startup.homepage", "xr://newtab"),
      Y("shield.trust_level", "strict"),
      Y("vault.access", "none"),
      Y("session.in_memory_partition", "true"),
      Y("history.ledger_tagging", "on"),
      V("tab.color_bar", "#f9ab00"), V("tab.glyph", "D"),
      V("window.border", "amber-dashed"),
  }});
  // Tor: template NOW, route binding lands P31 — the shape exists, the
  // route row is present but route_bound=false (surfaced in the ceremony).
  t.push_back({"tor", "Tor", "#78288c", "T", false, false, "tor", {
      P("browser.startup.homepage", "xr://newtab"),
      Y("shield.trust_level", "strict"),
      Y("history.ledger_tagging", "on"),
      Y("net.route", "tor"),           // binding deferred to P31
      Y("net.dns", "through-tunnel-only"),
      Y("net.webrtc", "disabled"),
      Y("net.auto_update", "off"),     // Tor disables auto-update by policy
      V("tab.color_bar", "#78288c"), V("tab.glyph", "T"),
      V("window.border", "violet"),
  }});
  return t;
}

}  // namespace

std::vector<IdentityTemplate> AllTemplates() { return BuildTemplates(); }

std::optional<IdentityTemplate> FindTemplate(std::string_view id) {
  for (const auto& t : BuildTemplates()) {
    if (t.id == id) return t;
  }
  return std::nullopt;
}

std::optional<std::vector<std::string>> Ceremony(std::string_view id) {
  auto t = FindTemplate(id);
  if (!t) return std::nullopt;
  std::vector<std::string> lines;
  lines.push_back("template " + t->id + " (" + t->label + ")");
  if (t->disposable) {
    lines.push_back("disposable: in-memory partition — close means zero "
                    "bytes remain (FS-diff asserted, P14-T6)");
  }
  if (t->route == "tor") {
    lines.push_back(t->route_bound
                        ? "route: Tor (bound by the route manager)"
                        : "route: Tor — NOT BOUND YET (route binding lands "
                          "P31; nothing claims Tor routing works today)");
  }
  // Generated FROM the same rows ApplyTemplate writes — the inventory
  // cannot drift (single source of truth; the test plants a drift).
  for (const auto& r : t->rows) {
    lines.push_back("applied " + r.ns + ":" + r.key + " = " + r.value);
  }
  return lines;
}

bool ApplyTemplate(std::string_view id,
                   std::map<std::string, std::string>* prefs_rows,
                   std::string* color, std::string* glyph,
                   std::vector<TemplateRow>* applied) {
  auto t = FindTemplate(id);
  if (!t) return false;
  for (const auto& r : t->rows) {
    if (r.ns == "prefs" || r.ns == "policy") {
      if (prefs_rows) (*prefs_rows)[r.key] = r.value;
    }
    // The ceremony inventory is the FULL row list (visual rows included —
    // color/glyph are defaults too); `applied` mirrors Ceremony exactly.
    if (applied) applied->push_back(r);
  }
  if (color) *color = t->color;
  if (glyph) *glyph = t->glyph;
  return true;
}

}  // namespace xr::identity
