// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — PolicyStore: envelope validation, the exceptions
// v1→v2 forward migration (granted_by synthesis encoding the "never
// permanent in-flow" law), and the DOWNGRADE NO-OP property over 200
// seeded pseudo-random docs: a v2 doc read by a v1-era binary round-trips
// byte-preserved (never rewritten, never rejected, never enforced
// guessingly). Deterministic seeds; std::mt19937 only.
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "policy/core/store.h"
#include "harness.h"

using namespace xr::policy;

namespace {

const char* kV1 = R"({"schema":"xr-exceptions","schema_version":1,"created_at":10,
 "data":{"exceptions":[
  {"id":"e1","domain":"a.com","identity":"","scope":"once","trust":"kShield",
   "created_at":1,"expires_at":0,"session_id":"","remaining_uses":1},
  {"id":"e2","domain":"b.com","identity":"","scope":"7d","trust":"kFortress",
   "created_at":2,"expires_at":9999,"session_id":"","remaining_uses":0},
  {"id":"e3","domain":"c.com","identity":"","scope":"permanent","trust":"kStandard",
   "created_at":3,"expires_at":0,"session_id":"","remaining_uses":0}]}})";

std::string TmpFile(const char* name) {
  return std::string("/tmp/p6store_") + name + ".json";
}

std::string LoadRaw(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return "";
  std::string out;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

// Seeded pseudo-random valid v2 exceptions doc (raw JSON text).
std::string RandomV2Doc(std::mt19937& rng) {
  static const char* domains[] = {"a.com", "b.org", "c.net", "d.io", "e.dev"};
  static const char* scopes[] = {"once", "session", "7d", "permanent"};
  static const char* tiers[] = {"kStandard", "kShield", "kFortress"};
  std::string out = "{\"schema\":\"xr-exceptions\",\"schema_version\":2,\"created_at\":" +
                    std::to_string(rng() % 100000) + ",\"data\":{\"exceptions\":[";
  int count = 1 + static_cast<int>(rng() % 6);
  for (int i = 0; i < count; ++i) {
    std::string scope = scopes[rng() % 4];
    std::string granted_by = (scope == "permanent") ? "settings" : "in_flow";
    out += "{\"id\":\"e" + std::to_string(rng() % 100000) + "\"";
    out += ",\"domain\":\"" + std::string(domains[rng() % 5]) + "\"";
    out += ",\"identity\":\"\"";
    out += ",\"scope\":\"" + scope + "\"";
    out += ",\"trust\":\"" + std::string(tiers[rng() % 3]) + "\"";
    out += ",\"created_at\":" + std::to_string(rng() % 1000000);
    out += ",\"expires_at\":" + std::to_string(scope == "7d" ? 1000 + static_cast<int>(rng() % 999999) : 0);
    out += ",\"session_id\":\"" + (scope == "session" ? "s" + std::to_string(rng() % 100) : "") + "\"";
    out += ",\"remaining_uses\":" + std::to_string(scope == "once" ? 1 + static_cast<int>(rng() % 9) : 0);
    out += ",\"granted_by\":\"" + granted_by + "\"}";
    if (i + 1 < count) out += ",";
  }
  out += "]}}";
  return out;
}

}  // namespace

