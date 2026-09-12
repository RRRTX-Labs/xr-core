// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the shield_host stdio protocol (P11-T2) — both vocabularies:
// the FROZEN mojom surface (Status/RecentEvents, {"ok"}/{"error"} envelope,
// byte-identical to the frozen fakes/shield.py fixture case) and the LIVING
// host surface (bare canonical results, {"error","detail"} exit 1,
// {"error":"kRejected","reason"} exit 0, usage exit 2). Requests go over
// STDIN — never shell-quoted argv (the update/tests lesson).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "common/core/sha256.h"

#include "harness.h"

namespace {

struct RunResult {
  std::string out;
  int rc = -1;
};

std::string HostPath() {
  const char* env = std::getenv("XR_SHIELD_HOST");
  return env != nullptr ? std::string(env) : std::string("build/shield_host");
}

RunResult Run(const std::string& frame, const std::string& flags = "") {
  const std::string tmp = "shield_host_request.tmp";
  {
    std::FILE* w = std::fopen(tmp.c_str(), "wb");
    if (!w) return {"", -1};
    std::fwrite(frame.data(), 1, frame.size(), w);
    std::fclose(w);
  }
  const std::string cmd = "cat " + tmp + " | " + HostPath() + " " + flags +
                          " 2>/dev/null; printf RC=%d $?";
  std::string out;
  std::FILE* f = popen(cmd.c_str(), "r");
  char buf[65536];
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
  while (!out.empty() && out.back() == '\n') out.pop_back();
  return {out, rc};
}

std::string Frame(const std::string& method, const std::string& args) {
  return "{\"args\":" + args + ",\"method\":\"" + method + "\"}";
}

void ExpectRun(const std::string& method, const std::string& args,
               const std::string& want_out, int want_rc, const char* what,
               const std::string& flags = "") {
  const RunResult r = Run(Frame(method, args), flags);
  XR_EXPECT_MSG(r.out == want_out && r.rc == want_rc,
                std::string(what) + "\n  want(rc=" + std::to_string(want_rc) +
                    "): " + want_out + "\n  got (rc=" + std::to_string(r.rc) +
                    "): " + r.out);
}

const char* kBundle =
    R"({"schema":"xr-list-bundle","schema_version":1,"name":"xr-default",)"
    R"("bundle_version":3,"lists":[{"name":"l1","attribution":"CC-BY-3.0",)"
    R"("rules":[{"id":"r1","kind":"network","filter":"||tracker.example^",)"
    R"("action":"block"}]}],"refusals":[]})";

}  // namespace

