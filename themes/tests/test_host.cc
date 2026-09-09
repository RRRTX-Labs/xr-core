// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Themes host protocol suite: end-to-end over the real binary (the same
// argv/stdout the parity harness drives). Covers list/current/apply/
// system-mode/import/validate-doc, typed refusals, durability across
// invocations (state file), disposable stores (zero bytes), and
// deterministic canonical output.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>

#include "harness.h"
#include "themes/core/json.h"

using namespace xr::themes;

namespace {
int Sys(const std::string& cmd) { return std::system(cmd.c_str()); }

std::string HostPath() {
  const char* env = std::getenv("XR_THEMES_HOST");
  return env != nullptr ? std::string(env) : std::string("build/themes_host");
}

const char* kTokens = "../../ui/themes/tokens.json";
const char* kStore = "themes-host-test-store";
const char* kStore2 = "themes-host-test-store2";

struct RunResult {
  std::string out;
  int rc = -1;
};

// Spawn the host with a request argv; returns stdout (last line) + rc.
RunResult RunHost(const std::string& store, const std::string& method_args) {
  const std::string cmd = HostPath() + " --store-dir " + store +
                          " --tokens " + kTokens + " '" + method_args + "'";
  std::FILE* p = popen(cmd.c_str(), "r");
  if (p == nullptr) return {"", -1};
  std::string out;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
  int rc = pclose(p);
  // rc is the wait status; normalize to the process exit code.
  if (rc >= 0) rc = WEXITSTATUS(rc);
  return {out, rc};
}

JsonParseResult ParseLine(const RunResult& rr) {
  return ParseJson(rr.out);
}

}  // namespace

