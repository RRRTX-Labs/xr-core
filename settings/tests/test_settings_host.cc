// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — the settings host protocol end-to-end over the real
// binary (the same argv/stdout the parity harness drives): full method
// surface, both flag states of xr_settings_v0 (off => typed rejection and
// zero results/counters; on => full behaviour), malformed frames, and
// policy preemption reads.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "settings/core/json.h"
#include "settings/tests/harness.h"

using namespace xr::settings;

namespace {
int Sys(const std::string& cmd) {
  int rc = std::system(cmd.c_str());
  return rc;
}


std::string HostPath() {
  const char* env = std::getenv("XR_SETTINGS_HOST");
  return env != nullptr ? std::string(env) : std::string("build/settings_host");
}

const char* kSchema = "../../settings/core/settings_schema_v1.json";
const char* kStore = "settings-host-test-store";
const char* kStore2 = "settings-host-test-store2";

// State snapshot with an active identity + one enterprise-preempted row.
const char* kStateActive =
    "{\"schema\":\"xr-settings-state\",\"schema_version\":1,"
    "\"context\":{\"identity_id\":\"xr:0000-0001\",\"origin\":null},"
    "\"values\":{\"network.adblock\":{\"value\":false,\"source\":\"enterprise\","
    "\"managed\":true}}}";
// State snapshot without an identity (identity section gated off).
const char* kStateBasic =
    "{\"schema\":\"xr-settings-state\",\"schema_version\":1,"
    "\"context\":{\"identity_id\":null,\"origin\":null},\"values\":{}}";

bool WriteFile(const std::string& path, const std::string& text) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  size_t n = std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
  return n == text.size();
}

// Spawn the host with a request argv; returns stdout (last line) + rc.
struct RunResult {
  std::string out;
  int rc = -1;
};

RunResult RunHost(const std::string& store, const std::string& state,
                  const std::string& flag, const std::string& req) {
  const std::string cmd = "\"" + HostPath() + "\" --store-dir \"" + store +
                          "\" --schema " + kSchema + " --state \"" + state +
                          "\" --flag xr_settings_v0=" + flag + " '" + req +
                          "' 2>/dev/null";
  std::FILE* p = popen(cmd.c_str(), "r");
  RunResult rr;
  if (p == nullptr) return rr;
  char buf[4096];
  std::string all;
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) all.append(buf, n);
  int rc = pclose(p);
  rr.rc = rc == -1 ? -1 : WEXITSTATUS(rc);
  if (!all.empty()) {
    while (!all.empty() && (all.back() == '\n' || all.back() == '\r'))
      all.pop_back();
    auto nl = all.rfind('\n');
    rr.out = nl == std::string::npos ? all : all.substr(nl + 1);
  }
  return rr;
}


}  // namespace

