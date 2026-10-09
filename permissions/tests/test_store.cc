// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P15 T1 — overlay store integrity: "corrupt => deny" is proven, not claimed.
// Every corrupt variant must load as the fully-denying store, and the
// resolver's view of it must be the corrupt projection. A planted "corrupt =>
// keep what parsed" loader would have to fail the partial-store checks below.
#include <string>

#include "permissions/core/store.h"
#include "permissions/core/types.h"
#include "policy/tests/harness.h"

using namespace xr::permissions;

namespace {

// Canonical by construction: keys sorted, no whitespace.
const char* kValid =
    "{\"contract\":\"permission-store\",\"contract_version\":1,\"identities\":{"
    "\"xr:0001\":{\"defaults\":{\"camera\":\"kAsk\"},\"denied\":false,"
    "\"extras\":{\"sensors\":\"kDeny\"},\"grants\":[{\"capability\":\"geolocation\","
    "\"created_at\":1000,\"domain\":\"example.com\",\"expires_at\":604801000,"
    "\"id\":\"g1\",\"identity\":\"xr:0001\",\"remaining_uses\":0,\"revoked\":false,"
    "\"revoked_at\":0,\"scope\":\"7d\",\"session_id\":\"\"}]}},\"next_seq\":2}";

const char* kDenyView = "{\"contract_version\":1,\"corrupt\":true}";

// Replaces the first occurrence of `from` with `to`; the test fails if absent,
// so a drifted fixture cannot pass vacuously.
std::string Mutate(const std::string& base, const std::string& from, const std::string& to) {
  auto pos = base.find(from);
  if (pos == std::string::npos) {
    XR_EXPECT_MSG(false, "fixture drift: mutation anchor missing: " + from);
    return base;
  }
  return base.substr(0, pos) + to + base.substr(pos + from.size());
}

void ExpectCorrupt(const std::string& text, const std::string& what) {
  Store s = LoadStore(text);
  XR_EXPECT_MSG(s.corrupt, "corrupt expected: " + what);
  XR_EXPECT_MSG(ProjectViewJson(s) == kDenyView, "fully-denying view expected: " + what);
  XR_EXPECT_MSG(s.identities.empty(), "a corrupt store must not keep partial data: " + what);
}

}  // namespace

