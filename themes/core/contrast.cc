// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// WCAG 2.1 contrast implementation. The math follows the W3C definition
// (research-log-P8 item 5: cite + worked examples + one cross-checked
// against an independent computation — the Python reference fake computes
// the same ratios independently and the parity lane compares them
// byte-for-byte over the built-in corpus).
#include "themes/core/contrast.h"

#include <cmath>
#include <cstdlib>

namespace xr::themes {

bool HexToRgb(const std::string& hex, double* r, double* g, double* b) {
  if (hex.size() != 7 && hex.size() != 9) return false;
  if (hex[0] != '#') return false;
  auto nyb = [&](size_t i) -> int {
    char c = hex[i];
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int ri = nyb(1), r2 = nyb(2), gi = nyb(3), g2 = nyb(4), bi = nyb(5),
      b2 = nyb(6);
  if (ri < 0 || r2 < 0 || gi < 0 || g2 < 0 || bi < 0 || b2 < 0) return false;
  if (hex.size() == 9) {
    for (size_t i = 7; i <= 8; ++i)
      if (nyb(i) < 0) return false;
  }
  *r = (ri * 16 + r2) / 255.0;
  *g = (gi * 16 + g2) / 255.0;
  *b = (bi * 16 + b2) / 255.0;
  return true;
}

double RelativeLuminance(double r, double g, double b) {
  auto lin = [](double c) {
    return c <= 0.04045 ? c / 12.92
                        : std::pow((c + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

double ContrastRatio(double l1, double l2) {
  double hi = std::max(l1, l2);
  double lo = std::min(l1, l2);
  double v = (hi + 0.05) / (lo + 0.05);
  if (v < 1.0) v = 1.0;
  if (v > 21.0) v = 21.0;
  return v;
}

double ContrastBetween(const std::string& fg_hex, const std::string& bg_hex) {
  double r, g, b, r2, g2, b2;
  if (!HexToRgb(fg_hex, &r, &g, &b) || !HexToRgb(bg_hex, &r2, &g2, &b2))
    return 1.0;
  return ContrastRatio(RelativeLuminance(r, g, b),
                       RelativeLuminance(r2, g2, b2));
}

bool ParseWaiverRow(const std::string& canonical_row, Waiver* out,
                    std::string* error) {
  JsonParseResult pr = ParseJson(canonical_row);
  if (!pr.ok || !pr.value.is_object()) {
    if (error) *error = "waiver row must be a JSON object";
    return false;
  }
  const JsonValue& o = pr.value;
  const JsonValue* tok = o.find("token");
  const JsonValue* pair = o.find("pair");
  const JsonValue* best = o.find("best");
  const JsonValue* reason = o.find("reason");
  if (!tok || !tok->is_string() || !pair || !pair->is_string() || !best ||
      !best->is_number() || !reason || !reason->is_string()) {
    if (error)
      *error = "waiver row must carry string token/pair, number best, "
               "string reason";
    return false;
  }
  out->token = tok->as_string();
  out->pair = pair->as_string();
  out->best = best->is_int() ? static_cast<double>(best->as_int())
                             : best->as_double();
  out->reason = reason->as_string();
  return true;
}

std::vector<ContrastFinding> AuditTheme(const TokenMap& values,
                                        const std::vector<TokenDef>& tokens,
                                        const std::vector<std::string>&
                                            waiver_rows) {
  // Parse waiver rows once.
  std::vector<Waiver> waivers;
  for (const auto& row : waiver_rows) {
    Waiver w;
    std::string err;
    if (ParseWaiverRow(row, &w, &err)) waivers.push_back(w);
    // A malformed waiver row is a data error; the loader refuses the theme
    // with the row text, so it cannot silently disappear here.
  }
  auto find_waiver = [&](const std::string& tok,
                         const std::string& pair) -> const Waiver* {
    for (const auto& w : waivers)
      if (w.token == tok && w.pair == pair) return &w;
    return nullptr;
  };

  std::vector<ContrastFinding> out;
  auto hex_of = [&](const std::string& name) -> std::string {
    auto it = values.find(name);
    if (it == values.end() || !it->second.is_string()) return "";
    return it->second.as_string();
  };
  for (const auto& t : tokens) {
    if (t.type != "color" || t.pairing.empty()) continue;
    auto it = values.find(t.name);
    if (it == values.end() || !it->second.is_string()) continue;  // refused upstream
    const std::string fg = it->second.as_string();
    for (const auto& pair : t.pairing) {
      const TokenDef* pd = FindToken(tokens, pair);
      if (pd == nullptr || pd->type != "color") continue;  // data error refused upstream
      std::string bg = hex_of(pair);
      if (bg.empty()) continue;
      double ratio = ContrastBetween(fg, bg);
      double required = t.security_critical ? kSecurityCriticalRatio
                                            : kBodyTextRatio;
      const Waiver* w = find_waiver(t.name, pair);
      bool waiver_row = w != nullptr;
      bool waiver_mismatch = false;
      if (waiver_row && std::abs(w->best - ratio) > 0.011)
        waiver_mismatch = true;  // stale/aspirational — never honored
      bool waived = waiver_row && !waiver_mismatch;
      // Waiver eligibility floor: a waiver excuses a security pair in
      // [4.5, 7) ONLY — below the body-text floor nothing is waivable
      // (recorded audit law; enforced in data by the audit tool).
      if (waived && ratio < kBodyTextRatio) waived = false;
      bool passed = ratio >= required || waived;
      ContrastFinding f;
      f.token = t.name;
      f.pair = pair;
      f.ratio = ratio;
      f.required = required;
      f.fg_hex = fg;
      f.bg_hex = bg;
      f.passed = passed;
      f.waived = waived;
      f.waiver_mismatch = waiver_mismatch;
      // Data-hygiene law: ANY waiver row on a passing pair is unnecessary
      // (whether or not its best matches) — a waiver exists to excuse a
      // failing pair, so rows on passing pairs are refused as redundant.
      f.unnecessary = waiver_row && ratio >= required;
      if (waived) f.waiver_reason = w->reason;
      out.push_back(f);
    }
  }
  return out;
}

}  // namespace xr::themes