int main() {
  PolicyStore latest(PolicyStore::DefaultKnownVersions());
  // "Old binary": knows exceptions only as v1 (P5-era code).
  auto old_known = PolicyStore::DefaultKnownVersions();
  old_known["xr-exceptions"] = 1;
  PolicyStore old(old_known);

  // ---- load + validate v1, forward-migrate to v2 ----
  {
    std::string path = TmpFile("v1");
    std::FILE* f = std::fopen(path.c_str(), "wb");
    XR_EXPECT(f != nullptr);
    std::fwrite(kV1, 1, std::strlen(kV1), f);
    std::fclose(f);

    auto lr = latest.Load(path);
    XR_EXPECT_MSG(lr.ok, "v1 loads: " + lr.error_detail);
    XR_EXPECT_EQ(lr.doc.schema_version, 1);
    auto m = latest.MigrateToLatest(lr.doc);
    XR_EXPECT(m.ok);
    XR_EXPECT(m.changed);
    XR_EXPECT_EQ(m.doc.schema_version, 2);
    // granted_by synthesis: in_flow scopes => in_flow; permanent => settings.
    const JsonValue* xs = m.doc.data.find("exceptions");
    XR_EXPECT(xs != nullptr && xs->is_array());
    int checked = 0;
    for (const auto& e : xs->as_array()) {
      const JsonValue* gb = e.find("granted_by");
      const JsonValue* sc = e.find("scope");
      XR_EXPECT(gb != nullptr && gb->is_string());
      XR_EXPECT(sc != nullptr && sc->is_string());
      if (sc->as_string() == "permanent") {
        XR_EXPECT_STREQ(gb->as_string().c_str(), "settings");
      } else {
        XR_EXPECT_STREQ(gb->as_string().c_str(), "in_flow");
      }
      ++checked;
    }
    XR_EXPECT_EQ(checked, 3);
    // Migrated doc re-validates at v2 (strict).
    std::string err;
    XR_EXPECT(ValidateExceptions(m.doc.data, 2, &err));
    // And saving the migrated doc + reloading gives the same doc (round trip).
    std::string spath = TmpFile("v2_saved");
    std::string io_err;
    XR_EXPECT(latest.Save(spath, m.doc, &io_err));
    auto lr2 = latest.Load(spath);
    XR_EXPECT_MSG(lr2.ok, "saved v2 reloads: " + lr2.error_detail);
    XR_EXPECT_EQ(lr2.doc.schema_version, 2);
    XR_EXPECT(lr2.doc.data == m.doc.data);
  }

  // ---- v2 law: permanent + in_flow is INVALID (never forever in-flow) ----
  {
    std::string bad = R"({"schema":"xr-exceptions","schema_version":2,"created_at":1,
     "data":{"exceptions":[{"id":"x","domain":"a.com","identity":"","scope":"permanent",
     "trust":"kShield","created_at":1,"expires_at":0,"session_id":"","remaining_uses":0,
     "granted_by":"in_flow"}]}})";
    std::string path = TmpFile("bad_v2");
    std::FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(bad.data(), 1, bad.size(), f);
    std::fclose(f);
    auto lr = latest.Load(path);
    XR_EXPECT_MSG(!lr.ok, "permanent+in_flow must be rejected");
    XR_EXPECT(lr.error == StoreError::kInvalidEntry);
  }

  // ---- downgrade no-op property: 200 seeded v2 docs through old binary ----
  {
    int preserved = 0;
    for (int seed = 1; seed <= 200; ++seed) {
      std::mt19937 rng(static_cast<unsigned>(seed));
      std::string doc = RandomV2Doc(rng);
      std::string path = TmpFile("prop");
      std::FILE* f = std::fopen(path.c_str(), "wb");
      std::fwrite(doc.data(), 1, doc.size(), f);
      std::fclose(f);
      // Old binary loads: future version => no-op mode (raw kept).
      auto lr = old.Load(path);
      if (!XR_EXPECT_MSG(lr.ok, "seed " + std::to_string(seed) + ": old binary must load v2 doc")) break;
      XR_EXPECT(lr.doc.future_version);
      // Old binary "saves" (e.g. unrelated field change): data preserved.
      std::string io_err;
      if (!XR_EXPECT_MSG(old.Save(path, lr.doc, &io_err), "seed save")) break;
      // Byte-level data preservation: reparse and compare full docs.
      std::string after = LoadRaw(path);
      auto pa = ParseJson(doc);
      auto pb = ParseJson(after);
      if (!XR_EXPECT_MSG(pa.ok && pb.ok, "seed parse")) break;
      // Canonical compare (whitespace-insensitive, order-insensitive).
      if (pa.value.Canonical() == pb.value.Canonical()) ++preserved;
      else xrtest::Record(false, "seed " + std::to_string(seed) + ": data NOT preserved");
    }
    XR_EXPECT_EQ(preserved, 200);
  }

  // ---- corrupt prefs battery: typed errors, never guesses ----
  {
    struct Case { std::string name, body; StoreError want; };
    std::vector<Case> cases = {
      {"not-json", "{oops", StoreError::kMalformedInput},
      {"no-envelope", "[1,2,3]", StoreError::kMalformedInput},
      {"unknown-schema",
       "{\"schema\":\"xr-mystery\",\"schema_version\":1,\"created_at\":0,\"data\":{}}",
       StoreError::kUnknownSchema},
      {"extra-envelope-field",
       "{\"schema\":\"xr-identity-list\",\"schema_version\":1,\"created_at\":0,\"data\":{},\"x\":1}",
       StoreError::kMalformedInput},
      {"bad-identity",
       "{\"schema\":\"xr-identity-list\",\"schema_version\":1,\"created_at\":0,"
       "\"data\":{\"identities\":[{\"value\":\"not-an-xr-id\"}]}}",
       StoreError::kInvalidEntry},
      {"bad-scope",
       "{\"schema\":\"xr-exceptions\",\"schema_version\":2,\"created_at\":0,"
       "\"data\":{\"exceptions\":[{\"id\":\"e\",\"domain\":\"a.com\",\"scope\":\"forever\","
       "\"trust\":\"kShield\",\"created_at\":1,\"expires_at\":0,\"session_id\":\"\","
       "\"remaining_uses\":0,\"granted_by\":\"in_flow\"}]}}",
       StoreError::kInvalidEntry},
      {"7d-without-expiry",
       "{\"schema\":\"xr-exceptions\",\"schema_version\":2,\"created_at\":0,"
       "\"data\":{\"exceptions\":[{\"id\":\"e\",\"domain\":\"a.com\",\"scope\":\"7d\","
       "\"trust\":\"kShield\",\"created_at\":1,\"expires_at\":0,\"session_id\":\"\","
       "\"remaining_uses\":0,\"granted_by\":\"in_flow\"}]}}",
       StoreError::kInvalidEntry},
      {"once-without-remaining-uses",
       "{\"schema\":\"xr-exceptions\",\"schema_version\":2,\"created_at\":0,"
       "\"data\":{\"exceptions\":[{\"id\":\"e\",\"domain\":\"a.com\",\"scope\":\"once\","
       "\"trust\":\"kShield\",\"created_at\":1,\"expires_at\":0,\"session_id\":\"\","
       "\"remaining_uses\":0,\"granted_by\":\"in_flow\"}]}}",
       StoreError::kInvalidEntry},
      {"session-with-empty-session-id",
       "{\"schema\":\"xr-exceptions\",\"schema_version\":2,\"created_at\":0,"
       "\"data\":{\"exceptions\":[{\"id\":\"e\",\"domain\":\"a.com\",\"scope\":\"session\","
       "\"trust\":\"kShield\",\"created_at\":1,\"expires_at\":0,\"session_id\":\"\","
       "\"remaining_uses\":0,\"granted_by\":\"in_flow\"}]}}",
       StoreError::kInvalidEntry},
      // P11-T0-c: the "integer REQUIRED" branches (expires_at / remaining_uses
      // absent or non-int) were never exercised — a `return false`->`true`
      // mutant there SURVIVED the seeded sample (deny-guard: a validation
      // failure that accepts). Absence must reject, never guess a default.
      {"missing-expires-at-field",
       "{\"schema\":\"xr-exceptions\",\"schema_version\":2,\"created_at\":0,"
       "\"data\":{\"exceptions\":[{\"id\":\"e\",\"domain\":\"a.com\",\"scope\":\"permanent\","
       "\"trust\":\"kShield\",\"created_at\":1,\"session_id\":\"\","
       "\"remaining_uses\":0,\"granted_by\":\"settings\"}]}}",
       StoreError::kInvalidEntry},
      {"missing-remaining-uses-field",
       "{\"schema\":\"xr-exceptions\",\"schema_version\":2,\"created_at\":0,"
       "\"data\":{\"exceptions\":[{\"id\":\"e\",\"domain\":\"a.com\",\"scope\":\"permanent\","
       "\"trust\":\"kShield\",\"created_at\":1,\"expires_at\":0,\"session_id\":\"\","
       "\"granted_by\":\"settings\"}]}}",
       StoreError::kInvalidEntry},
      {"non-int-remaining-uses",
       "{\"schema\":\"xr-exceptions\",\"schema_version\":2,\"created_at\":0,"
       "\"data\":{\"exceptions\":[{\"id\":\"e\",\"domain\":\"a.com\",\"scope\":\"permanent\","
       "\"trust\":\"kShield\",\"created_at\":1,\"expires_at\":0,\"session_id\":\"\","
       "\"remaining_uses\":\"3\",\"granted_by\":\"settings\"}]}}",
       StoreError::kInvalidEntry},
      {"identity-list-bad-storage",
       "{\"schema\":\"xr-identity-list\",\"schema_version\":1,\"created_at\":0,"
       "\"data\":{\"identities\":[{\"value\":\"xr:abc\",\"storage\":\"somewhere-else\"}]}}",
       StoreError::kInvalidEntry},
      {"identity-list-bad-kind",
       "{\"schema\":\"xr-identity-list\",\"schema_version\":1,\"created_at\":0,"
       "\"data\":{\"identities\":[{\"value\":\"xr:abc\",\"kind\":\"ultra\"}]}}",
       StoreError::kInvalidEntry},
    };
    for (const auto& c : cases) {
      std::string path = TmpFile("corrupt");
      std::FILE* f = std::fopen(path.c_str(), "wb");
      std::fwrite(c.body.data(), 1, c.body.size(), f);
      std::fclose(f);
      auto lr = latest.Load(path);
      XR_EXPECT_MSG(!lr.ok, c.name + ": must be rejected");
      XR_EXPECT_MSG(lr.error == c.want, c.name + ": got " + ToString(lr.error) + " want " +
                                            ToString(c.want) + " (" + lr.error_detail + ")");
    }
    // Missing file: kIoError (caller treats as absent layer).
    auto lr = latest.Load("/tmp/p6store_definitely_absent.json");
    XR_EXPECT(!lr.ok);
    XR_EXPECT(lr.error == StoreError::kIoError);
  }

  return xrtest::Report("test_store");
}
