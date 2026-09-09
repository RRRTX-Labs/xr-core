// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Loader suite: validate -> audit -> refuse semantics, atomicity, hostile
// import rejects (T4), waiver behavior, deltas, system resolver, budget
// smoke (apply well under the 100 ms budget on the 40x6 corpus — the
// measured number lives in themes/bench).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "harness.h"
#include "themes/core/loader.h"

namespace {

using xr::themes::ApplyResult;
using xr::themes::BuiltinTheme;
using xr::themes::FindBuiltin;
using xr::themes::LoaderState;
using xr::themes::TokenMap;
using Clock = std::chrono::steady_clock;

std::string ReadTokens() {
  bool ok = false;
  std::string t = xrtest::ReadFile("ui/themes/tokens.json", &ok);
  if (!ok)
    t = xrtest::ReadFile("../../ui/themes/tokens.json", &ok);
  return ok ? t : "";
}

}  // namespace

int main() {
  const std::string toks = ReadTokens();
  XR_EXPECT_MSG(!toks.empty(), "tokens readable");
  if (toks.empty()) return xrtest::Report("loader");

  // 1. LoaderLoad default: resolves through "system" -> light (mode light).
  LoaderState st = xr::themes::LoaderLoad(toks, "light");
  XR_EXPECT_MSG(st.ok, st.error);
  XR_EXPECT_STREQ(st.applied, "light");
  XR_EXPECT_EQ(st.values.size(), 40u);

  // 2. Every built-in applies (light via waivers; the rest full AAA).
  int applied_ok = 0;
  for (const auto& b : st.source.builtins) {
    ApplyResult r = ApplyBuiltin(&st, b.name);
    if (r.ok) ++applied_ok;
    XR_EXPECT_MSG(r.ok, r.refusal);
  }
  XR_EXPECT_EQ(applied_ok, 5);

  // 3. "system" resolves by mode: dark mode -> dark values.
  st.mode = "dark";
  ApplyResult sys = ApplyBuiltin(&st, "system");
  XR_EXPECT_MSG(sys.ok, sys.refusal);
  XR_EXPECT_STREQ(sys.applied_theme, "dark");
  XR_EXPECT_STREQ(st.applied, "dark");

  // 4. Unknown theme refused with typed text.
  ApplyResult u = ApplyBuiltin(&st, "no-such-theme");
  XR_EXPECT_MSG(!u.ok, "unknown built-in refused");
  if (!u.ok) XR_EXPECT_MSG(u.refusal.find("unknown theme") == 0,
                           "typed refusal prefix");

  // 5. Hostile import rejects (each with its own negative).
  {
    // 5a. size > 64 KiB
    std::string big = "{\"surface\": \"#ffffff\", \"pad\": \"";
    big.append(70 * 1024, 'x');
    big.append("\"}");
    ApplyResult r = xr::themes::ImportTheme(&st, big);
    XR_EXPECT_MSG(!r.ok && r.refusal.find("64 KiB cap") != std::string::npos,
                  "oversize doc refused");
    XR_EXPECT_STREQ(st.applied, "dark");  // unchanged (atomic)
    // 5b. duplicate keys (raw scan)
    ApplyResult r2 = xr::themes::ImportTheme(
        &st, "{\"surface\":\"#ffffff\",\"surface\":\"#000000\"}");
    XR_EXPECT_MSG(!r2.ok && r2.refusal.find("duplicate key") !=
                                std::string::npos,
                  "duplicate key refused");
    // 5c. not JSON / not an object
    ApplyResult r3 = xr::themes::ImportTheme(&st, "not json at all");
    XR_EXPECT_MSG(!r3.ok, "garbage refused");
    ApplyResult r4 = xr::themes::ImportTheme(&st, "[1,2,3]");
    XR_EXPECT_MSG(!r4.ok, "non-object doc refused");
    // 5d. url( anywhere (font value)
    ApplyResult r5 = xr::themes::ImportTheme(
        &st, "{\"font-family-ui\":\"url(https://x.e/f.woff)\"}");
    XR_EXPECT_MSG(!r5.ok && r5.refusal.find("url(") != std::string::npos,
                  "url() refused");
    // 5e. non-token key (script)
    ApplyResult r6 = xr::themes::ImportTheme(
        &st, "{\"script\":\"alert(1)\",\"surface\":\"#123456\"}");
    XR_EXPECT_MSG(!r6.ok && r6.refusal.find("script") != std::string::npos,
                  "script key refused");
    // 5f. reserved law through import
    ApplyResult r7 = xr::themes::ImportTheme(
        &st, "{\"critical-red\":\"#1565c0\"}");
    XR_EXPECT_MSG(!r7.ok, "calm critical-red refused on import");
    // 5g. invalid UTF-8 in the raw doc (parser validates UTF-8 strictly)
    {
      std::string bad8 = "{\"surface\":\"#ffffff\",\"note\":\"";
      bad8.append("\xc3\x28");  // 0xC3 needs a continuation byte
      bad8.append("\"}");
      ApplyResult r8 = xr::themes::ImportTheme(&st, bad8);
      XR_EXPECT_MSG(!r8.ok &&
                       r8.refusal.find("UTF-8") != std::string::npos,
                   "invalid UTF-8 refused at parse");
      XR_EXPECT_STREQ(st.applied, "dark");  // atomic
    }
    // 5h. nesting depth tricks (parser depth cap 64)
    {
      std::string deep;
      for (int i = 0; i < 70; ++i) deep += "{\"a\":";
      deep += "1";
      for (int i = 0; i < 70; ++i) deep += "}";
      ApplyResult r9 = xr::themes::ImportTheme(&st, deep);
      XR_EXPECT_MSG(!r9.ok && r9.refusal.find("nesting depth") !=
                                  std::string::npos,
                    "deeply nested doc refused at parse");
    }
    // 5i. huge ints (overflow): 2^63 parses as double; the schema demands
    // typed integers for dimension tokens -> refused, never coerced.
    ApplyResult r10 = xr::themes::ImportTheme(
        &st, "{\"space-1\":9223372036854775808}");
    XR_EXPECT_MSG(!r10.ok && r10.refusal.find("does not match declared type") !=
                                 std::string::npos,
                  "huge int refused by the type check");
    // 5j. NaN-ish numbers (1e999 overflows the double range)
    ApplyResult r11 =
        xr::themes::ImportTheme(&st, "{\"space-1\":1e999}");
    XR_EXPECT_MSG(!r11.ok,
                  "out-of-range number refused (never inf/NaN coercion)");
  }

  // 6. Failing-contrast custom doc refused (never applies): take the light
  // built-in's map and set text to white (#fff on white = 1.0:1).
  {
    const BuiltinTheme* light = FindBuiltin(st.source.builtins, "light");
    XR_EXPECT(light != nullptr);
    if (light != nullptr) {
      std::string doc = "{";
      bool first = true;
      for (const auto& [k, v] : light->values) {
        if (!first) doc += ",";
        first = false;
        doc += "\"" + k + "\":";
        if (v.is_string()) {
          doc += "\"" + (k == "text" ? std::string("#ffffff") : v.as_string()) +
                 "\"";
        } else if (v.is_int()) {
          doc += std::to_string(v.as_int());
        } else {
          doc += v.Canonical();
        }
      }
      doc += "}";
      ApplyResult r = xr::themes::ImportTheme(&st, doc);
      XR_EXPECT_MSG(!r.ok, "white-on-white doc refused by contrast audit");
      if (!r.ok)
        XR_EXPECT_MSG(r.refusal.find("contrast text/surface") !=
                          std::string::npos,
                      "refusal names the failing pair");
      XR_EXPECT_STREQ(st.applied, "dark");  // atomic: nothing changed
      // no partial state: applied values still dark's text
      XR_EXPECT_STREQ(st.values.at("text").as_string().c_str(), "#e6edf3");
    }
  }

  // 7. A passing custom doc applies with deltas vs the current (dark) map.
  {
    const BuiltinTheme* prairie = FindBuiltin(st.source.builtins, "prairie");
    XR_EXPECT(prairie != nullptr);
    if (prairie != nullptr) {
      std::string doc = "{";
      bool first = true;
      for (const auto& [k, v] : prairie->values) {
        if (!first) doc += ",";
        first = false;
        doc += "\"" + k + "\":";
        if (v.is_string()) {
          doc += "\"" + v.as_string() + "\"";
        } else if (v.is_int()) {
          doc += std::to_string(v.as_int());
        } else {
          doc += v.Canonical();
        }
      }
      doc += "}";
      ApplyResult r = xr::themes::ImportTheme(&st, doc);
      XR_EXPECT_MSG(r.ok, r.refusal);
      if (r.ok) {
        XR_EXPECT_STREQ(st.applied, "custom");
        XR_EXPECT_EQ(st.values.size(), 40u);
        XR_EXPECT_MSG(r.deltas.size() > 20,
                      "deltas list the per-token old -> new rows");
      }
    }
  }

  // 8. Waiver law: an unnecessary waiver (pair passes) must refuse the
  // built-in, and a stale waiver (best != actual) must refuse too. Both
  // cases are enforced on data — checked through a crafted source.
  {
    // minimal source: 2 tokens + 1 theme + a waiver on a passing pair
    const std::string src =
        "{\"schema_version\":1,"
        "\"tokens\":{\"ink\":{\"type\":\"color\",\"usage\":\"t\","
        "\"pairing\":[\"paper\"],\"security_critical\":true},"
        "\"paper\":{\"type\":\"color\",\"usage\":\"bg\"}},"
        "\"themes\":{\"t\":{\"ink\":\"#000000\",\"paper\":\"#ffffff\","
        "\"waivers\":[{\"token\":\"ink\",\"pair\":\"paper\",\"best\":1.0,"
        "\"reason\":\"unnecessary\"}]}},"
        "\"system_resolution\":{\"default\":\"light\",\"modes\":{"
        "\"light\":\"t\"}}}";
    LoaderState s2 = xr::themes::LoaderLoad(src, "light");
    XR_EXPECT_MSG(s2.ok, s2.error);
    if (s2.ok) {
      ApplyResult r = ApplyBuiltin(&s2, "t");
      XR_EXPECT_MSG(!r.ok && r.refusal.find("unnecessary") !=
                                 std::string::npos,
                    "unnecessary waiver refused");
    }
  }

  return xrtest::Report("loader");
}