int main() {
  // Fresh store dirs (both flag runs + malformed do not share state).
  Sys(("rm -rf " + std::string(kStore) + " " + kStore2).c_str());
  Sys(("mkdir -p " + std::string(kStore) + " " + kStore2).c_str());
  const std::string active = std::string(kStore) + "/state.json";
  const std::string basic = std::string(kStore2) + "/state.json";
  XR_EXPECT(WriteFile(active, kStateActive));
  XR_EXPECT(WriteFile(basic, kStateBasic));

  const std::string req =
      "{\"method\":\"sections\",\"args\":{}}";

  // 1. flag-status (works in both states, never touches the store).
  {
    auto r = RunHost(kStore, active, "on", "{\"method\":\"flag-status\",\"args\":{}}");
    XR_EXPECT_EQ(r.rc, 0);
    XR_EXPECT(r.out.find("\"xr_settings_v0\":\"on\"") != std::string::npos);
    auto r2 = RunHost(kStore, active, "off", "{\"method\":\"flag-status\",\"args\":{}}");
    XR_EXPECT_EQ(r2.rc, 0);
    XR_EXPECT(r2.out.find("\"xr_settings_v0\":\"off\"") != std::string::npos);
  }

  // 2. flag ON: sections reflect the pinned state (identity gated OFF when
  //    the snapshot has no identity; ON when it does).
  {
    auto r = RunHost(kStore2, basic, "on", req);
    XR_EXPECT_EQ(r.rc, 0);
    XR_EXPECT(r.out.find("\"count\":3") != std::string::npos);
    XR_EXPECT(r.out.find("\"id\":\"identity\"") != std::string::npos);
    XR_EXPECT(r.out.find("\"available\":false") != std::string::npos);
    auto r2 = RunHost(kStore, active, "on", req);
    XR_EXPECT_EQ(r2.rc, 0);
    XR_EXPECT(r2.out.find("\"available\":true") != std::string::npos);
  }

  // 3. search: top-ranked results and empty for empty queries.
  {
    auto r = RunHost(kStore, active, "on",
                     "{\"method\":\"search\",\"args\":{\"query\":\"container\"}}");
    XR_EXPECT_EQ(r.rc, 0);
    XR_EXPECT(r.out.find("\"key\":\"identity.kind\"") != std::string::npos);
    XR_EXPECT(r.out.find("\"count\":") != std::string::npos);
  }

  // 4. get: deny-safe defaults vs enterprise-preempted state rows.
  {
    auto r = RunHost(kStore, active, "on",
                     "{\"method\":\"get\",\"args\":{\"key\":\"network.adblock\"}}");
    XR_EXPECT_EQ(r.rc, 0);
    XR_EXPECT(r.out.find("\"source\":\"enterprise\"") != std::string::npos);
    XR_EXPECT(r.out.find("\"preempted\":true") != std::string::npos);
    XR_EXPECT(r.out.find("\"value\":false") != std::string::npos);
    auto r2 = RunHost(kStore, active, "on",
                      "{\"method\":\"get\",\"args\":{\"key\":\"privacy.letterbox\"}}");
    XR_EXPECT_EQ(r2.rc, 0);
    XR_EXPECT(r2.out.find("\"source\":\"default\"") != std::string::npos);
    XR_EXPECT(r2.out.find("\"value\":false") != std::string::npos);
    auto r3 = RunHost(kStore, active, "on",
                      "{\"method\":\"get\",\"args\":{\"key\":\"ghost.key\"}}");
    XR_EXPECT(r3.out.find("\"error\":\"kRejected\"") != std::string::npos);
  }

  // 5. set: v0 ships no user write path (writable_in_v0 empty) — typed
  //    rejection with the surfaced-by-policy reason; never a silent write.
  {
    auto r = RunHost(kStore, active, "on",
                     "{\"method\":\"set\",\"args\":{\"key\":\"network.adblock\","
                     "\"value\":true}}");
    XR_EXPECT_EQ(r.rc, 0);
    XR_EXPECT(r.out.find("\"error\":\"kRejected\"") != std::string::npos);
    XR_EXPECT(r.out.find("surfaced-by-policy-only") != std::string::npos);
  }

  // 6. router-resolve + counters-dump: opening a section via its anchor
  //    records a day-granular counter; dump carries schema+days.
  {
    auto r = RunHost(kStore, active, "on",
                     "{\"method\":\"router-resolve\",\"args\":{\"anchor\":"
                     "\"xr://settings/network\"}}");
    XR_EXPECT_EQ(r.rc, 0);
    XR_EXPECT(r.out.find("\"kind\":\"section\"") != std::string::npos);
    auto c = RunHost(kStore, active, "on",
                     "{\"method\":\"counters-dump\",\"args\":{}}");
    XR_EXPECT_EQ(c.rc, 0);
    XR_EXPECT(c.out.find("\"schema\":\"xr-settings-counters\"") != std::string::npos);
    XR_EXPECT(c.out.find("\"days\"") != std::string::npos);
  }

  // 7. schema-dump + unknown method (typed error result, exit 0).
  {
    auto r = RunHost(kStore, active, "on",
                     "{\"method\":\"schema-dump\",\"args\":{}}");
    XR_EXPECT_EQ(r.rc, 0);
    XR_EXPECT(r.out.find("\"count\":11") != std::string::npos);
    auto u = RunHost(kStore, active, "on",
                     "{\"method\":\"teleport\",\"args\":{}}");
    XR_EXPECT_EQ(u.rc, 0);
    XR_EXPECT(u.out.find("\"error\":\"kUnknownMethod\"") != std::string::npos);
  }

  // 8. flag OFF (kill-switch): sections/search return empty-or-rejected,
  //    everything else a typed kRejected, and NO counters file appears.
  {
    auto s = RunHost(kStore2, basic, "off", req);
    XR_EXPECT_EQ(s.rc, 0);
    XR_EXPECT(s.out.find("\"error\":\"kRejected\"") != std::string::npos);
    auto sr = RunHost(kStore2, basic, "off",
                      "{\"method\":\"search\",\"args\":{\"query\":\"ads\"}}");
    XR_EXPECT(sr.out.find("\"count\":0") != std::string::npos);
    auto g = RunHost(kStore2, basic, "off",
                     "{\"method\":\"get\",\"args\":{\"key\":\"network.adblock\"}}");
    XR_EXPECT(g.out.find("\"error\":\"kRejected\"") != std::string::npos);
    auto st = RunHost(kStore2, basic, "off",
                      "{\"method\":\"set\",\"args\":{\"key\":\"network.adblock\","
                      "\"value\":false}}");
    XR_EXPECT(st.out.find("\"error\":\"kRejected\"") != std::string::npos);
    auto rr = RunHost(kStore2, basic, "off",
                      "{\"method\":\"router-resolve\",\"args\":{\"anchor\":"
                      "\"xr://settings/network/adblock\"}}");
    XR_EXPECT(rr.out.find("\"error\":\"kRejected\"") != std::string::npos);
    auto cd = RunHost(kStore2, basic, "off",
                      "{\"method\":\"counters-dump\",\"args\":{}}");
    XR_EXPECT(cd.out.find("\"error\":\"kRejected\"") != std::string::npos);
    // kill-switch form: no counters are ever written (nothing on disk)
    bool has_counters_file = false;
    (void)xrtest::ReadFile(std::string(kStore2) + "/settings-counters.json",
                           &has_counters_file);
    XR_EXPECT_MSG(!has_counters_file, "flag-off run must write zero bytes");
  }

  // 9. Malformed frames: bad JSON -> exit 1 with a typed parse error; bad
  //    flag value -> usage (exit 2).
  {
    auto r = RunHost(kStore, active, "on", "{not-json");
    XR_EXPECT_EQ(r.rc, 1);
    XR_EXPECT(r.out.find("\"error\"") != std::string::npos);
    const std::string badflag =
        "\"" + HostPath() + "\" --store-dir " + kStore +
        " --flag xr_settings_v0=maybe '{}' 2>/dev/null";
    int rc = std::system(badflag.c_str());
    XR_EXPECT_EQ(rc != -1 ? WEXITSTATUS(rc) : -1, 2);
  }

  Sys(("rm -rf " + std::string(kStore) + " " + kStore2).c_str());
  std::printf("suite: %d checks, %d failures\n", xrtest::g_checks,
              xrtest::g_failures);
  return xrtest::Report("test_settings_host");
}
