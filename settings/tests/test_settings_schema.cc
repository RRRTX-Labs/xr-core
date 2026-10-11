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
#include "settings/core/json.h"
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
    XR_EXPECT_EQ(s.Sections().size(), 4);  // + permissions (P15-CLOSE C5)
    {
      const SectionDef* p = s.FindSection("permissions");
      XR_EXPECT(p != nullptr && p->order == 10 && p->settings.empty() &&
                !p->aliases.empty());
    }
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
  // P15 hardening (mutation survivors, research-log-P15 section 10).
  {
    // The int window is closed: its lower bound is in range.
    SettingDef def;
    def.type = "int";
    std::string err;
    XR_EXPECT_MSG(ValidateSettingValue(def, {}, JsonValue(static_cast<int64_t>(-2147483648LL)), &err),
                  "int at the lower window bound accepted");
    XR_EXPECT_MSG(!ValidateSettingValue(def, {}, JsonValue(static_cast<int64_t>(-2147483649LL)), &err),
                  "int below the window refused");
    // An enum value must be a string; an unknown declared type is refused.
    SettingDef en;
    en.type = "enum";
    XR_EXPECT_MSG(!ValidateSettingValue(en, {"on", "off"}, JsonValue(static_cast<int64_t>(1)), &err),
                  "non-string enum value refused");
    SettingDef bogus;
    bogus.type = "bogus";
    XR_EXPECT_MSG(!ValidateSettingValue(bogus, {}, JsonValue(std::string("x")), &err),
                  "unknown declared type refused");
  }
  {
    // Schema-variant loads: edit the parsed v1 document, then load it strictly.
    auto load_edited = [](auto edit) -> SchemaLoadResult {
      std::string err;
      const std::string text = ReadSchema(&err);
      JsonParseResult pr = ParseJson(text);
      JsonValue::Object obj = pr.value.as_object();
      edit(obj);
      SettingsSchema s;
      return s.Load(JsonValue(obj).Canonical());
    };
    const SchemaLoadResult base = load_edited([](JsonValue::Object&) {});
    XR_EXPECT_MSG(base.ok, "unedited v1 schema loads (variant harness sanity)");
    auto first_row = [](JsonValue::Object& o, auto set) {
      JsonValue::Array rows = o.at("settings").as_array();
      JsonValue::Object row = rows[0].as_object();
      set(row);
      rows[0] = JsonValue(row);
      o["settings"] = JsonValue(rows);
    };
    const SchemaLoadResult unknown = load_edited([&](JsonValue::Object& o) {
      first_row(o, [](JsonValue::Object& row) { row["bogus"] = JsonValue(std::string("x")); });
    });
    XR_EXPECT_MSG(!unknown.ok && unknown.error.find("unknown field") != std::string::npos,
                  "setting row with an unknown field refused");
    const SchemaLoadResult flag = load_edited([&](JsonValue::Object& o) {
      first_row(o, [](JsonValue::Object& row) {
        row["security_relevant"] = JsonValue(std::string("yes"));
      });
    });
    XR_EXPECT_MSG(!flag.ok && flag.error.find("security_relevant must be bool") != std::string::npos,
                  "non-bool security_relevant refused");
    const SchemaLoadResult writable = load_edited([](JsonValue::Object& o) {
      o["writable_in_v0"] = JsonValue(JsonValue::Array{JsonValue(std::string("no.such.key"))});
    });
    XR_EXPECT_MSG(!writable.ok && writable.error.find("known key") != std::string::npos,
                  "writable_in_v0 entry must name a known key");
  }

  // P15-CLOSE C11: kills for the full-matrix survivors on settings_schema.cc
  // (xr-core c9c51d8, seed 20260911). Variants edit the parsed v1 document.
  {
    auto load_variant = [](auto edit, SettingsSchema* out) -> SchemaLoadResult {
      std::string err;
      JsonParseResult pr = ParseJson(ReadSchema(&err));
      JsonValue::Object obj = pr.value.as_object();
      edit(obj);
      return out->Load(JsonValue(obj).Canonical());
    };
    auto edit_row0 = [](JsonValue::Object& o, auto set) {
      JsonValue::Array rows = o.at("settings").as_array();
      JsonValue::Object row = rows[0].as_object();
      set(row);
      rows[0] = JsonValue(row);
      o["settings"] = JsonValue(rows);
    };
    // A string-typed setting: a string default loads, a bool default does not.
    SettingsSchema str_ok;
    const SchemaLoadResult s_ok = load_variant([&](JsonValue::Object& o) {
      edit_row0(o, [](JsonValue::Object& row) {
        row["type"] = JsonValue(std::string("string"));
        row["default"] = JsonValue(std::string("x"));
      });
    }, &str_ok);
    XR_EXPECT_MSG(s_ok.ok, "string setting with a string default loads: " + s_ok.error);
    if (s_ok.ok) {
      JsonValue::Object good_doc;
      good_doc["network.adblock"] = JsonValue(std::string("y"));
      XR_EXPECT(str_ok.ValidateDoc(JsonValue(good_doc)).ok);
      JsonValue::Object bad_doc;
      bad_doc["network.adblock"] = JsonValue(true);
      const DocValidateResult bad = str_ok.ValidateDoc(JsonValue(bad_doc));
      XR_EXPECT_MSG(!bad.ok && bad.error.find("expected string") != std::string::npos,
                    "a bool value for a string setting is refused");
    }
    SettingsSchema str_bad;
    const SchemaLoadResult s_bad = load_variant([&](JsonValue::Object& o) {
      edit_row0(o, [](JsonValue::Object& row) {
        row["type"] = JsonValue(std::string("string"));
        row["default"] = JsonValue(true);
      });
    }, &str_bad);
    XR_EXPECT_MSG(!s_bad.ok && s_bad.error.find("expected string") != std::string::npos,
                  "string setting with a bool default refused");
    // A section row with an unknown field is refused, naming the field.
    SettingsSchema sec_bad;
    const SchemaLoadResult sec = load_variant([](JsonValue::Object& o) {
      JsonValue::Array secs = o.at("sections").as_array();
      JsonValue::Object s0 = secs[0].as_object();
      s0["bogus"] = JsonValue(std::string("x"));
      secs[0] = JsonValue(s0);
      o["sections"] = JsonValue(secs);
    }, &sec_bad);
    XR_EXPECT_STREQ(sec.error, "section row: unknown field 'bogus'");
    // desc_id is read when present, and a non-string desc_id is refused.
    {
      SettingsSchema v1;
      std::string err;
      XR_EXPECT(v1.Load(ReadSchema(&err)).ok);
      const SettingDef* d = v1.FindSetting("network.adblock");
      XR_EXPECT(d != nullptr && !d->desc_id.empty());
    }
    SettingsSchema desc_bad;
    const SchemaLoadResult desc = load_variant([&](JsonValue::Object& o) {
      edit_row0(o, [](JsonValue::Object& row) { row["desc_id"] = JsonValue(int64_t{5}); });
    }, &desc_bad);
    XR_EXPECT_MSG(!desc.ok, "a non-string desc_id is refused");
    // A section that lists another section's setting: the exact error.
    SettingsSchema owner_bad;
    const SchemaLoadResult owner = load_variant([](JsonValue::Object& o) {
      JsonValue::Array secs = o.at("sections").as_array();
      for (size_t i = 0; i < secs.size(); ++i) {
        JsonValue::Object s = secs[i].as_object();
        if (s.at("id").as_string() != "privacy") continue;
        JsonValue::Array list = s.at("settings").as_array();
        list.push_back(JsonValue(std::string("network.adblock")));
        s["settings"] = JsonValue(list);
        secs[i] = JsonValue(s);
      }
      o["sections"] = JsonValue(secs);
    }, &owner_bad);
    XR_EXPECT_STREQ(owner.error,
                    "setting 'network.adblock' section 'network' != owning section 'privacy'");
    // A settings doc that is not an object: the exact error.
    {
      SettingsSchema v1;
      std::string err;
      XR_EXPECT(v1.Load(ReadSchema(&err)).ok);
      XR_EXPECT_STREQ(v1.ValidateDoc(JsonValue(int64_t{5})).error,
                      "settings doc must be an object (key -> value)");
    }
    // writable_in_v0 answers only for the keys it lists.
    SettingsSchema writable;
    const SchemaLoadResult w = load_variant([](JsonValue::Object& o) {
      o["writable_in_v0"] = JsonValue(JsonValue::Array{JsonValue(std::string("network.adblock"))});
    }, &writable);
    XR_EXPECT_MSG(w.ok, w.error);
    XR_EXPECT(writable.WritableInV0("network.adblock"));
    XR_EXPECT(!writable.WritableInV0("network.route"));
  }

  return xrtest::Report("test_settings_schema");
}
