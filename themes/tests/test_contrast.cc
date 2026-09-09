// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// WCAG 2.1 contrast suite. Worked examples (research-log-P8 item 5; formula
// source: W3C WCAG 2.1 definition of relative luminance + contrast ratio,
// fetched 2026-09-09). The independent cross-check required by the brief is
// the Python reference fake (fakes/themes.py implements the same definition
// independently); the xr-browser parity gate asserts byte-equal audits and
// ratio equality over worked-example pairs (two implementations agreeing).
#include <cmath>
#include <cstdio>
#include <string>

#include "harness.h"
#include "themes/core/contrast.h"
#include "themes/core/theme.h"

namespace {

bool Near(double a, double b, double eps = 0.011) {
  return std::fabs(a - b) <= eps;
}

}  // namespace

int main() {
  using namespace xr::themes;
  double r, g, b;

  // -- Hex parsing -------------------------------------------------------
  XR_EXPECT(HexToRgb("#ffffff", &r, &g, &b) && r == 1.0 && g == 1.0 &&
            b == 1.0);
  XR_EXPECT(HexToRgb("#000000", &r, &g, &b) && r == 0.0 && b == 0.0);
  XR_EXPECT_MSG(!HexToRgb("fffff", &r, &g, &b), "short hex refused");
  XR_EXPECT_MSG(!HexToRgb("#12zz45", &r, &g, &b), "non-hex refused");
  XR_EXPECT_MSG(HexToRgb("#ff0000aa", &r, &g, &b), "alpha form accepted");
  XR_EXPECT(Near(r, 1.0) && Near(g, 0.0) && Near(b, 0.0));

  // -- Relative luminance (definition): L = 0.2126R + 0.7152G + 0.0722B,
  // channel linearized: c/12.92 if c <= 0.04045 else ((c+0.055)/1.055)^2.4.
  XR_EXPECT(Near(RelativeLuminance(1, 1, 1), 1.0, 1e-9));
  XR_EXPECT(Near(RelativeLuminance(0, 0, 0), 0.0, 1e-9));
  // worked example 1: #8a5f00 (the light-theme caution amber).
  //   r=0x8a=138/255=0.5412 -> ((0.5412+0.055)/1.055)^2.4 = 0.2546
  //   g=0x5f=95/255=0.3725 -> 0.4053^2.4                = 0.1146
  //   b=0 -> 0
  //   L = 0.2126*0.2546 + 0.7152*0.1146 + 0 = 0.0541 + 0.0820 = 0.1361
  double l_amber = RelativeLuminance(
      0x8a / 255.0, 0x5f / 255.0, 0x00 / 255.0);
  XR_EXPECT(Near(l_amber, 0.1361, 0.002));
  // worked example 2: contrast of #8a5f00 on white = (1.05)/(0.1861) = 5.64
  XR_EXPECT(Near(ContrastBetween("#8a5f00", "#ffffff"), 5.64, 0.05));
  // worked example 3: black on white = 21:1 exactly (definitional max).
  XR_EXPECT(Near(ContrastBetween("#000000", "#ffffff"), 21.0, 1e-9));
  XR_EXPECT(Near(ContrastBetween("#ffffff", "#ffffff"), 1.0, 1e-9));

  // -- Threshold law -----------------------------------------------------
  // body 4.5: white text on #767676 fails; on #545454 passes (both well-
  // anchors (4.54:1 and 7.57:1 on white) verified by both
  // implementations (C++ core + fakes/themes.py).)
  double g7676 = ContrastBetween("#767676", "#ffffff");
  double g5454 = ContrastBetween("#545454", "#ffffff");
  XR_EXPECT_MSG(g7676 >= 4.5 - 0.011 && g7676 < 4.55,
                "gray #767676 anchors 4.54:1");
  XR_EXPECT(g5454 >= 7.5 && g5454 <= 7.7);
  XR_EXPECT(kBodyTextRatio == 4.5 && kLargeTextRatio == 3.0);
  XR_EXPECT(kSecurityCriticalRatio == 7.0);

  // -- Audit over the shipped data ---------------------------------------
  const std::string tokens_path = "ui/themes/tokens.json";
  bool ok = false;
  std::string toks = xrtest::ReadFile(tokens_path, &ok);
  if (!ok) toks = xrtest::ReadFile("../../" + tokens_path, &ok);
  XR_EXPECT_MSG(!toks.empty(), "tokens readable");
  if (!toks.empty()) {
    TokenSource src = LoadTokenSource(toks);
    XR_EXPECT_MSG(src.ok, src.error);
    if (src.ok) {
      // light carries exact waiver rows for its <7 security pairs; every
      // other built-in must audit with ZERO waivers (curated AAA).
      for (const auto& bt : src.builtins) {
        std::vector<ContrastFinding> fs =
            AuditTheme(bt.values, src.tokens, bt.waivers);
        size_t fail = 0;
        size_t waived = 0;
        for (const auto& f : fs) {
          if (!f.passed) ++fail;
          if (f.waived) ++waived;
        }
        XR_EXPECT_EQ(fail, 0u);
        if (bt.name == "light") {
          XR_EXPECT_EQ(waived, 9u);
        } else {
          XR_EXPECT_EQ(waived, 0u);  // dark/hc/dusk/prairie earn full AAA
        }
        // no unnecessary or stale waiver rows anywhere
        for (const auto& f : fs) {
          XR_EXPECT_MSG(!f.unnecessary, "no unnecessary waivers");
          XR_EXPECT_MSG(!f.waiver_mismatch, "no stale waivers");
        }
      }
    }
  }

  // -- Waiver parsing ----------------------------------------------------
  {
    std::string error;
    Waiver w;
    XR_EXPECT(ParseWaiverRow(
        "{\"best\":5.13,\"pair\":\"surface\",\"reason\":\"r\","
        "\"token\":\"danger-caution\"}",
        &w, &error));
    XR_EXPECT_STREQ(w.token, "danger-caution");
    XR_EXPECT_STREQ(w.pair, "surface");
    XR_EXPECT(Near(w.best, 5.13, 1e-9));
    XR_EXPECT_MSG(!ParseWaiverRow("{\"token\":1}", &w, &error),
                  "malformed waiver refused");
  }

  return xrtest::Report("contrast");
}
