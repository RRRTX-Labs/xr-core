// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// templates suite (P14-T2): all seven shipped; the CEREMONY INVENTORY is
// machine-checked (every applied default is listed, every listed default
// applies — Spec-A §3.9); Disposable and Tor are inert-with-reason (the
// P12-T6 honesty law); and the cannot-bypass negative: a default that
// applies without being listed in the ceremony reddens the checker.
#include <map>
#include <string>
#include <vector>

#include "harness.h"
#include "core/templates.h"

using xr::identity::ApplyTemplate;
using xr::identity::Ceremony;
using xr::identity::FindTemplate;
using xr::identity::TemplateRow;

namespace {

// The ceremony-completeness law as a function, so the negative can probe it:
// every row ApplyTemplate writes must appear as an "applied ns:key = value"
// ceremony line, and every ceremony "applied" line must be a row that was
// written. Returns "" when complete, else the first divergence (named).
std::string CeremonyDrift(const std::vector<TemplateRow>& applied,
                          const std::vector<std::string>& ceremony) {
  std::vector<std::string> applied_lines;
  for (const auto& r : applied) {
    applied_lines.push_back("applied " + r.ns + ":" + r.key + " = " + r.value);
  }
  for (const auto& line : applied_lines) {
    bool found = false;
    for (const auto& c : ceremony) found |= (c == line);
    if (!found) return "applied-but-not-listed: " + line;
  }
  for (const auto& c : ceremony) {
    if (c.rfind("applied ", 0) != 0) continue;
    bool found = false;
    for (const auto& line : applied_lines) found |= (c == line);
    if (!found) return "listed-but-never-applied: " + c;
  }
  return "";
}

}  // namespace

int main() {
  // 1. All seven templates exist, in menu order, with palettes and glyphs.
  auto all = xr::identity::AllTemplates();
  XR_EXPECT_EQ(all.size(), size_t(7));
  const char* kOrder[] = {"personal", "work",   "research", "banking",
                          "shopping", "disposable", "tor"};
  for (size_t i = 0; i < all.size(); ++i) {
    XR_EXPECT_STREQ(all[i].id.c_str(), kOrder[i]);
  }
  XR_EXPECT_STREQ(FindTemplate("tor")->color.c_str(), "#78288c");  // violet

  // 2. The ceremony inventory is COMPLETE for every template (§3.9).
  for (const auto& t : all) {
    std::map<std::string, std::string> prefs;
    std::string color, glyph;
    std::vector<TemplateRow> applied;
    XR_EXPECT_MSG(ApplyTemplate(t.id, &prefs, &color, &glyph, &applied),
                  "apply " + t.id);
    auto ceremony = Ceremony(t.id);
    XR_EXPECT_MSG(ceremony.has_value(), "ceremony " + t.id);
    const std::string drift = CeremonyDrift(applied, *ceremony);
    XR_EXPECT_MSG(drift.empty(), (t.id + ": " + drift));
    XR_EXPECT_MSG(!color.empty() && !glyph.empty(), "visual rows set");
  }

  // 3. THE CANNOT-BYPASS NEGATIVE: a default that applies without a
  // ceremony line (or a ceremony line that never applies) is CAUGHT. Plant
  // both shapes and prove the checker reddens each.
  {
    std::vector<TemplateRow> applied = {{"prefs", "secret.telemetry", "on"}};
    auto ceremony = *Ceremony("personal");  // has no secret.telemetry line
    XR_EXPECT_MSG(!CeremonyDrift(applied, ceremony).empty(),
                  "unlisted default is caught");
    std::vector<TemplateRow> none;
    auto with_ghost = *Ceremony("personal");
    with_ghost.push_back("applied prefs:ghost.row = 1");
    XR_EXPECT_MSG(!CeremonyDrift(none, with_ghost).empty(),
                  "listed-but-never-applied is caught");
  }

  // 4. Disposable: inert-with-reason — the flags say in-memory, and the
  // ceremony SAYS SO in prose (the honesty law), not just in flags.
  {
    auto t = FindTemplate("disposable");
    XR_EXPECT(t->disposable);
    const std::map<std::string, std::string> kWant = {
        {"vault.access", "none"}, {"session.in_memory_partition", "true"}};
    std::map<std::string, std::string> prefs;
    std::string c, g;
    XR_EXPECT(ApplyTemplate("disposable", &prefs, &c, &g));
    for (const auto& [k, v] : kWant) {
      XR_EXPECT_MSG(prefs.count(k) && prefs[k] == v,
                    "disposable row " + k + " = " + v);
    }
    auto ceremony = Ceremony("disposable");
    bool says_inert = false;
    for (const auto& line : *ceremony) {
      says_inert |= line.find("in-memory partition") != std::string::npos;
    }
    XR_EXPECT_MSG(says_inert, "ceremony states the in-memory posture");
  }

  // 5. Tor: a template NOW, route binding lands P31 — the shape exists
  // (violet, route row) but route_bound=false and the ceremony SAYS the
  // route is not bound (inert-with-reason; never a claim Tor works).
  {
    auto t = FindTemplate("tor");
    XR_EXPECT_STREQ(t->route.c_str(), "tor");
    XR_EXPECT_MSG(!t->route_bound, "route NOT bound (P31)");
    auto ceremony = Ceremony("tor");
    bool says_unbound = false;
    for (const auto& line : *ceremony) {
      says_unbound |= line.find("NOT BOUND YET") != std::string::npos;
    }
    XR_EXPECT_MSG(says_unbound,
                  "ceremony surfaces the inert route with its reason");
    std::map<std::string, std::string> prefs;
    std::string c, g;
    XR_EXPECT(ApplyTemplate("tor", &prefs, &c, &g));
    XR_EXPECT_MSG(prefs.count("net.route") && prefs["net.route"] == "tor",
                  "the route ROW exists (shape present)");
    XR_EXPECT_MSG(prefs.count("net.auto_update") &&
                      prefs["net.auto_update"] == "off",
                  "Tor template disables auto-update by policy");
    XR_EXPECT_STREQ(c.c_str(), "#78288c");  // violet border
  }

  // 6. Unknown template: apply refuses, ceremony nullopt — never a
  // silent "personal".
  {
    std::map<std::string, std::string> prefs;
    std::string c, g;
    XR_EXPECT_MSG(!ApplyTemplate("guest", &prefs, &c, &g),
                  "unknown template refused");
    XR_EXPECT_MSG(!Ceremony("guest").has_value(), "no ceremony for unknown");
  }

  // 7. Banking posture is restrictive at birth (the overlay rows a user
  // cannot forget to set): strict trust, no durable auth cookies.
  {
    std::map<std::string, std::string> prefs;
    std::string c, g;
    XR_EXPECT(ApplyTemplate("banking", &prefs, &c, &g));
    XR_EXPECT_STREQ(prefs["shield.trust_level"].c_str(), "strict");
    XR_EXPECT_STREQ(prefs["session.persistent_cookies"].c_str(), "off");
    XR_EXPECT_STREQ(prefs["net.webrtc"].c_str(), "disable_non_proxied_udp");
  }

  return xrtest::Report("identity/templates");
}
