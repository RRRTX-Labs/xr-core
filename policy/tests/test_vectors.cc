// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — the P6 centerpiece gate: every golden vector
// (policy-resolver-v1, 66; route-manager-v1 is P16 surface, driven via the
// fake elsewhere) must resolve in the C++ core to the BYTE-IDENTICAL
// canonical JSON the frozen Python fake produces (vectors_check.py parity,
// now cross-language). Also drives the same vectors through the stdio
// façade binary shape in test_host.cc.
#include <cstdlib>
#include <string>

#include "policy/core/json.h"
#include "policy/core/resolve.h"
#include "policy/core/sha256.h"
#include "harness.h"

namespace {

std::string VectorsPath(const char* argv0) {
  (void)argv0;
  const char* env = std::getenv("XR_BROWSER_ROOT");
  if (env != nullptr && *env) return std::string(env) + "/docs/contracts/vectors/policy-resolver-v1.json";
  return "../../../xr-browser/docs/contracts/vectors/policy-resolver-v1.json";
}

}  // namespace

int main(int, char** argv) {
  // SHA-256 FIPS 180-4 test vectors (sanity for the snapshot integrity base).
  XR_EXPECT_STREQ(xr::policy::Sha256Hex(""),
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  XR_EXPECT_STREQ(xr::policy::Sha256Hex("abc"),
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  XR_EXPECT_STREQ(xr::policy::Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  bool ok = false;
  std::string data = xrtest::ReadFile(VectorsPath(argv[0]), &ok);
  if (!XR_EXPECT_MSG(ok, "vectors file readable")) return xrtest::Report("test_vectors");

  auto parsed = xr::policy::ParseJson(data);
  if (!XR_EXPECT_MSG(parsed.ok, "vectors JSON parses")) return xrtest::Report("test_vectors");

  const xr::policy::JsonValue* vectors = parsed.value.find("vectors");
  if (!XR_EXPECT_MSG(vectors != nullptr && vectors->is_array(), "vectors array present"))
    return xrtest::Report("test_vectors");

  int n = 0;
  for (const auto& v : vectors->as_array()) {
    const xr::policy::JsonValue* name = v.find("name");
    const xr::policy::JsonValue* request = v.find("request");
    const xr::policy::JsonValue* expected = v.find("expected");
    if (!XR_EXPECT_MSG(name != nullptr && name->is_string(), "vector has name")) continue;
    if (!XR_EXPECT_MSG(request != nullptr, (name->as_string() + ": has request").c_str())) continue;
    if (!XR_EXPECT_MSG(expected != nullptr, (name->as_string() + ": has expected").c_str())) continue;

    auto rp = xr::policy::ParseResolveRequest(*request);
    xr::policy::ResolveOutput out = xr::policy::Resolve(rp.request);
    std::string got = xr::policy::ResolveOutputToCanonicalJson(out);
    std::string want = expected->Canonical();
    XR_EXPECT_MSG(got == want, name->as_string() + ": byte-parity (got " + got.substr(0, 120) +
                                   " want " + want.substr(0, 120) + ")");
    ++n;
  }
  XR_EXPECT_EQ(n, 66);
  return xrtest::Report("test_vectors");
}
