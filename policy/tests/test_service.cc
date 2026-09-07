// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — PolicyResolverService integration: store-dir loading
// (identity list, trust bindings, exceptions — merged UNDER request
// layers), snapshot sequencing, and the SIGNED enterprise path end-to-end
// with a real minisign keypair (generated in a temp dir at test time).
// When minisign is absent the signature-dependent half SKIPs VISIBLY
// (printed, non-failing — skip-policy law); unsigned-ignored is tested
// everywhere. Dump/watch surfaces are exercised for shape.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "policy/core/service.h"
#include "harness.h"

using namespace xr::policy;

namespace {

std::string TmpDir(const char* tag) {
  std::string d = "/tmp/p6svc_" + std::string(tag) + std::to_string(::getpid());
  std::filesystem::remove_all(d);
  std::filesystem::create_directories(d);
  return d;
}

void Write(const std::string& path, const std::string& body) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  XR_EXPECT(f != nullptr);
  std::fwrite(body.data(), 1, body.size(), f);
  std::fclose(f);
}

bool MinisignPresent() {
  return std::system("command -v minisign >/dev/null 2>&1") == 0;
}

}  // namespace

int main() {
  const std::string kReq =
      "{\"identity\":{\"value\":\"xr:11111111-2222-4333-8444-555555555555\"},"
      "\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"example.com\"},"
      "\"request_class\":\"kNavigation\"}";

  // ---- store-less service: bare request parity (deny for unknown id) ----
  {
    PolicyResolverService svc;
    std::string out = svc.ResolveText(kReq);
    EffectivePolicy deny;
    XR_EXPECT(out == ResolveOutputToJson(ResolveOutput{true, "", deny}).Canonical());
  }

  // ---- store-dir: identity list + binding + exception merge ----
  {
    std::string dir = TmpDir("store");
    Write(dir + "/identity-list-v1.json",
          "{\"schema\":\"xr-identity-list\",\"schema_version\":1,\"created_at\":1,"
          "\"data\":{\"identities\":[{\"value\":\"xr:11111111-2222-4333-8444-555555555555\"}]}}");
    Write(dir + "/trust-bindings-v1.json",
          "{\"schema\":\"xr-trust-bindings\",\"schema_version\":1,\"created_at\":1,"
          "\"data\":{\"bindings\":[{\"domain\":\"example.com\",\"identity\":\"\","
          "\"trust\":\"kShield\",\"created_at\":1}]}}");
    Write(dir + "/exceptions-v1.json",
          "{\"schema\":\"xr-exceptions\",\"schema_version\":1,\"created_at\":1,"
          "\"data\":{\"exceptions\":[{\"id\":\"e1\",\"domain\":\"other.com\",\"identity\":\"\","
          "\"scope\":\"7d\",\"trust\":\"kFortress\",\"created_at\":1,\"expires_at\":9999,"
          "\"session_id\":\"\",\"remaining_uses\":0}]}}");
    PolicyResolverService svc(dir);
    std::string out = svc.ResolveText(kReq);
    // Binding applies: Shield tier for example.com.
    auto p = ParseJson(out);
    XR_EXPECT(p.ok);
    const JsonValue* ok = p.value.find("ok");
    XR_EXPECT(ok != nullptr);
    const JsonValue* route = ok ? ok->find("egress")->find("route") : nullptr;
    XR_EXPECT(route != nullptr && route->as_string() == "kProxy");
    // The 7d exception is for other.com => must NOT apply here.
    const JsonValue* letterbox = ok ? ok->find("letterbox") : nullptr;
    XR_EXPECT(letterbox != nullptr && !letterbox->as_bool());
    // Identity recognized via the store list (not deny).
    const JsonValue* storage = ok ? ok->find("storage_scope")->find("scope") : nullptr;
    XR_EXPECT(storage != nullptr && storage->as_string() == "kIdentityScoped");
    // Request-carried layers WIN over store layers.
    std::string req_explicit = kReq.substr(0, kReq.size() - 1) +
        ",\"trust_context\":\"kFortress\"}";
    std::string out2 = svc.ResolveText(req_explicit);
    auto p2 = ParseJson(out2);
    const JsonValue* ok2 = p2.value.find("ok");
    XR_EXPECT(ok2 != nullptr && ok2->find("letterbox")->as_bool());
    // Snapshot sequencing: full then diff, both within budget.
    auto full = svc.SnapshotFull();
    XR_EXPECT(full.ok);
    auto diff = svc.SnapshotDiff();
    XR_EXPECT(diff.ok);
    auto dec = DecodeFullSnapshot(full.blob);
    XR_EXPECT(dec.ok);
    XR_EXPECT_EQ(dec.entries.size(), 2u);  // both resolves cached distinct keys
  }

  // ---- unsigned managed doc: ignored, never enforced ----
  {
    std::string dir = TmpDir("unsigned");
    Write(dir + "/managed-policy.json",
          "{\"schema\":\"xr-managed-policy\",\"schema_version\":1,\"created_at\":1,"
          "\"data\":{\"trust_floor\":\"kFortress\"}}");
    PolicyResolverService svc(dir);
    std::string out = svc.ResolveText(kReq);
    auto p = ParseJson(out);
    const JsonValue* ok = p.value.find("ok");
    XR_EXPECT(ok != nullptr);
    // No identity list in this store => unknown identity => deny... which is
    // ALSO the outcome an enforced floor would produce. Use a known identity
    // to make the assertion meaningful:
    const std::string kReq2 =
        "{\"identity\":{\"value\":\"xr:00000000-0000-4000-8000-000000000001\"},"
        "\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"example.com\"},"
        "\"request_class\":\"kNavigation\"}";
    std::string out2 = svc.ResolveText(kReq2);
    auto p2 = ParseJson(out2);
    const JsonValue* ok2 = p2.value.find("ok");
    XR_EXPECT(ok2 != nullptr);
    XR_EXPECT(!ok2->find("letterbox")->as_bool());  // Standard tier: floor NOT applied
    XR_EXPECT(svc.diagnostics().managed.status == ManagedStatus::kIgnoredUnsigned);
    bool ledger_row = false;
    for (const auto& r : svc.diagnostics().managed.ledger_rows)
      ledger_row |= r.find("IGNORED (unsigned)") != std::string::npos;
    XR_EXPECT(ledger_row);
  }

  // ---- SIGNED managed doc: verified => enforced => floor clamps (real
  //      minisign keypair; SKIP-visible when the tool is absent) ----
  {
    if (!MinisignPresent()) {
      std::printf("SKIP (tool absent: minisign) — signed-managed-path half of "
                  "test_service; unsigned-ignored halves ran. Install minisign "
                  "to exercise the verified path (CI does).\n");
    } else {
      std::string dir = TmpDir("signed");
      Write(dir + "/managed-policy.json",
            "{\"schema\":\"xr-managed-policy\",\"schema_version\":1,\"created_at\":1,"
            "\"data\":{\"trust_floor\":\"kFortress\"}}");
      std::string pub = dir + "/managed-policy.pub";
      int rc = std::system(("cd '" + dir + "' && printf 'k\\nk\\n' | minisign -G -p managed-policy.pub -s managed-policy.key >/dev/null 2>&1 && printf 'k\\n' | minisign -Sm managed-policy.json -s managed-policy.key >/dev/null 2>&1").c_str());
      XR_EXPECT_MSG(rc == 0, "minisign keygen+sign setup");
      PolicyResolverService svc(dir);
      XR_EXPECT_MSG(svc.diagnostics().managed.status == ManagedStatus::kVerified,
                    std::string("expected kVerified, got ") +
                        ToString(svc.diagnostics().managed.status));
      XR_EXPECT(svc.diagnostics().managed.enforced);
      const std::string kReq2 =
          "{\"identity\":{\"value\":\"xr:00000000-0000-4000-8000-000000000001\"},"
          "\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"example.com\"},"
          "\"request_class\":\"kNavigation\"}";
      std::string out = svc.ResolveText(kReq2);
      auto p = ParseJson(out);
      const JsonValue* ok = p.value.find("ok");
      XR_EXPECT(ok != nullptr);
      XR_EXPECT_MSG(ok->find("letterbox")->as_bool(),
                    "enterprise floor kFortress must clamp a Standard request");
    }
  }

  // ---- corrupt store docs: typed ledger rows, deny-safe continuation ----
  {
    std::string dir = TmpDir("corrupt");
    Write(dir + "/trust-bindings-v1.json", "{corrupt");
    Write(dir + "/exceptions-v1.json",
          "{\"schema\":\"xr-exceptions\",\"schema_version\":1,\"created_at\":1,"
          "\"data\":{\"exceptions\":[{\"id\":\"x\",\"domain\":\"a.com\",\"scope\":\"forever\","
          "\"trust\":\"kShield\",\"created_at\":1,\"expires_at\":0,\"session_id\":\"\","
          "\"remaining_uses\":0}]}}");
    PolicyResolverService svc(dir);
    bool saw_reject = false, saw_ledger = false;
    for (const auto& r : svc.diagnostics().ledger_rows) {
      saw_ledger = true;
      saw_reject |= r.find("rejected") != std::string::npos;
    }
    XR_EXPECT(saw_ledger);
    XR_EXPECT(saw_reject);
    // Resolution still total (deny for unknown identity here).
    std::string out = svc.ResolveText(kReq);
    EffectivePolicy deny;
    XR_EXPECT(out == ResolveOutputToJson(ResolveOutput{true, "", deny}).Canonical());
  }

  // ---- dump/watch shape ----
  {
    PolicyResolverService svc;
    std::string d = svc.Dump(true);
    XR_EXPECT(d.find("== xr policy service ==") == 0);
    XR_EXPECT(d.find("cache:") != std::string::npos);
    XR_EXPECT(d.find("managed: status=") != std::string::npos);
    JsonValue j = svc.DumpJson(true);
    XR_EXPECT(j.find("cache") != nullptr);
    XR_EXPECT(j.find("managed") != nullptr);
    XR_EXPECT(j.find("ledger") != nullptr);
  }

  return xrtest::Report("test_service");
}