int main() {
  // ---- frozen mojom surface: the fixture case, byte-identical ----
  ExpectRun("Status",
            R"({"identity":{"value":"xr:...-001"},"origin":{"scheme":"https",)"
            R"("registrable_domain":"example.com"}})",
            R"({"ok":{"blocked_count":0,"enabled":true}})", 0,
            "frozen fixture case (Status)");
  ExpectRun("Status",
            R"({"identity":{"value":"xr:a"},"origin":{"scheme":"https",)"
            R"("registrable_domain":"e.com"}})",
            R"({"ok":{"blocked_count":0,"enabled":false}})", 0,
            "Status flag off", "--flag xr_shield_v1=off");
  ExpectRun("Status", R"({"identity":{"value":""},"origin":{"scheme":"https"}})",
            R"({"error":"kUnknownIdentity"})", 0, "Status unknown identity");
  ExpectRun("Status", R"({"identity":{"value":"x"},"origin":{"scheme":"ftp"}})",
            R"({"error":"kUnknownOrigin"})", 0, "Status unknown origin");
  ExpectRun("Status", R"({"identity":{"value":"x"},"origin":{"scheme":"https"},)"
                      R"("ring":[{"ts_millis":1,"identity":{"value":"x"},)"
                      R"("tab_id":1,"origin":{"scheme":"https",)"
                      R"("registrable_domain":"t.example"},"target":"t",)"
                      R"("rule":"r","list_provenance":"l","action":"kBlocked",)"
                      R"("request_class":"kScript"}]})",
            R"({"ok":{"blocked_count":1,"enabled":true}})", 0,
            "Status ring count (living helper key)");
  ExpectRun("RecentEvents", R"({"identity":{"value":"xr:...-001"},)"
                            R"("max_events":5})",
            R"({"ok":[{"action":"kBlocked","identity":{"value":"xr:...-001"},)"
            R"("list_provenance":"xr-default-list-v1","origin":)"
            R"({"registrable_domain":"tracker.example","scheme":"https"},)"
            R"("request_class":"kScript","rule":"||tracker.example^",)"
            R"("tab_id":1,"target":"https://tracker.example/a.js",)"
            R"("ts_millis":1000}]})",
            0, "RecentEvents frozen sample (byte-identical to fake)");
  ExpectRun("RecentEvents", R"({"identity":{"value":"xr:a"},"max_events":0})",
            R"({"ok":[]})", 0, "RecentEvents max 0");
  ExpectRun("RecentEvents", R"({"identity":{"value":"xr:a"},"max_events":"x"})",
            R"({"error":"kMalformedInput"})", 0, "RecentEvents bad max_events");

  // ---- living surface ----
  ExpectRun("flag-status", "{}", R"({"xr_shield_v1":"on"})", 0, "flag on");
  ExpectRun("flag-status", "{}", R"({"xr_shield_v1":"off"})", 0, "flag off",
            "--flag xr_shield_v1=off");
  ExpectRun("posture", "{}",
            R"({"chip":"green","mode":"normal","reason":"normal"})", 0,
            "posture default");
  ExpectRun("posture", R"({"route_bound":false,"kill_switch_on":true})",
            R"({"chip":"red","mode":"fail-closed","reason":"route-loss"})", 0,
            "posture route law absolute");
  ExpectRun("posture", R"({"engine_alive":"no"})",
            R"({"detail":"field-not-bool:engine_alive","error":"kMalformedInput"})",
            1, "posture typed error");
  ExpectRun("posture", R"({"switch":true})",
            R"({"detail":"unknown-field:switch","error":"kMalformedInput"})", 1,
            "posture unknown field");

  // match: the decision pipeline over the wire
  const std::string ctx =
      R"({"identity":{"value":"xr:a"},"origin":{"scheme":"https",)"
      R"("registrable_domain":"tracker.example"},)"
      R"("url":"https://tracker.example/a.js","request_class":"kScript"})";
  ExpectRun("match", "{\"context\":" + ctx + ",\"bundle\":" + kBundle + "}",
            R"({"fail_closed":false,"posture":{"chip":"green","mode":"normal",)"
            R"("reason":"normal"},"verdict":{"action":"block",)"
            R"("bundle_version":3,"engine_decision":true,"list_id":"l1",)"
            R"("rule_id":"r1","why_code":"rule-blocked"}})",
            0, "match blocked");
  ExpectRun("match", "{\"context\":" + ctx + "}",
            R"({"fail_closed":false,"posture":{"chip":"green","mode":"normal",)"
            R"("reason":"normal"},"verdict":{"action":"allow",)"
            R"("bundle_version":0,"engine_decision":false,"list_id":"",)"
            R"("rule_id":"","why_code":"no-bundle"}})",
            0, "match no bundle");
  ExpectRun("match",
            "{\"context\":" + ctx + ",\"bundle\":" + kBundle +
                ",\"route_bound\":false}",
            R"({"fail_closed":true,"posture":{"chip":"red","mode":"fail-closed",)"
            R"("reason":"route-loss"},"verdict":{"action":"block",)"
            R"("bundle_version":3,"engine_decision":false,"list_id":"",)"
            R"("rule_id":"","why_code":"route-loss-fail-closed"}})",
            0, "match route loss fail-closed");
  ExpectRun("match", "{}",
            R"({"detail":"missing-context","error":"kMalformedInput"})", 1,
            "match missing context");
  ExpectRun("match",
            "{\"context\":" + ctx + ",\"scopes\":[{\"scope_id\":\"s\"}]}",
            R"({"detail":"missing-reason:s","error":"kMalformedInput"})", 1,
            "match scope without reason");

  // bundle-load / bundle-check
  {
    // the digest is DEFINED as sha256 over the canonical per-list bytes —
    // computed here independently of the host (the vectors pin the value)
    const std::string list_bytes =
        R"({"attribution":"CC-BY-3.0","name":"l1","rules":[{"action":"block",)"
        R"("filter":"||tracker.example^","id":"r1","kind":"network"}]})";
    const std::string digest = xr::common::Sha256Hex("[" + list_bytes + "]");
    ExpectRun("bundle-load", std::string("{\"bundle\":") + kBundle + "}",
              R"({"bundle_version":3,"digest":")" + digest +
                  R"(","lists":[{"name":"l1","rules":1}],"name":"xr-default",)"
                  R"("refusals":[]})",
              0, "bundle-load summary");
  }
  ExpectRun("bundle-load", R"({"bundle":{"schema":"wrong"}})",
            R"({"detail":"wrong-schema","error":"kMalformedInput"})", 1,
            "bundle-load malformed");
  // bundle-check: the empty-rules list canonical bytes are
  // {"attribution":"","name":"l","rules":[]} (sorted, compact — the shared
  // serializer); the manifest entry sha256 covers exactly those bytes.
  {
    const std::string empty_bundle =
        R"({"bundle":{"schema":"xr-list-bundle","schema_version":1,)"
        R"("name":"n","bundle_version":1,"lists":[{"name":"l",)"
        R"("attribution":"","rules":[]}],"refusals":[]},)";
    const std::string good_sha = xr::common::Sha256Hex(
        R"({"attribution":"","name":"l","rules":[]})");
    ExpectRun("bundle-check",
              empty_bundle + R"("manifest":{"lists":[{"name":"l","sha256":")" +
                  good_sha + R"(","rules":0}]}})",
              R"({"bound":true,"bundle_version":1,"digest":")" +
                  xr::common::Sha256Hex(std::string("[") +
                                        R"({"attribution":"","name":"l","rules":[]})" +
                                        "]") +
                  R"(","list_count":1})",
              0, "bundle-check bound");
    ExpectRun("bundle-check",
              empty_bundle + R"("manifest":{"lists":[{"name":"l","sha256":")" +
                  std::string(64, '0') + R"(","rules":0}]}})",
              R"({"error":"kRejected","reason":"manifest-sha256:l"})", 0,
              "bundle-check tamper sha (rejection, exit 0)");
    ExpectRun("bundle-check",
              empty_bundle + R"("manifest":{"lists":[{"name":"l","sha256":")" +
                  good_sha + R"(","rules":9}]}})",
              R"({"error":"kRejected","reason":"manifest-rule-count:l"})", 0,
              "bundle-check rule count mismatch");
    ExpectRun("bundle-check", empty_bundle + R"("manifest":{"lists":[]}})",
              R"({"error":"kRejected","reason":"manifest-list-count"})", 0,
              "bundle-check list count mismatch");
  }

  // apply lifecycle over the wire
  const std::string s0 =
      R"({"active":{"present":false},"lkg":{"present":false},"pins":[],)"
      R"("last_apply_mono":-1})";
  {
    const RunResult r = Run(Frame(
        "apply", "{\"bundle\":" + std::string(kBundle) + ",\"state\":" + s0 +
                 ",\"now_mono\":100}"));
    XR_EXPECT_MSG(r.rc == 0 && r.out.find("\"version\":3") != std::string::npos &&
                      r.out.find("\"pins\":[{") != std::string::npos,
                  "apply fresh: " + r.out);
    const std::string s1 = [&] {
      const size_t p = r.out.find("{\"state\":");
      return r.out.substr(p + 9, r.out.size() - p - 10);
    }();
    const RunResult d = Run(Frame(
        "apply", "{\"bundle\":" + std::string(kBundle) + ",\"state\":" + s1 +
                 ",\"now_mono\":200}"));
    XR_EXPECT_MSG(d.rc == 0 &&
                      d.out == R"({"error":"kRejected","reason":)"
                               R"("bundle-version-equal-reoffer"})",
                  "apply equal reoffer: " + d.out);
    const RunResult h = Run(Frame(
        "apply",
        "{\"bundle\":" + std::string(kBundle) + ",\"state\":" +
            R"({"active":{"present":true,"bundle_id":"xr-default","digest":")" +
            std::string(64, 'a') + R"(","version":3},"lkg":{"present":false},)"
            R"("pins":[],"last_apply_mono":100})" +
            ",\"now_mono\":300}"));
    XR_EXPECT_MSG(h.rc == 0 &&
                      h.out == R"({"error":"kRejected","reason":)"
                               R"("pins-missing-active"})",
                  "apply hot-pin-out: " + h.out);
  }
  ExpectRun("apply", "{\"bundle\":" + std::string(kBundle) + ",\"state\":" + s0 +
                         "}",
            R"({"detail":"missing-now-mono","error":"kMalformedInput"})", 1,
            "apply requires now_mono");

  // protocol errors + usage (exit codes)
  ExpectRun("bogus", "{}", R"({"detail":"bogus","error":"kUnknownMethod"})", 1,
            "unknown method");
  {
    const RunResult r = Run("not json at all");
    XR_EXPECT_MSG(r.rc == 1 && r.out.find("kMalformedInput") != std::string::npos,
                  "malformed stdin frame");
  }
  {
    // argv shapes: method + args positional, and single-JSON frame
    const std::string cmd1 = HostPath() + " flag-status 2>/dev/null";
    XR_EXPECT_MSG(std::system((cmd1 + " | grep -q '\"xr_shield_v1\":\"on\"'")
                              .c_str()) == 0,
                  "argv method form");
    const std::string tmp = "shield_frame.tmp";
    {
      std::FILE* w = std::fopen(tmp.c_str(), "wb");
      const std::string fr = Frame("flag-status", "{}");
      std::fwrite(fr.data(), 1, fr.size(), w);
      std::fclose(w);
    }
    XR_EXPECT_MSG(std::system((HostPath() + " \"$(cat " + tmp + ")\" | grep -q on")
                              .c_str()) == 0,
                  "argv single-frame form");
    std::remove(tmp.c_str());
    XR_EXPECT_MSG(std::system((HostPath() + " --store-dir /tmp 2>/dev/null")
                              .c_str()) != 0,
                  "--store-dir is not a v1 option (exit 2)");
    XR_EXPECT_MSG(std::system((HostPath() + " --flag xr_shield_v1=maybe "
                               "flag-status 2>/dev/null").c_str()) != 0,
                  "bad flag value (exit 2)");
    XR_EXPECT_MSG(std::system((HostPath() + " --flag bogus=on flag-status "
                               "2>/dev/null").c_str()) != 0,
                  "unknown flag name (exit 2)");
  }
  {  // P11-T5: event-emit — the living block-event-v1 row, byte-checked
     // by the vectors; tokens and redaction pinned here too.
    const std::string ctx =
        R"({"identity":{"value":"xr:a"},"origin":{"scheme":"https",)"
        R"("registrable_domain":"tracker.example"},)"
        R"("url":"https://tracker.example/ad.js?q=secret#frag",)"
        R"("request_class":"kScript"})";
    RunResult r = Run(Frame("event-emit",
        R"({"action":"kBlocked","bundle_version":2,"context":)" + ctx +
        R"(,"list_id":"l-1","rule":"||tracker.example^","rule_id":"r-1",)"
        R"("seq":7,"tab_id":3,"ts_millis":1234,"why_code":"rule-blocked"})"));
    XR_EXPECT(r.rc == 0);
    XR_EXPECT(r.out.find(R"("target":"https://tracker.example/ad.js")") !=
              std::string::npos);
    XR_EXPECT(r.out.find("secret") == std::string::npos);
    XR_EXPECT(r.out.find(R"("seq":7)") != std::string::npos);
    XR_EXPECT(r.out.find(R"("contract_version":1)") != std::string::npos);
    // defaults: optional provenance omitted -> empty strings, zero ints
    r = Run(Frame("event-emit", R"({"action":"kAllowed","context":)" + ctx +
            R"(,"seq":0,"ts_millis":0,"why_code":"rule-allowed"})"));
    XR_EXPECT(r.rc == 0 && r.out.find(R"("rule_id":"")") != std::string::npos);
    XR_EXPECT(r.out.find(R"("tab_id":0)") != std::string::npos &&
              r.out.find(R"("bundle_version":0)") != std::string::npos);
    // closed vocabularies + required-arg tokens (kMalformedInput, exit 1)
    r = Run(Frame("event-emit", R"({"action":"blocked","context":)" + ctx +
            R"(,"seq":1,"ts_millis":1,"why_code":"no-match"})"));
    XR_EXPECT(r.rc == 1 && r.out.find("bad-action:blocked") !=
              std::string::npos);
    r = Run(Frame("event-emit", R"({"action":"kBlocked","context":)" + ctx +
            R"(,"seq":1,"ts_millis":1,"why_code":"because"})"));
    XR_EXPECT(r.rc == 1 && r.out.find("bad-why-code:because") !=
              std::string::npos);
    r = Run(Frame("event-emit", R"({"action":"kBlocked","context":)" + ctx +
            R"(,"seq":-1,"ts_millis":1,"why_code":"no-match"})"));
    XR_EXPECT(r.rc == 1 && r.out.find("missing-seq") != std::string::npos);
    r = Run(Frame("event-emit", R"({"action":"kBlocked","context":)" + ctx +
            R"(,"seq":1,"why_code":"no-match"})"));
    XR_EXPECT(r.rc == 1 && r.out.find("missing-ts-millis") !=
              std::string::npos);
    r = Run(Frame("event-emit", R"({"context":)" + ctx +
            R"(,"seq":1,"ts_millis":1})"));
    XR_EXPECT(r.rc == 1 && r.out.find("missing-action") != std::string::npos);
    r = Run(Frame("event-emit",
            R"({"action":"kBlocked","seq":1,"ts_millis":1,)"
            R"("why_code":"no-match"})"));
    XR_EXPECT(r.rc == 1 && r.out.find("missing-context") != std::string::npos);
    r = Run(Frame("event-emit", R"({"action":"kBlocked","context":)" + ctx +
            R"(,"rule":7,"seq":1,"ts_millis":1,"why_code":"no-match"})"));
    XR_EXPECT(r.rc == 1 && r.out.find("field-not-string:rule") !=
              std::string::npos);
    r = Run(Frame("event-emit", R"({"action":"kBlocked","context":)" + ctx +
            R"(,"extra":1,"seq":1,"ts_millis":1,"why_code":"no-match"})"));
    XR_EXPECT(r.rc == 1 && r.out.find("unknown-field:extra") !=
              std::string::npos);
  }
  // ---- P11-T6: the dev-only debug page — startup channel gate + union ----
  {
    // A bad channel value is usage (exit 2; vectors cannot carry that).
    RunResult r = Run(Frame("page-states", "{}"), "--build-channel bogus");
    XR_EXPECT(r.rc == 2);
    // The DEFAULT channel (release) refuses the page typed, exit 0 — the
    // gate fails CLOSED without the option.
    r = Run(Frame("debug-page", "{}"));
    XR_EXPECT(r.rc == 0 &&
              r.out.find("build-channel-not-dev:release") !=
                  std::string::npos);
    // nightly-test is not dev either (the brief: dev builds only).
    r = Run(Frame("debug-page", "{}"), "--build-channel nightly-test");
    XR_EXPECT(r.rc == 0 &&
              r.out.find("build-channel-not-dev:nightly-test") !=
                  std::string::npos);
    // page-states serves the union (NOT channel-gated — the state
    // vocabulary is public; the page bytes are not).
    r = Run(Frame("page-states", "{}"));
    XR_EXPECT(r.rc == 0 &&
              r.out == R"({"states":["normal","engine-dead","engine-poisoned","kill-switch","route-loss"]})");
    // dev opens the page: the enterprise force-disable WINS over the
    // caller's kill_switch_on=false and the reason rides VERBATIM.
    r = Run(Frame("debug-page",
                  R"({"enterprise":{"force_disabled":true,"reason":"IT 4711"},)"
                  R"("kill_switch_on":false})"),
            "--build-channel dev");
    XR_EXPECT(r.rc == 0 &&
              r.out.find(R"("page_state":"kill-switch")") !=
                  std::string::npos &&
              r.out.find(R"("reason":"IT 4711")") != std::string::npos &&
              r.out.find(R"("chip":"amber")") != std::string::npos);
    // A force-disable WITHOUT a reason is a silent suppression: malformed.
    r = Run(Frame("debug-page", R"({"enterprise":{"force_disabled":true}})"),
            "--build-channel dev");
    XR_EXPECT(r.rc == 1 &&
              r.out.find("missing-enterprise-reason") != std::string::npos);
  }
  return xrtest::Report("test_shield_host");
}
