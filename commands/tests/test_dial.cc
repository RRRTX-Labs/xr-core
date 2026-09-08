// Copyright 2026 RRRTX Labs
// test_dial.cc — "dial steps provably toggle P6 policy state end-to-end through
// the host." Drives the REAL commands_host binary (subprocess) to invoke the
// dial commands, then loads the resulting trust_bindings.json with the P6
// policy store (PolicyStore) — the P6 authority — to prove the host wrote
// state the P6 store validates and the trust tier actually toggled.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "commands/tests/harness.h"

#include "policy/core/json.h"
#include "policy/core/resolve.h"
#include "policy/core/store.h"

using xr::policy::JsonValue;
using xr::policy::PolicyStore;
using xr::policy::StoreLoadResult;
using xr::policy::TrustTierIndex;

namespace {

void PCloseDel(FILE* f) {
  if (f) pclose(f);
}

std::string HostPath() {
  const char* e = std::getenv("XR_COMMANDS_HOST");
  return e ? std::string(e) : std::string("./build/commands_host");
}
const std::string kRoster = "../../commands/core/roster_v1.json";
const std::string kDir = "dial-test-store";

// Run the host binary with a method + args JSON; return the canonical line.
std::string RunHost(const std::string& method, const std::string& args_json) {
  std::string cmd = HostPath() + " --store-dir " + kDir + " --roster " + kRoster +
                    " " + method + " '" + args_json + "' 2>/dev/null";
  std::unique_ptr<FILE, decltype(&PCloseDel)> p(popen(cmd.c_str(), "r"), &PCloseDel);
  std::string out;
  if (p) {
    char b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof(b), p.get())) > 0) out.append(b, n);
  }
  return out;
}

}  // namespace

int main() {
  // Fresh store dir.
  int setup_rc = std::system(("rm -rf " + kDir + " && mkdir -p " + kDir).c_str());
  (void)setup_rc;

  // 1) dial.set-standard (site=example.com) — first binding.
  std::string r1 = RunHost("invoke",
                           "{\"id\":\"dial.set-standard\",\"source\":\"palette\","
                           "\"site\":\"example.com\"}");
  XR_EXPECT_MSG(r1.find("\"status\":\"authorized\"") != std::string::npos, "standard authorized");
  XR_EXPECT(r1.find("\"trust\":\"kStandard\"") != std::string::npos);
  XR_EXPECT(r1.find("\"prev\":\"\"") != std::string::npos);

  // 2) dial.set-fortress (destructive => confirmed) — toggles to fortress.
  std::string r2 = RunHost("invoke",
                           "{\"id\":\"dial.set-fortress\",\"source\":\"palette\","
                           "\"confirmed\":true,\"site\":\"example.com\"}");
  XR_EXPECT_MSG(r2.find("\"status\":\"authorized\"") != std::string::npos, "fortress authorized");
  XR_EXPECT(r2.find("\"trust\":\"kFortress\"") != std::string::npos);
  XR_EXPECT(r2.find("\"prev\":\"kStandard\"") != std::string::npos);  // the toggle

  // 3) The P6 store (the authority) loads + validates the host's output.
  PolicyStore store(PolicyStore::DefaultKnownVersions());
  StoreLoadResult ld = store.Load(kDir + "/trust_bindings.json");
  XR_EXPECT_MSG(ld.ok, (std::string("P6 PolicyStore validates host output: ") +
                        ld.error_detail).c_str());
  XR_EXPECT_STREQ(ld.doc.schema.c_str(), "xr-trust-bindings");
  XR_EXPECT_EQ(ld.doc.schema_version, 1);
  // the binding the host wrote is present with the fortress tier.
  bool found = false;
  const JsonValue* bs = ld.doc.data.find("bindings");
  if (bs && bs->is_array()) {
    for (const auto& e : bs->as_array()) {
      const JsonValue* d = e.find("domain");
      const JsonValue* t = e.find("trust");
      if (d && d->is_string() && d->as_string() == "example.com" && t && t->is_string() &&
          t->as_string() == "kFortress")
        found = true;
    }
  }
  XR_EXPECT_MSG(found, "host wrote a fortress binding for example.com (P6 state)");

  // 4) The trust tier actually toggled (P6 trust ladder ordering).
  XR_EXPECT_EQ(TrustTierIndex("kFortress"), 2);
  XR_EXPECT_EQ(TrustTierIndex("kStandard"), 0);
  XR_EXPECT(TrustTierIndex("kFortress") > TrustTierIndex("kStandard"));

  // 5) dial.reset removes the binding (caution => no confirmation needed).
  std::string r3 = RunHost("invoke",
                           "{\"id\":\"dial.reset\",\"source\":\"palette\","
                           "\"site\":\"example.com\"}");
  XR_EXPECT_MSG(r3.find("\"removed\":true") != std::string::npos, "reset removed the binding");
  XR_EXPECT(r3.find("\"prev\":\"kFortress\"") != std::string::npos);
  StoreLoadResult ld2 = store.Load(kDir + "/trust_bindings.json");
  XR_EXPECT(ld2.ok);
  bool still_there = false;
  const JsonValue* bs2 = ld2.doc.data.find("bindings");
  if (bs2 && bs2->is_array()) {
    for (const auto& e : bs2->as_array()) {
      const JsonValue* d = e.find("domain");
      if (d && d->is_string() && d->as_string() == "example.com") still_there = true;
    }
  }
  XR_EXPECT(!still_there);

  int teardown_rc = std::system(("rm -rf " + kDir).c_str());
  (void)teardown_rc;
  return xrtest::Report("test_dial");
}
