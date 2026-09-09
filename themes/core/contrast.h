// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — WCAG 2.1 contrast math (authoritative definition: W3C
// "Web Content Accessibility Guidelines (WCAG) 2.1", Understanding
// Success Criterion 1.4.3 Contrast (Minimum); relative luminance per the
// W3C definition — see research-log-P8 item 5 for the cited formula and
// worked examples). Everything here is pure double math over sRGB hex
// colors. Thresholds (P8-T3 theme audit):
//   body text       4.5 : 1
//   large text      3.0 : 1   (>=18 pt, or >=14 pt bold)
//   security-critical pairs 7.0 : 1 where achievable — a pair below 7 is
//   only acceptable with a machine-readable contrast_waivers row INSIDE the
//   token data (theme waivers list) naming token, pair, best ratio and a
//   reason. A waiver is a data row, never a comment; a waiver for a pair
//   that PASSES is itself a validation error (no unnecessary waivers).
// The audit treats every DECLARED pairing (tokens.json `pairing` arrays)
// as a foreground/background pair that must clear the required threshold
// for the foreground token's class: security_critical foregrounds are
// audited at 7.0, everything else at 4.5 (large-text 3.0 is available for
// callers that know a token renders large text; v1 declares no large-only
// tokens — recorded).
#pragma once

#include <string>
#include <vector>

#include "themes/core/theme.h"

namespace xr::themes {

// sRGB hex "#rrggbb" or "#rrggbbaa" -> (r,g,b) in 0..1 (alpha ignored for
// contrast; premultiplied compositing is out of scope for v1 — recorded).
// Returns false for malformed input (caller must refuse, never guess).
bool HexToRgb(const std::string& hex, double* r, double* g, double* b);

// WCAG 2.1 relative luminance of an sRGB channel triplet (linearized).
double RelativeLuminance(double r, double g, double b);

// Contrast ratio (L_hi + 0.05) / (L_lo + 0.05), clamped to [1, 21].
double ContrastRatio(double l1, double l2);

// Convenience: ratio between two hex colors (fg, bg). Returns 1.0 when a
// color is malformed (never used to pass anything — audits refuse first).
double ContrastBetween(const std::string& fg_hex, const std::string& bg_hex);

// Thresholds (WCAG 2.1 SC 1.4.3 + P8 audit rules).
constexpr double kBodyTextRatio = 4.5;
constexpr double kLargeTextRatio = 3.0;
constexpr double kSecurityCriticalRatio = 7.0;

// One audit finding on a declared pairing.
struct ContrastFinding {
  std::string token;   // foreground token name
  std::string pair;    // background token name
  double ratio = 0.0;
  double required = 0.0;
  std::string fg_hex;
  std::string bg_hex;
  bool passed = false;
  bool waived = false;      // a waiver row exists for this exact (token, pair)
  bool waiver_mismatch = false;  // waiver best != actual ratio (stale row)
  bool unnecessary = false;      // waiver present although the pair passes
  std::string waiver_reason;
};

// Full audit of one theme value map against the declared pairing graph:
// returns one finding per (token, pairing-background) edge. The caller
// (loader) refuses the theme unless every finding passes or is waived.
std::vector<ContrastFinding> AuditTheme(const TokenMap& values,
                                        const std::vector<TokenDef>& tokens,
                                        const std::vector<std::string>&
                                            waiver_rows);

// Loads a waiver row (canonical JSON object) -> (token, pair, best, reason).
// Returns false when the row is malformed (refuse: waivers must be exact).
struct Waiver {
  std::string token;
  std::string pair;
  double best = 0.0;
  std::string reason;
};
bool ParseWaiverRow(const std::string& canonical_row, Waiver* out,
                    std::string* error);

}  // namespace xr::themes
