// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — strict JSON layer edge corpus: canonical output
// byte-parity with the Python reference form (json.dumps sort_keys +
// compact separators, ensure_ascii semantics incl. surrogate pairs), UTF-8
// validation (overlong / surrogate / truncated / out-of-range sequences),
// structural strictness (depth cap, trailing content, duplicate keys,
// control chars, invalid escapes), number edges. Floats and >int64 bignums
// are a DOCUMENTED divergence (no float fields exist in any frozen
// contract; validators reject doubles where integers are expected —
// deny-safe), so this suite pins only exact-match classes.
#include <string>

#include "policy/core/json.h"
#include "harness.h"

using namespace xr::policy;

namespace {

bool Parses(const std::string& s) {
  auto r = ParseJson(s);
  if (!r.ok) return false;
  return r.value.Canonical().size() > 0 || s.empty() || r.value.is_null();
}

bool Fails(const std::string& s) {
  auto r = ParseJson(s);
  return !r.ok;
}

}  // namespace

int main() {
  // ---- canonical parity with the Python reference form ----
  struct Canon { std::string in, want; };
  const Canon canon[] = {
      {"{\"b\":1,\"a\":2}", "{\"a\":2,\"b\":1}"},
      {"[1,2,3]", "[1,2,3]"},
      {"\"\\\"\\n\\t\\u0001\\u001f\\\\\"", "\"\\\"\\n\\t\\u0001\\u001f\\\\\""},
      {"\"é漢😀\x7f\"", "\"\\u00e9\\u6f22\\ud83d\\ude00\\u007f\""},
      {"\"\\ud83d\\ude00\"", "\"\\ud83d\\ude00\""},   // surrogate pair in/out
      {"-0", "0"},
      {"0", "0"},
      {"{\"a\":[1,{\"b\":null,\"c\":true,\"d\":false}]}",
       "{\"a\":[1,{\"b\":null,\"c\":true,\"d\":false}]}"},
      {"  [ ]  ", "[]"},
      {"{\"x\":{\"z\":1,\"y\":{\"w\":2}}}", "{\"x\":{\"y\":{\"w\":2},\"z\":1}}"},
      {"\"\\/\"", "\"/\""},                          // \/ unescapes; '/' raw out
  };
  for (const auto& c : canon) {
    auto r = ParseJson(c.in);
    XR_EXPECT_MSG(r.ok, "parse: " + c.in);
    if (r.ok) {
      XR_EXPECT_MSG(r.value.Canonical() == c.want,
                    "canonical of " + c.in + ": got " + r.value.Canonical() +
                        " want " + c.want);
    }
  }

  // ---- duplicate keys: keep-last (matches the Python harness) ----
  {
    auto r = ParseJson("{\"a\":1,\"a\":2}");
    XR_EXPECT(r.ok);
    XR_EXPECT_STREQ(r.value.Canonical().c_str(), "{\"a\":2}");
  }

  // ---- UTF-8 validation: every invalid class must fail ----
  XR_EXPECT(Fails("\"\xC0\x80\""));       // overlong 2-byte (NUL)
  XR_EXPECT(Fails("\"\xC0\xAF\""));       // overlong 2-byte (/)
  XR_EXPECT(Fails("\"\xE0\x80\x80\""));   // overlong 3-byte
  XR_EXPECT(Fails("\"\xF0\x80\x80\x80\""));  // overlong 4-byte
  XR_EXPECT(Fails("\"\xED\xA0\x80\""));   // UTF-8-encoded surrogate D800
  XR_EXPECT(Fails("\"\xED\xBF\xBF\""));   // UTF-8-encoded surrogate DFFF
  XR_EXPECT(Fails("\"\xF5\x80\x80\x80\""));  // > U+10FFFF
  XR_EXPECT(Fails("\"\xC2\""));           // truncated 2-byte
  XR_EXPECT(Fails("\"\xE2\x82\""));       // truncated 3-byte
  XR_EXPECT(Fails("\"\xF0\x9F\x98\""));   // truncated 4-byte
  XR_EXPECT(Fails("\"\x80\""));           // stray continuation byte
  XR_EXPECT(Fails("\"\xFF\""));           // invalid byte
  // Valid sequences must pass.
  XR_EXPECT(Parses("\"\xC3\xA9\""));              // é
  XR_EXPECT(Parses("\"\xE6\xBC\xA2\""));          // 漢
  XR_EXPECT(Parses("\"\xF0\x9F\x98\x80\""));      // 😀
  XR_EXPECT(Parses("\"\x7f\""));                  // DEL is valid UTF-8 (escaped out)

  // ---- escape strictness ----
  XR_EXPECT(Fails("\"\x01\""));           // raw control char
  XR_EXPECT(Fails("\"\\x41\""));          // invalid escape
  XR_EXPECT(Fails("\"\\u00\""));          // truncated \u
  XR_EXPECT(Fails("\"\\ud83d\""));        // lone high surrogate
  XR_EXPECT(Fails("\"\\udc00\""));        // lone low surrogate
  XR_EXPECT(Fails("\"\\ud83d\\ud83d\"")); // high + high

  // ---- structural strictness ----
  XR_EXPECT(Fails("{}{}"));               // trailing content
  XR_EXPECT(Fails("1 2"));
  XR_EXPECT(Fails("{"));
  XR_EXPECT(Fails("[1,"));
  XR_EXPECT(Fails("{\"a\" 1}"));
  XR_EXPECT(Fails("[01]"));               // leading zero
  XR_EXPECT(Fails("[-]"));
  XR_EXPECT(Fails("[1.]"));
  XR_EXPECT(Fails("[1e]"));
  XR_EXPECT(Fails("tru"));
  XR_EXPECT(Fails("nul"));
  XR_EXPECT(Parses("[1E5]"));             // exponent ok (double)
  XR_EXPECT(Parses("[-1.5e-3]"));
  // Depth cap: 60 nested arrays pass, 70 fail (limit 64).
  {
    std::string deep60(60, '[');
    deep60.append(60, ']');
    XR_EXPECT(Parses(deep60));
    std::string deep70(70, '[');
    deep70.append(70, ']');
    XR_EXPECT(Fails(deep70));
  }

  // ---- known, documented divergences from Python (deny-safe) ----
  // Python keeps arbitrary-precision ints; this implementation degrades
  // out-of-int64-range integers to doubles, which every strict validator
  // in the policy path REJECTS (never guessed). Pinned here so any change
  // to that policy is a conscious one.
  {
    auto r = ParseJson("[999999999999999999999999]");
    XR_EXPECT(r.ok);
    XR_EXPECT(r.value.as_array()[0].is_double());
  }

  // ---- error reporting carries an offset ----
  {
    auto r = ParseJson("{\"a\":1} garbage");
    XR_EXPECT(!r.ok);
    XR_EXPECT_MSG(r.error.find("trailing") != std::string::npos, r.error);
    XR_EXPECT(r.offset > 0);
  }

  return xrtest::Report("test_json");
}
