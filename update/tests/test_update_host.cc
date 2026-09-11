// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the update_host stdio protocol (P10-T1) — methods, canonical
// output bytes, malformed frames, both flag states, the TEST-ONLY banner
// on every verify, and the About state machine incl. the refused terminal
// law (no silent failures).
#include <cstdlib>
#include <string>

#include "env_helper.h"
#include "harness.h"

using namespace xr::update;
using xrtest_update::BaseResponse;
using xrtest_update::Envelope;

namespace {

int Sys(const std::string& cmd) { return std::system(cmd.c_str()); }

std::string HostPath() {
  const char* env = std::getenv("XR_UPDATE_HOST");
  return env != nullptr ? std::string(env) : std::string("build/update_host");
}

const char* kStore = "update-host-test-store";

struct RunResult {
  std::string out;
  int rc = -1;
};

// Runs the host with the request frame on STDIN (never shell-quoted argv:
// envelope bytes contain quotes and the )]}\' prefix — quoting is how the
// first cut of this test produced empty transcripts).
RunResult Run(const std::string& args, const std::string& flags = "") {
  const std::string tmp = "update_host_request.tmp";
  {
    FILE* w = std::fopen(tmp.c_str(), "wb");
    if (!w) return {"", -1};
    std::fwrite(args.data(), 1, args.size(), w);
    std::fclose(w);
  }
  const std::string cmd = "cat " + tmp + " | " + HostPath() + " " + flags +
                          " 2>/dev/null; printf RC=%d $?";
  std::string out;
  FILE* f = popen(cmd.c_str(), "r");
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  int rc = -1;
  const size_t pos = out.rfind("RC=");
  if (pos != std::string::npos) {
    rc = std::atoi(out.c_str() + pos + 3);
    out = out.substr(0, pos);
  }
  pclose(f);
  std::remove(tmp.c_str());
  while (!out.empty() && (out.back() == '\n')) out.pop_back();
  return {out, rc};
}

}  // namespace

