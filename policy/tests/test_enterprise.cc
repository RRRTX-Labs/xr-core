// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — enterprise managed source: signed-or-ignored (unsigned
// => IGNORED with a ledger row, never enforced), invalid/future => ignored,
// absent => absent, and — when the minisign tool is unavailable on this
// host — the skip-visible path (verification impossible => STILL ignored;
// never a silent accept). Precedence-through-the-same-resolver is proven in
// test_resolve.cc (floor clamps, forced fields, one brain).
#include <cstdio>
#include <string>

#include "policy/enterprise/managed_source.h"
#include "harness.h"

using namespace xr::policy;

namespace {

std::string Tmp(const char* name) { return std::string("/tmp/p6ent_") + name; }
void Write(const std::string& path, const std::string& body) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  XR_EXPECT(f != nullptr);
  std::fwrite(body.data(), 1, body.size(), f);
  std::fclose(f);
}
const char* kGoodDoc = "{\"schema\":\"xr-managed-policy\",\"schema_version\":1,"
                       "\"created_at\":7,\"data\":{\"trust_floor\":\"kFortress\"}}";

bool MinisignPresent() {
  return std::system("command -v minisign >/dev/null 2>&1") == 0;
}

std::string ReadAllOrEmpty(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return "";
  std::string out;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

}  // namespace

int main() {
  const std::string pub = Tmp("managed.pub");
  Write(pub, "placeholder-public-key\n");  // public material; rotation documented

  // ---- absent doc: normal profile ----
  {
    auto r = LoadManagedPolicy(Tmp("absent.json"), pub);
    XR_EXPECT(r.status == ManagedStatus::kAbsent);
    XR_EXPECT(!r.enforced);
  }

  // ---- UNSIGNED: ignored with ledger row (never enforced) ----
  {
    std::string doc = Tmp("unsigned.json");
    Write(doc, kGoodDoc);
    auto r = LoadManagedPolicy(doc, pub);
    XR_EXPECT_MSG(r.status == ManagedStatus::kIgnoredUnsigned,
                   std::string("got ") + ToString(r.status));
    XR_EXPECT(!r.enforced);
    XR_EXPECT(!r.ledger_rows.empty());
    bool has_row = false;
    for (const auto& row : r.ledger_rows) has_row |= row.find("IGNORED (unsigned)") != std::string::npos;
    XR_EXPECT(has_row);
  }

  // ---- invalid JSON: ignored ----
  {
    std::string doc = Tmp("invalid.json");
    Write(doc, "{not json");
    auto r = LoadManagedPolicy(doc, pub);
    XR_EXPECT(r.status == ManagedStatus::kIgnoredInvalid);
    XR_EXPECT(!r.enforced);
    XR_EXPECT(!r.ledger_rows.empty());
  }

  // ---- future schema version: ignored (never guess) ----
  {
    std::string doc = Tmp("future.json");
    Write(doc, "{\"schema\":\"xr-managed-policy\",\"schema_version\":2,\"created_at\":1,\"data\":{}}");
    // (unsigned => fails at the signature gate only if it got past parsing;
    //  here we assert envelope-level handling via ValidateManagedEnvelope)
    auto p = ParseJson(ReadAllOrEmpty(doc));
    if (p.ok) {
      EnterprisePolicy e;
      std::string err;
      XR_EXPECT(!ValidateManagedEnvelope(p.value, &e, &err));
      XR_EXPECT(err.find("future") != std::string::npos);
    }
    auto r = LoadManagedPolicy(doc, pub);
    XR_EXPECT(!r.enforced);  // whatever the failure mode: never enforced
  }

  // ---- signed-but-bad: minisign present => bad signature; absent => skip ----
  // (both paths enforce NOTHING; the difference is only the status/ledger)
  {
    std::string doc = Tmp("signed.json");
    Write(doc, kGoodDoc);
    Write(doc + ".minisig", "fake signature bytes\n");  // minisign detached-sig convention
    auto r = LoadManagedPolicy(doc, pub);
    XR_EXPECT(!r.enforced);
    if (MinisignPresent()) {
      XR_EXPECT(r.status == ManagedStatus::kIgnoredBadSignature);
    } else {
      XR_EXPECT(r.status == ManagedStatus::kSkippedNoTool);
      bool has_row = false;
      for (const auto& row : r.ledger_rows)
        has_row |= row.find("SKIP-visibility") != std::string::npos;
      XR_EXPECT(has_row);
    }
  }

  // ---- envelope strictness: extra fields rejected ----
  {
    auto p = ParseJson(
        "{\"schema\":\"xr-managed-policy\",\"schema_version\":1,\"created_at\":0,"
        "\"data\":{\"trust_floor\":\"kShield\"},\"sneaky\":1}");
    XR_EXPECT(p.ok);
    EnterprisePolicy e;
    std::string err;
    XR_EXPECT(!ValidateManagedEnvelope(p.value, &e, &err));
  }
  // ---- enterprise field validation: bad enum rejected ----
  {
    auto p = ParseJson(
        "{\"schema\":\"xr-managed-policy\",\"schema_version\":1,\"created_at\":0,"
        "\"data\":{\"trust_floor\":\"kUltra\"}}");
    XR_EXPECT(p.ok);
    EnterprisePolicy e;
    std::string err;
    XR_EXPECT(!ValidateManagedEnvelope(p.value, &e, &err));
  }

  return xrtest::Report("test_enterprise");
}