int main() {
  // --- the canonical fixture is accepted and round-trips byte-for-byte --------
  {
    Store s = LoadStore(kValid);
    XR_EXPECT_MSG(!s.corrupt, "canonical literal must load (is the fixture canonical?)");
    XR_EXPECT_MSG(SerializeStore(s) == kValid, "round-trip must be byte-identical");
    XR_EXPECT_MSG(s.identities.count("xr:0001") == 1, "identity present after load");
    XR_EXPECT_MSG(s.identities.at("xr:0001").grants.size() == 1, "grant present after load");
    XR_EXPECT_MSG(ProjectViewJson(s).find("\"corrupt\"") == std::string::npos,
                  "valid store must not project a corrupt flag");
  }

  // --- the empty store is valid and projects an inert view -------------------
  {
    Store empty;
    std::string text = SerializeStore(empty);
    Store back = LoadStore(text);
    XR_EXPECT_MSG(!back.corrupt, "empty store loads");
    XR_EXPECT_MSG(SerializeStore(back) == text, "empty store round-trips");
    XR_EXPECT_MSG(ProjectViewJson(back) ==
                      "{\"contract_version\":1,\"defaults\":[],\"denied_identities\":[],\"grants\":[]}",
                  "empty view is inert (no defaults, no denials, no grants)");
  }

  // --- bytes that are not our canonical form ---------------------------------
  ExpectCorrupt(std::string(kValid, 40), "truncated");
  ExpectCorrupt(std::string(kValid) + "x", "trailing byte");
  ExpectCorrupt(std::string(kValid) + "\n", "trailing newline (canonical has none)");
  ExpectCorrupt("", "empty input");
  ExpectCorrupt("not json", "garbage");
  ExpectCorrupt(Mutate(kValid, "\"contract\":\"permission-store\",", "\"contract\": \"permission-store\","),
                "whitespace after colon (non-canonical bytes)");
  // Duplicate key: the shared parser keeps the LAST value, so the re-serialized
  // text differs from the original and the round-trip check rejects it.
  ExpectCorrupt(Mutate(kValid, "\"next_seq\":2", "\"next_seq\":1,\"next_seq\":2"), "duplicate key (keep-last)");
  // Unsorted keys: the same bytes in a different order are not canonical.
  ExpectCorrupt(Mutate(kValid, "\"contract\":\"permission-store\",\"contract_version\":1,",
                       "\"contract_version\":1,\"contract\":\"permission-store\","),
                "unsorted top-level keys");

  // --- schema violations (canonical bytes, wrong content) --------------------
  ExpectCorrupt(Mutate(kValid, "\"next_seq\":2", "\"next_seq\":2,\"usage_active\":true"),
                "extra top-level key (no usage field exists)");
  ExpectCorrupt(Mutate(kValid, "\"next_seq\":2", "\"next_seq\":\"2\""), "wrong type next_seq");
  ExpectCorrupt(Mutate(kValid, "\"next_seq\":2", "\"next_seq\":0"), "next_seq below 1");
  ExpectCorrupt(Mutate(kValid, "\"contract_version\":1", "\"contract_version\":2"), "unknown store version");
  ExpectCorrupt(Mutate(kValid, "\"defaults\":{\"camera\":\"kAsk\"}", "\"defaults\":{\"sensors\":\"kAsk\"}"),
                "an extra used as a default (extras have no PermissionState slot)");
  ExpectCorrupt(Mutate(kValid, "\"camera\":\"kAsk\"", "\"camera\":\"kAllowAll\""), "unknown state name");
  ExpectCorrupt(Mutate(kValid, "\"camera\":\"kAsk\"", "\"unicorn\":\"kAsk\""), "unknown capability name");
  ExpectCorrupt(Mutate(kValid, "\"denied\":false", "\"denied\":\"no\""), "wrong type denied");
  ExpectCorrupt(Mutate(kValid, "\"sensors\":\"kDeny\"", "\"sensors\":\"kAllow\""),
                "a record claiming kAllow for an extra (envelope: deny-only)");
  ExpectCorrupt(Mutate(kValid, "\"sensors\":\"kDeny\"", "\"telepathy\":\"kDeny\""), "unknown extra name");
  ExpectCorrupt(Mutate(kValid, "\"scope\":\"7d\"", "\"scope\":\"forever\""), "unknown scope (permanent is Settings-only)");
  ExpectCorrupt(Mutate(kValid, "\"expires_at\":604801000", "\"expires_at\":9999999999999"),
                "7d lifetime longer than the scope allows");
  ExpectCorrupt(Mutate(kValid, "\"identity\":\"xr:0001\",\"remaining_uses\"", "\"identity\":\"xr:9999\",\"remaining_uses\""),
                "grant filed under a different identity (cross-identity smuggle)");
  ExpectCorrupt(Mutate(kValid, "\"session_id\":\"\"}", "\"session_id\":\"s1\"}"),
                "session_id on a 7d grant (field not admitted by scope)");
  ExpectCorrupt(Mutate(kValid, "\"revoked_at\":0", "\"revoked_at\":5"), "revoked_at set on a live grant");
  ExpectCorrupt(Mutate(kValid, "\"id\":\"g1\"", "\"id\":\"\""), "empty grant id");

  // Duplicate grant id across identities: one copy is valid, two are corrupt.
  {
    const std::string g7 =
        "\"capability\":\"camera\",\"created_at\":1,\"domain\":\"a.example\",\"expires_at\":0,"
        "\"id\":\"g7\",\"identity\":\"%ID%\",\"remaining_uses\":1,\"revoked\":false,"
        "\"revoked_at\":0,\"scope\":\"once\",\"session_id\":\"\"";
    auto rec = [&](const std::string& id) {
      std::string g = g7;
      g.replace(g.find("%ID%"), 4, id);
      return "\"" + id + "\":{\"defaults\":{},\"denied\":false,\"extras\":{},\"grants\":[{" + g + "}]}";
    };
    const std::string head =
        "{\"contract\":\"permission-store\",\"contract_version\":1,\"identities\":{";
    const std::string tail = "},\"next_seq\":8}";
    XR_EXPECT_MSG(!LoadStore(head + rec("xr:0001") + tail).corrupt, "fixture: one copy of g7 is valid");
    ExpectCorrupt(head + rec("xr:0001") + "," + rec("xr:0002") + tail, "duplicate grant id across identities");
  }

  // --- exhaustive single-byte mutation sweep (the deterministic fuzz row) ----
  // libFuzzer is absent in this sandbox (no clang), so the fuzz row is an
  // EXHAUSTIVE single-byte mutation of the canonical fixture, at every position,
  // with six substitutions. Each mutant must be refused (corrupt, fully
  // denying), or be accepted ONLY as canonical bytes that re-serialize to
  // themselves (no silent repair). Acceptance is legitimate for a semantic edit
  // that still satisfies the schema, so the property is about the bytes.
  {
    const std::string base = kValid;
    const char subs[] = {'"', ' ', '0', 'x', '}', ','};
    int mutants = 0, refused = 0, accepted = 0;
    for (size_t i = 0; i < base.size(); ++i) {
      for (char c : subs) {
        if (base[i] == c) continue;
        std::string m = base;
        m[i] = c;
        ++mutants;
        Store s = LoadStore(m);
        if (s.corrupt) {
          ++refused;
          XR_EXPECT_MSG(ProjectViewJson(s) == kDenyView, "a refused mutant denies every identity");
        } else {
          ++accepted;
          XR_EXPECT_MSG(SerializeStore(s) == m, "an accepted mutant is canonical (no silent repair)");
        }
      }
    }
    XR_EXPECT_MSG(mutants > 1000, "the sweep covers every position of the fixture");
    XR_EXPECT_MSG(refused > accepted, "most single-byte corruptions are refused");
    XR_EXPECT_MSG(refused + accepted == mutants, "every mutant is either refused or canonical-accepted");
  }

  // Corruption persists: a corrupt store serializes to a marker that is itself
  // corrupt on reload, so a save path cannot erase the deny.
  {
    Store bad = LoadStore("garbage");
    std::string persisted = SerializeStore(bad);
    XR_EXPECT_MSG(LoadStore(persisted).corrupt, "corruption survives save and reload");
    XR_EXPECT_MSG(ProjectViewJson(LoadStore(persisted)) == kDenyView, "persisted corruption still denies");
  }

  return xrtest::Report("test_store");
}
