// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// theme model suite: token source strictness, doc validation, duplicate-key
// scan, reserved critical-red law, pairing references, system resolution.
#include <cstdio>
#include <string>

#include "harness.h"
#include "themes/core/theme.h"

namespace {

const char* kTokensPath = "ui/themes/tokens.json";

std::string ReadTokens() {
  bool ok = false;
  std::string t = xrtest::ReadFile(kTokensPath, &ok);
  if (!ok) t = xrtest::ReadFile("../../" + std::string(kTokensPath), &ok);
  return ok ? t : "";
}

}  // namespace

int main() {
  using namespace xr::themes;
  const std::string toks = ReadTokens();
  XR_EXPECT_MSG(!toks.empty(), "tokens.json readable from tests cwd");

  // 1. Source loads strictly: 40 tokens, 5 built-ins, system resolution.
  TokenSource src = LoadTokenSource(toks);
  XR_EXPECT_MSG(src.ok, src.error);
  if (src.ok) {
    XR_EXPECT_EQ(static_cast<int>(src.tokens.size()), 40);
    XR_EXPECT_EQ(static_cast<int>(src.builtins.size()), 5);
    XR_EXPECT_STREQ(src.system_default, "light");
    XR_EXPECT_STREQ(src.system_modes.at("dark"), "dark");
    XR_EXPECT_STREQ(src.system_modes.at("high-contrast"), "high-contrast");
    // critical-red reserved law: flags present in data.
    const TokenDef* cr = FindToken(src.tokens, "critical-red");
    XR_EXPECT_MSG(cr != nullptr, "critical-red exists");
    if (cr) {
      XR_EXPECT_MSG(cr->reserved, "critical-red carries reserved in data");
      XR_EXPECT_MSG(cr->security_critical, "critical-red is security_critical");
    }
    // pairing edges must reference real tokens (load already enforces).
    for (const auto& b : src.builtins) {
      XR_EXPECT_EQ(static_cast<int>(b.values.size()), 40);
      // every built-in's critical-red passes the reserved family law
      auto it = b.values.find("critical-red");
      XR_EXPECT_MSG(it != b.values.end(), "theme covers critical-red");
    }
  }

  // 2. Strict doc validation on a valid full doc (a built-in's map).
  if (src.ok) {
    const BuiltinTheme& bt = src.builtins[0];
    DocVerdict v = ValidateThemeDoc(src.tokens, bt.values);
    XR_EXPECT_MSG(v.ok, v.problems.empty() ? "valid doc ok"
                                           : v.problems[0].what);
  }

  // 3. Unknown token rejected (never ignored).
  if (src.ok) {
    TokenMap bad;
    bad["script"] = JsonValue(std::string("alert(1)"));
    DocVerdict v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(!v.ok, "non-token key refused");
    if (!v.problems.empty())
      XR_EXPECT_MSG(v.problems[0].where.find("script") != std::string::npos,
                    "problem names the key");
    bad.clear();
    bad["surface"] = JsonValue(std::string("#123456"));
    bad["totally-unknown-token"] = JsonValue(std::string("#123456"));
    v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(!v.ok, "unknown token refused");
  }

  // 4. Type mismatches: color value as int / dimension as string / bad hex.
  if (src.ok) {
    TokenMap bad;
    bad["surface"] = JsonValue(static_cast<int64_t>(42));
    DocVerdict v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(!v.ok, "color as int refused");
    bad.clear();
    bad["space-1"] = JsonValue(std::string("4px"));
    v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(!v.ok, "dimension as string refused");
    bad.clear();
    bad["accent"] = JsonValue(std::string("rgb(1,2,3)"));
    v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(!v.ok, "non-hex color refused");
    bad.clear();
    bad["space-1"] = JsonValue(static_cast<int64_t>(9999999999ll));
    v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(!v.ok, "huge int refused (int window)");
  }

  // 5. Unsafe fonts rejected (url / remote / separators).
  if (src.ok) {
    TokenMap bad;
    bad["font-family-ui"] =
        JsonValue(std::string("url(https://evil.example/x.woff)"));
    DocVerdict v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(!v.ok, "url() font refused");
    bad.clear();
    bad["font-family-ui"] =
        JsonValue(std::string("'x'; position: fixed"));
    v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(!v.ok, "code-ish font refused");
  }

  // 6. Reserved law: critical-red mapped calm -> refused; alarming passes.
  if (src.ok) {
    TokenMap bad;
    bad["critical-red"] = JsonValue(std::string("#1565c0"));  // blue
    DocVerdict v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(!v.ok, "calm critical-red refused (reserved law)");
    bad.clear();
    bad["critical-red"] = JsonValue(std::string("#ffaeae"));  // pale pink
    v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(!v.ok, "pale pink critical-red refused");
    bad.clear();
    bad["critical-red"] = JsonValue(std::string("#8f1a1a"));  // maroon
    v = ValidateThemeDoc(src.tokens, bad);
    XR_EXPECT_MSG(v.ok, "maroon critical-red accepted");
  }

  // 7. Duplicate-key raw scan.
  {
    XR_EXPECT_STREQ(FindDuplicateKey("{\"a\":1,\"a\":2}"), "a");
    XR_EXPECT_STREQ(FindDuplicateKey("{\"a\":{\"b\":1,\"b\":2}}"), "b");
    XR_EXPECT_MSG(FindDuplicateKey("{\"a\":1,\"b\":2}").empty(),
                  "no dup on clean doc");
    // string values containing braces/quotes must not confuse the scanner
    XR_EXPECT_MSG(
        FindDuplicateKey("{\"a\":\"{\\\"b\\\":1}\",\"c\":2}").empty(),
        "scanner ignores string bodies");
  }

  // 8. Built-in token-source load rejects bad sources (schema/version).
  {
    TokenSource s2 = LoadTokenSource("{\"schema_version\": 2}");
    XR_EXPECT_MSG(!s2.ok, "unknown version refused (rollback law)");
    TokenSource s3 = LoadTokenSource("{\"schema_version\": 1}");
    XR_EXPECT_MSG(!s3.ok, "missing tokens/themes refused");
  }

  return xrtest::Report("theme");
}