int main() {
  Sys(("rm -rf " + std::string(kStore)).c_str());

  // flag-status
  {
    const RunResult r = Run("{\"method\":\"flag-status\"}");
    XR_EXPECT_MSG(r.out == "{\"xr_updater_v0\":\"on\"}" && r.rc == 0,
                  "flag-status on (canonical bytes)");
  }
  {
    const RunResult r = Run("{\"method\":\"flag-status\"}",
                            "--flag xr_updater_v0=off");
    XR_EXPECT_MSG(r.out == "{\"xr_updater_v0\":\"off\"}" && r.rc == 0,
                  "flag-status off");
  }

  // about-states: the full enumerator (the About view's completeness law)
  {
    const RunResult r = Run("{\"method\":\"about-states\"}");
    XR_EXPECT_MSG(r.out ==
                      "{\"states\":[\"idle\",\"checking\",\"available\","
                      "\"downloading\",\"ready\",\"failed\",\"refused\"]}",
                  "about-states canonical list");
  }

  // both flag states for a stateful method: typed refusal, never a silent
  // success and never a crash
  {
    const RunResult off = Run("{\"method\":\"backoff\",\"args\":{\"state\":{},"
                              "\"now_mono\":1}}",
                              "--flag xr_updater_v0=off");
    XR_EXPECT_MSG(off.rc == 0 &&
                      off.out.find("xr_updater_v0 is off") != std::string::npos,
                  "flag-off backoff is a typed refusal");
    const RunResult on = Run("{\"method\":\"backoff\",\"args\":{\"state\":{},"
                             "\"now_mono\":1}}");
    XR_EXPECT_MSG(on.rc == 0 && on.out.find("may_check_now") != std::string::npos,
                  "flag-on backoff answers");
  }

  // verify: accept carries the TEST-ONLY banner (never silent test crypto)
  {
    const std::string env = Envelope(BaseResponse("1.2.0.0"));
    std::string req = "{\"method\":\"verify\",\"args\":{\"channel\":\"dev\","
                      "\"current_version\":\"1.0.0.0\",\"keys\":[{"
                      "\"key_id\":\"xr-root-1\",\"public_key\":\"ROOT-PUB\"}],"
                      "\"envelope\":\"";
    for (char c : env) {
      if (c == '"') req += "\\\"";
      else if (c == '\\') req += "\\\\";
      else req += c;
    }
    req += "\"}}";
    const RunResult r = Run(req);
    XR_EXPECT_MSG(r.rc == 0 &&
                      r.out.find("\"verdict\":\"accept\"") != std::string::npos,
                  "host accepts a valid offer: " + r.out);
    XR_EXPECT_MSG(r.out.find("test-only: XR UPDATE HOST IS A TEST FIXTURE") !=
                      std::string::npos,
                  "the TEST-ONLY banner travels with the verdict");
  }

  // malformed frames: exit 1 + typed error
  {
    const RunResult r = Run("not json");
    XR_EXPECT_MSG(r.rc == 1 &&
                      (r.out.find("kMalformedInput") != std::string::npos ||
                       r.out.find("kUnknownMethod") != std::string::npos),
                  "malformed frame exit 1 typed");
  }
  {
    const RunResult r = Run("{\"method\":\"no-such-method\"}");
    XR_EXPECT_MSG(r.rc == 1 && r.out.find("kUnknownMethod") != std::string::npos,
                  "unknown method exit 1 typed");
  }

  // unknown verify arg (deny-on-unknown at the request layer)
  {
    const RunResult r = Run("{\"method\":\"verify\",\"args\":{\"cookie\":"
                            "\"track-me\"}}");
    XR_EXPECT_MSG(r.rc == 1 &&
                      r.out.find("unknown verify arg") != std::string::npos,
                  "unknown verify arg refused");
  }

  // usage errors: exit 2
  {
    const RunResult r = Run("{\"method\":\"flag-status\"}",
                            "--flag bogus=on");
    XR_EXPECT_MSG(r.rc == 2, "unknown flag exits 2");
  }

  // About state machine: the refused state is TERMINAL (the kill switch);
  // failed always carries the manual-download pointer + check-again
  {
    const RunResult r = Run("{\"method\":\"about-state\",\"args\":{\"state\":"
                            "\"refused\",\"event\":\"check\"}}");
    XR_EXPECT_MSG(r.rc == 0 &&
                      r.out.find("\"state\":\"refused\"") != std::string::npos &&
                      r.out.find("manual_download") != std::string::npos,
                  "refused is terminal with the manual path");
  }
  {
    const RunResult r = Run("{\"method\":\"about-state\",\"args\":{\"state\":"
                            "\"checking\",\"event\":\"error\"}}");
    XR_EXPECT_MSG(r.out.find("\"state\":\"failed\"") != std::string::npos &&
                      r.out.find("manual_download") != std::string::npos &&
                      r.out.find("check_again") != std::string::npos,
                  "failed renders reason + manual path + check again");
  }

  // durable seen via --store-dir: accept then replay-refuse in a second process
  {
    Sys(("mkdir -p " + std::string(kStore)).c_str());
    const std::string env = Envelope(BaseResponse("1.2.0.0"));
    auto esc = [](const std::string& s) {
      std::string r;
      for (char c : s) {
        if (c == '"') r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else r += c;
      }
      return r;
    };
    const std::string req = "{\"method\":\"verify\",\"args\":{\"channel\":"
                            "\"dev\",\"current_version\":\"1.0.0.0\",\"keys\":"
                            "[{\"key_id\":\"xr-root-1\",\"public_key\":\"ROOT-PUB\"}],"
                            "\"envelope\":\"" + esc(env) + "\"}}";
    const RunResult r1 = Run(req, "--store-dir " + std::string(kStore));
    XR_EXPECT_MSG(r1.rc == 0 &&
                      r1.out.find("\"verdict\":\"accept\"") != std::string::npos,
                  "first process accepts (durable store)");
    XR_EXPECT_MSG(Sys(("test -f " + std::string(kStore) +
                       "/update-seen.json").c_str()) == 0,
                  "seen-set persisted");
    const RunResult r2 = Run(req, "--store-dir " + std::string(kStore));
    XR_EXPECT_MSG(r2.rc == 0 && r2.out.find("\"reason\":\"replay-refused\"") !=
                                     std::string::npos,
                  "second process refuses the replay (cross-process replay "
                  "protection)");
    Sys(("rm -rf " + std::string(kStore)).c_str());
  }

  return xrtest::Report("test_update_host");
}
