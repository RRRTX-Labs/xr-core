// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: golden vectors (docs/contracts/vectors/update-v1.json, >=60
// cases) driven through the update_host BINARY. Every case's expected
// verdict/reason (or typed error) must match; the byte-parity against the
// Python fake is the xr-browser harness's job (tools/
// update_vectors_check.py) — this suite proves the compiled core agrees
// with the committed vectors.
#include <cstdlib>
#include <string>

#include "update/core/json.h"

#include "harness.h"

namespace {

std::string HostPath() {
  const char* env = std::getenv("XR_UPDATE_HOST");
  return env != nullptr ? std::string(env) : std::string("build/update_host");
}

std::string ReadFile(const std::string& path, bool* ok) {
  *ok = false;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return {};
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  std::fclose(f);
  *ok = true;
  return data;
}

struct RunResult {
  std::string out;
  int rc = -1;
};

// stdin-piped (never shell-quoted argv — see test_update_host.cc).
RunResult Run(const std::string& args) {
  const std::string tmp = "update_host_request.tmp";
  {
    FILE* w = std::fopen(tmp.c_str(), "wb");
    if (!w) return {"", -1};
    std::fwrite(args.data(), 1, args.size(), w);
    std::fclose(w);
  }
  const std::string cmd =
      "cat " + tmp + " | " + HostPath() + " 2>/dev/null; printf RC=%d $?";
  std::string out;
  FILE* f = popen(cmd.c_str(), "r");
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

// crude single-request JSON frame builder (args are pre-canonical bytes)
std::string Frame(const std::string& method, const std::string& args) {
  return "{\"args\":" + args + ",\"method\":\"" + method + "\"}";
}

}  // namespace

int main() {
  const char* root_env = std::getenv("XR_BROWSER_ROOT");
  const std::string root = root_env ? std::string(root_env) : "../../../xr-browser";
  const std::string vec_path = root + "/docs/contracts/vectors/update-v1.json";
  bool ok = false;
  const std::string text = ReadFile(vec_path, &ok);
  XR_EXPECT_MSG(ok, "vectors file readable at " + vec_path);
  if (!ok) return xrtest::Report("test_golden_vectors");

  auto pr = xr::update::ParseJson(text);
  XR_EXPECT_MSG(pr.ok, "vectors parse");
  if (!pr.ok) return xrtest::Report("test_golden_vectors");
  const auto* cases = pr.value.find("cases");
  XR_EXPECT_MSG(cases && cases->is_array() && cases->as_array().size() >= 60,
                "at least 60 golden-vector cases");
  if (!cases) return xrtest::Report("test_golden_vectors");

  size_t ran = 0;
  for (const auto& c : cases->as_array()) {
    const std::string cid = c.find("id")->as_string();
    const std::string method = c.find("method")->as_string();
    const std::string args = c.find("args")->Canonical();
    const auto& expect = *c.find("expect");
    const RunResult r = Run(Frame(method, args));
    ++ran;
    if (expect.find("error") != nullptr) {
      const std::string want_err = expect.find("error")->as_string();
      const bool rc_ok = expect.find("error")->as_string() == "kMalformedInput"
                             ? r.rc == 1
                             : r.rc == 0;
      XR_EXPECT_MSG(rc_ok && r.out.find(want_err) != std::string::npos,
                    "vector " + cid + ": expected " + want_err + ", got " +
                        r.out);
      continue;
    }
    // Full-object expectations (state machines, epoch-apply, cohort,
    // backoff) compare CANONICAL bytes; verify results are field-matched
    // (the host adds seen_size/manifest_id the vectors do not pin).
    const bool full_object = expect.find("verdict") == nullptr &&
                             expect.find("error") == nullptr;
    if (full_object) {
      XR_EXPECT_MSG(r.out == expect.Canonical(),
                    "vector " + cid + " canonical bytes:\n  want " +
                        expect.Canonical() + "\n  got  " + r.out);
      continue;
    }
    const auto* want_state = expect.find("state");
    if (want_state != nullptr) {
      XR_EXPECT_MSG(r.out == expect.Canonical(),
                    "vector " + cid + " canonical bytes:\n  want " +
                        expect.Canonical() + "\n  got  " + r.out);
      continue;
    }
    const std::string want_verdict =
        expect.find("verdict") ? expect.find("verdict")->as_string() : "";
    const std::string want_reason =
        expect.find("reason") ? expect.find("reason")->as_string() : "";
    const bool want_manual = expect.find("manual_path") &&
                             expect.find("manual_path")->as_bool();
    XR_EXPECT_MSG(r.rc == 0, "vector " + cid + ": exit 0");
    if (!want_verdict.empty()) {
      XR_EXPECT_MSG(r.out.find("\"verdict\":\"" + want_verdict + "\"") !=
                        std::string::npos,
                    "vector " + cid + ": verdict " + want_verdict + " in " +
                        r.out);
    }
    if (!want_reason.empty()) {
      XR_EXPECT_MSG(r.out.find("\"reason\":\"" + want_reason + "\"") !=
                        std::string::npos,
                    "vector " + cid + ": reason " + want_reason + " in " +
                        r.out);
    }
    XR_EXPECT_MSG((r.out.find("\"manual_path\":true") != std::string::npos) ==
                      want_manual,
                  "vector " + cid + ": manual_path flag");
  }
  XR_EXPECT_MSG(ran == cases->as_array().size(),
                "every vector case ran (no silent skips)");

  return xrtest::Report("test_golden_vectors");
}
