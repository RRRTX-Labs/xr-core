// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: golden vectors (docs/contracts/vectors/shield-v1.json, >=150
// cases) driven through the shield_host BINARY. Every case's expected
// output bytes (or typed error/rejection + exit code) must match exactly;
// the byte-parity against the Python fake (fakes/shield.py) is the
// xr-browser harness's job (tools/vectors_check.py) — this suite proves
// the compiled core agrees with the committed vectors. Case shape:
//   {id, method, args, expect, flags?, exit?}
// `expect` is either {"error":…} (rc implied: kMalformedInput/kUnknownMethod
// => 1, everything else => 0, overridable via "exit") or the FULL canonical
// output object compared byte-for-byte.
#include <cstdlib>
#include <cstdio>
#include <string>

#include "common/core/json.h"

#include "harness.h"

namespace {

std::string HostPath() {
  const char* env = std::getenv("XR_SHIELD_HOST");
  return env != nullptr ? std::string(env) : std::string("build/shield_host");
}

struct RunResult {
  std::string out;
  int rc = -1;
};

// stdin-piped (never shell-quoted argv — see test_shield_host.cc)
RunResult Run(const std::string& frame, const std::string& flags) {
  const std::string tmp = "shield_vectors_request.tmp";
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

}  // namespace

int main() {
  const char* root_env = std::getenv("XR_BROWSER_ROOT");
  const std::string root =
      root_env ? std::string(root_env) : "../../../xr-browser";
  const std::string vec_path = root + "/docs/contracts/vectors/shield-v1.json";
  bool ok = false;
  const std::string text = xrtest::ReadFile(vec_path, &ok);
  XR_EXPECT_MSG(ok, "vectors file readable at " + vec_path);
  if (!ok) return xrtest::Report("test_golden_vectors");

  auto pr = xr::common::ParseJson(text);
  XR_EXPECT_MSG(pr.ok, "vectors parse");
  if (!pr.ok) return xrtest::Report("test_golden_vectors");
  const auto* cases = pr.value.find("cases");
  XR_EXPECT_MSG(cases && cases->is_array() && cases->as_array().size() >= 150,
                "at least 150 golden-vector cases");
  if (!cases) return xrtest::Report("test_golden_vectors");

  size_t ran = 0;
  for (const auto& c : cases->as_array()) {
    const std::string cid = c.find("id")->as_string();
    std::string frame;
    if (const auto* raw = c.find("raw"); raw != nullptr) {
      frame = raw->as_string();  // protocol-level frame, verbatim
    } else {
      frame = Frame(c.find("method")->as_string(),
                    c.find("args")->Canonical());
    }
    std::string flags;
    if (const auto* fl = c.find("flags"); fl != nullptr)
      flags = fl->as_string();
    const auto& expect = *c.find("expect");
    const RunResult r = Run(frame, flags);
    ++ran;
    int want_rc = 0;
    if (const auto* ex = c.find("exit"); ex != nullptr) want_rc = ex->as_int();
    if (const auto* errv = expect.find("error"); errv != nullptr) {
      const std::string want_err = errv->as_string();
      if (c.find("exit") == nullptr) {
        want_rc = (want_err == "kMalformedInput" || want_err == "kUnknownMethod")
                      ? 1
                      : 0;
      }
      XR_EXPECT_MSG(r.rc == want_rc && r.out.find(want_err) != std::string::npos,
                    "vector " + cid + ": expected " + want_err + " rc=" +
                        std::to_string(want_rc) + ", got rc=" +
                        std::to_string(r.rc) + " " + r.out);
      continue;
    }
    XR_EXPECT_MSG(r.rc == want_rc,
                  "vector " + cid + ": exit " + std::to_string(want_rc) +
                      ", got " + std::to_string(r.rc));
    XR_EXPECT_MSG(r.out == expect.Canonical(),
                  "vector " + cid + " canonical bytes:\n  want " +
                      expect.Canonical() + "\n  got  " + r.out);
  }
  XR_EXPECT_MSG(ran == cases->as_array().size(),
                "every vector case ran (no silent skips)");

  return xrtest::Report("test_golden_vectors");
}