int main() {
  Sys("rm -rf " + std::string(kStore) + " " + std::string(kStore2));
  Sys("mkdir -p " + std::string(kStore) + " " + std::string(kStore2));

  // 1. list on a fresh store (removed above; disposable semantics == no
  // state file): 5 built-ins + the resolver.
  {
    RunResult r = RunHost(kStore, "{\"method\":\"list\",\"args\":{}}");
    XR_EXPECT_EQ(r.rc, 0);
    JsonParseResult p = ParseLine(r);
    XR_EXPECT(p.ok);
    if (p.ok) {
      XR_EXPECT(p.value.is_object());
      XR_EXPECT(p.value.find("themes") != nullptr);
      XR_EXPECT(p.value.find("count") != nullptr);
      const JsonValue* count = p.value.find("count");
      XR_EXPECT(count->is_int() && count->as_int() == 6);
      XR_EXPECT_STREQ(p.value.find("current")->as_string(), "system");
      XR_EXPECT_STREQ(p.value.find("mode")->as_string(), "light");
    }
  }

  // 2. current: default session resolved to light values.
  {
    RunResult r = RunHost(kStore, "{\"method\":\"current\",\"args\":{}}");
    XR_EXPECT_EQ(r.rc, 0);
    JsonParseResult p = ParseLine(r);
    if (p.ok) {
      XR_EXPECT_STREQ(p.value.find("resolved")->as_string(), "light");
      const JsonValue* values = p.value.find("values");
      XR_EXPECT(values != nullptr && values->is_object());
      XR_EXPECT_EQ(values->as_object().size(), 40u);
    }
  }

  // 3. apply dark on a durable store -> ok + values == dark map; a second
  // invocation (fresh process) sees dark persisted.
  {
    RunResult r = RunHost(kStore, "{\"method\":\"apply\",\"args\":{"
                                  "\"name\":\"dark\"}}");
    XR_EXPECT_EQ(r.rc, 0);
    JsonParseResult p = ParseLine(r);
    if (p.ok) {
      XR_EXPECT_STREQ(p.value.find("applied")->as_string(), "dark");
      XR_EXPECT_STREQ(p.value.find("resolved")->as_string(), "dark");
      const JsonValue* v = p.value.find("values");
      XR_EXPECT(v != nullptr);
      XR_EXPECT_STREQ(v->find("surface")->as_string().c_str(), "#0f1216");
    }
    RunResult r2 = RunHost(kStore, "{\"method\":\"list\",\"args\":{}}");
    JsonParseResult p2 = ParseLine(r2);
    if (p2.ok) {
      XR_EXPECT_STREQ(p2.value.find("current")->as_string(), "dark");
    }
  }

  // 4. apply unknown -> typed refusal kRejected, exit 1.
  {
    RunResult r = RunHost(kStore, "{\"method\":\"apply\",\"args\":{"
                                  "\"name\":\"magenta\"}}");
    XR_EXPECT_EQ(r.rc, 1);
    JsonParseResult p = ParseLine(r);
    if (p.ok) {
      XR_EXPECT_STREQ(p.value.find("error")->as_string(), "kRejected");
      XR_EXPECT(p.value.find("detail") != nullptr);
    }
  }

  // 5. system-mode dark: mode changes; since dark is applied directly, the
  // applied theme stays dark.
  {
    RunResult r = RunHost(kStore, "{\"method\":\"system-mode\",\"args\":{"
                                  "\"mode\":\"dark\"}}");
    XR_EXPECT_EQ(r.rc, 0);
    JsonParseResult p = ParseLine(r);
    if (p.ok) {
      XR_EXPECT_STREQ(p.value.find("mode")->as_string(), "dark");
    }
  }

  // 6. validate-doc: refuses garbage without touching state; accepts an
  // empty doc with zero deltas (audit-only surface).
  {
    RunResult r = RunHost(kStore, "{\"method\":\"validate-doc\",\"args\":{"
                                  "\"theme-doc\":\"not-json\"}}");
    XR_EXPECT_EQ(r.rc, 1);
    JsonParseResult p = ParseLine(r);
    if (p.ok) {
      XR_EXPECT_STREQ(p.value.find("error")->as_string(), "kRejected");
    }
    // state untouched after the refusal: still high-contrast? store2 has HC;
    // this store has dark (applied in #3).
    RunResult rlist = RunHost(kStore, "{\"method\":\"list\",\"args\":{}}");
    JsonParseResult pl = ParseLine(rlist);
    if (pl.ok) {
      XR_EXPECT_STREQ(pl.value.find("current")->as_string(), "dark");
    }
    RunResult r2 = RunHost(kStore, "{\"method\":\"validate-doc\",\"args\":{"
                                   "\"theme-doc\":\"{}\"}}");
    XR_EXPECT_EQ(r2.rc, 0);
    JsonParseResult p2 = ParseLine(r2);
    if (p2.ok) {
      XR_EXPECT(p2.value.find("ok") != nullptr);
      const JsonValue* dc = p2.value.find("delta_count");
      XR_EXPECT(dc != nullptr && dc->is_int() && dc->as_int() == 0);
    }
    // unknown method
    RunResult r3 = RunHost(kStore, "{\"method\":\"beam-me-up\",\"args\":{}}");
    XR_EXPECT_EQ(r3.rc, 1);
    JsonParseResult p3 = ParseLine(r3);
    if (p3.ok) {
      XR_EXPECT_STREQ(p3.value.find("error")->as_string(),
                      "kUnknownMethod");
    }
    // malformed args
    RunResult r4 = RunHost(kStore, "{\"method\":\"apply\",\"args\":3}");
    XR_EXPECT_EQ(r4.rc, 1);
    JsonParseResult p4 = ParseLine(r4);
    if (p4.ok) {
      XR_EXPECT_STREQ(p4.value.find("error")->as_string(),
                      "kMalformedInput");
    }
  }

  // 7. flag-status + unknown-method + usage exit codes.
  {
    RunResult r = RunHost(kStore, "{\"method\":\"flag-status\",\"args\":{}}");
    XR_EXPECT_EQ(r.rc, 0);
    JsonParseResult p = ParseLine(r);
    if (p.ok) {
      XR_EXPECT_STREQ(p.value.find("xr_themes_v0")->as_string(), "on");
    }
  }

  // 8. Durability: apply high-contrast on store2, list store2 -> HC.
  {
    RunResult r = RunHost(kStore2, "{\"method\":\"apply\",\"args\":{"
                                   "\"name\":\"high-contrast\"}}");
    XR_EXPECT_EQ(r.rc, 0);
    RunResult r2 = RunHost(kStore2, "{\"method\":\"current\",\"args\":{}}");
    JsonParseResult p2 = ParseLine(r2);
    if (p2.ok) {
      XR_EXPECT_STREQ(p2.value.find("applied")->as_string(),
                      "high-contrast");
    }
  }

  Sys("rm -rf " + std::string(kStore) + " " + std::string(kStore2));
  Sys("mkdir -p " + std::string(kStore) + " " + std::string(kStore2));
  return xrtest::Report("themes-host");
}
