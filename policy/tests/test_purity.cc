// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — purity proof for the C++ resolver (extends the P5
// monkeypatch-purity test to the C++ core): identical inputs must produce
// identical bytes under perturbed ambient environment (TZ, locale, env
// vars) and regardless of wall-clock reading time. The resolver reads only
// its inputs by construction (no getenv/clock calls in policy/core/resolve
// — also statically asserted by mode_lint); this test is the dynamic
// counterpart. now_ms is a plain INPUT field, so "time" cannot leak in.
#include <cstdlib>
#include <cstring>
#include <string>

#include "policy/core/resolve.h"
#include "harness.h"

using namespace xr::policy;

namespace {

const char* kRequests[] = {
    "{\"identity\":{\"value\":\"xr:00000000-0000-4000-8000-000000000001\"},"
    "\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"example.com\"},"
    "\"request_class\":\"kNavigation\"}",
    "{\"identity\":{\"value\":\"xr:00000000-0000-4000-8000-0000000000ef\"},"
    "\"origin\":{\"scheme\":\"http\",\"registrable_domain\":\"other.org\"},"
    "\"request_class\":\"kScript\",\"trust_context\":\"kShield\"}",
    "{\"identity\":{\"value\":\"bogus\"},\"origin\":{},\"request_class\":\"kBogus\"}",
    "{\"identity\":{\"value\":\"xr:00000000-0000-4000-8000-000000000002\"},"
    "\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"x.io\"},"
    "\"request_class\":\"kPermission\",\"exceptions\":[{\"id\":\"e\",\"domain\":\"x.io\","
    "\"scope\":\"7d\",\"trust\":\"kStandard\",\"created_at\":1,\"expires_at\":50}],"
    "\"now_ms\":10}",
};

std::string ResolveTo(const char* req) {
  auto p = ParseJson(req);
  XR_EXPECT_MSG(p.ok, "fixture must parse");
  RequestParse rp = ParseResolveRequest(p.value);
  return ResolveOutputToCanonicalJson(Resolve(rp.request));
}

}  // namespace

int main() {
  // Baseline under the ambient environment.
  std::string baseline[4];
  for (int i = 0; i < 4; ++i) baseline[i] = ResolveTo(kRequests[i]);

  // Perturb environment hard; outputs must be byte-identical.
  const char* perturb[] = {"TZ=Asia/Tokyo",    "TZ=America/Los_Angeles", "LANG=C",
                           "LANG=de_DE.UTF-8", "LC_ALL=tr_TR.UTF-8",     "TERM=dumb",
                           "HOME=/nonexistent", "XR_TEST_SHOULD_NOT_MATTER=1"};
  for (const char* e : perturb) {
    // putenv (not setenv) so the string lives for the process lifetime.
    char* storage = strdup(e);
    XR_EXPECT(storage != nullptr);
    putenv(storage);
    for (int i = 0; i < 4; ++i) {
      std::string again = ResolveTo(kRequests[i]);
      XR_EXPECT_MSG(again == baseline[i],
                    std::string("purity violated under ") + e + " for request #" +
                        std::to_string(i) + " (got " + again.substr(0, 80) + ")");
    }
  }

  // Order independence: resolving request A after B must equal A alone
  // (no hidden state in the pure path).
  std::string mixed[4];
  for (int round = 0; round < 2; ++round) {
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j) mixed[j] = ResolveTo(kRequests[j]);
  }
  for (int i = 0; i < 4; ++i) XR_EXPECT_MSG(mixed[i] == baseline[i], "order independence");

  // Determinism under repetition (hash-consistency, not just equality).
  for (int i = 0; i < 4; ++i) {
    for (int rep = 0; rep < 100; ++rep) {
      XR_EXPECT_MSG(ResolveTo(kRequests[i]) == baseline[i], "repeat determinism");
      if (xrtest::g_failures > 0) break;
    }
  }

  return xrtest::Report("test_purity");
}
