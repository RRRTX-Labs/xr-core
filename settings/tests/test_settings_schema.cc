// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — strict settings-schema validation (P8-T1): the schema
// DOC is rejected when any row/section violates the frozen settings-schema
// contract (unknown fields, missing deny-safe default, unknown section
// references, bad types/enums, unknown aliases/keys), and a user-facing
// settings DOC validates strictly against the loaded schema (unknown keys
// rejected, type/enum enforced, deny-safe defaults fill the rest).
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "settings/core/settings_schema.h"
#include "settings/tests/harness.h"

using namespace xr::settings;

namespace {

const char* kSchemaPath = "../../settings/core/settings_schema_v1.json";

std::string ReadSchema(std::string* err) {
  bool ok = false;
  std::string t = xrtest::ReadFile(kSchemaPath, &ok);
  if (!ok) *err = "cannot read " + std::string(kSchemaPath);
  return t;
}

// Insert `key: <value>` as the last member of the settings array's first row.
std::string InjectRowField(const std::string& doc, const std::string& field) {
  auto at = doc.find("\"key\": \"network.adblock\"");
  if (at == std::string::npos) return doc;
  auto brace = doc.find('}', at);
  if (brace == std::string::npos) return doc;
  return doc.substr(0, brace) + ",\n    " + field + doc.substr(brace);
}

}  // namespace

int main() {
  std::string err;
  const std::string good = ReadSchema(&err);
  XR_EXPECT_MSG(!good.empty(), "schema read: " + err);

  // 1. The shipped schema itself loads strict-clean.
  {
    SettingsSchema s;
    auto r = s.Load(good);
    XR_EXPECT_MSG(r.ok, r.error);
    XR_EXPECT_EQ(s.version(), 1);
    XR_EXPECT_EQ(s.Count(), 11);
    XR_EXPECT_EQ(s.Sections().size(), 3);
    XR_EXPECT_STREQ(s.anchor_root(), "xr://settings");
    const SettingDef* d = s.FindSetting("network.adblock");
    XR_EXPECT(d != nullptr);
    if (d) {
      XR_EXPECT_STREQ(d->type, "bool");
      XR_EXPECT(d->default_value.is_bool());
      XR_EXPECT(d->default_value.as_bool());
      XR_EXPECT_STREQ(d->attention_tier, "tier2");
      XR_EXPECT_STREQ(d->section, "network");
      XR_EXPECT(!d->aliases.empty());
    }
    XR_EXPECT(s.FindSetting("no.such.key") == nullptr);
    XR_EXPECT(s.FindSection("bogus") == nullptr);
    XR_EXPECT(s.KnownKey("network.adblock"));
    XR_EXPECT(!s.KnownKey("network.nope"));
    // enum options are data
    XR_EXPECT_EQ(s.EnumOptions("network.adblock").size(), 0);
    XR_EXPECT(s.EnumOptions("privacy.notifications").size() >= 3);
    // deny-safe defaults for every key (the schema data must carry them)
    XR_EXPECT_EQ(s.Settings().size(), 11);
    for (const SettingDef* st : s.Settings()) {
      XR_EXPECT_MSG(s.DefaultFor(st->key) != nullptr,
                    "missing default for " + st->key);
    }
    // writable_in_v0 is EMPTY in v0 (skeleton: no user write path yet)
    XR_EXPECT(!s.WritableInV0("network.adblock"));
  }

  // 3. Strict rejects: unknown field on a setting row; missing default.
  {
    SettingsSchema s;
    std::string t1 = InjectRowField(good, "\"rogue_field\": 1");
    XR_EXPECT(!s.Load(t1).ok);
    SettingsSchema s2;
    std::string t2 = InjectRowField(good, "\"attention_tier\": \"tier0\", \"_tmp\": 1");
    XR_EXPECT(!s2.Load(t2).ok);
  }
  {
    // drop the default of the first bool row (deny-safe law)
    SettingsSchema s;
    std::string t = good;
    auto at = t.find("\"default\": true");
    if (at != std::string::npos) t = t.substr(0, at) + t.substr(at + 16);
    auto r = s.Load(t);
    XR_EXPECT_MSG(!r.ok, "missing default must reject");
    XR_EXPECT(r.error.find("default") != std::string::npos);
  }

  // 4. Orphan detection: a section that lists a row owned by ANOTHER
  //    section is rejected (no hand-maintained lists anywhere).
  {
    SettingsSchema s;
    std::string t = good;
    auto at = t.find("\"network.route\"");
    if (at != std::string::npos)
      t.replace(at, 15, "\"privacy.letterbox\"");
    auto r = s.Load(t);
    XR_EXPECT_MSG(!r.ok, "cross-section row reference must reject");
    if (!r.ok)
      XR_EXPECT_MSG(r.error.find("owning section") != std::string::npos,
                    r.error);
  }

  // 5. Doc validation: a subset doc with valid typed values is OK; garbage
  //    keys/values are rejected with reasons.
  {
    SettingsSchema s;
    XR_EXPECT(s.Load(good).ok);
    auto check = [&s](const std::string& json, bool want_ok,
                      const std::string& note) {
      auto p = ParseJson(json);
      XR_EXPECT(p.ok);
      auto r = s.ValidateDoc(p.value);
      XR_EXPECT_MSG(r.ok == want_ok, note + " :: " + r.error);
    };
    check("{\"network.adblock\": true}", true, "valid bool subset");
    check("{\"network.adblock\": false, \"privacy.vault-export\": true}",
          true, "two-key valid subset");
    check("{\"network.route\": \"kDirect\"}", true, "valid enum value");
    check("{\"network.route\": \"kDeny\"}", false, "enum not in options");
    check("{\"network.adblock\": \"yes\"}", false, "bool as string rejected");
    check("{\"network.adblock\": 1}", false, "bool as int rejected");
    check("{\"bogus.key\": true}", false, "unknown doc key rejected");
    check("{\"identity.kind\": \"standard\"}", true, "identity enum valid");
    check("{\"identity.kind\": \"root\"}", false, "identity enum bogus");
  }

  // 6. Int range guard (never huge/NaN-ish ints).
  {
    SettingsSchema s;
    XR_EXPECT(s.Load(good).ok);
    auto p = ParseJson("{\"network.adblock\": 9223372036854775807}");
    XR_EXPECT(p.ok);
    auto r = s.ValidateDoc(p.value);
    XR_EXPECT(!r.ok);  // bool must be bool; also exercises the range path
  }

  std::printf("suite: %d checks, %d failures\n", xrtest::g_checks,
              xrtest::g_failures);
  return xrtest::Report("test_settings_schema");
}
