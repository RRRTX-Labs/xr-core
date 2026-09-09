// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Settings schema implementation (strict; see settings_schema.h).
#include "settings/core/settings_schema.h"

#include <cstdint>

namespace xr::settings {
namespace {

constexpr int64_t kIntMin = -2147483648ll;   // int32 window (sane ints only)
constexpr int64_t kIntMax = 2147483647ll;

const JsonValue* GetStr(const JsonValue& obj, const char* key,
                        std::string* out, std::string* error,
                        const std::string& ctx) {
  const JsonValue* v = obj.find(key);
  if (v == nullptr) {
    *error = ctx + ": missing '" + key + "'";
    return nullptr;
  }
  if (!v->is_string()) {
    *error = ctx + ": '" + key + "' must be a string";
    return nullptr;
  }
  *out = v->as_string();
  return v;
}

}  // namespace

bool ValidateSettingValue(const SettingDef& def,
                          const std::vector<std::string>& enum_options,
                          const JsonValue& v, std::string* error) {
  if (def.type == "bool") {
    if (!v.is_bool()) { *error = "expected bool"; return false; }
    return true;
  }
  if (def.type == "string") {
    if (!v.is_string()) { *error = "expected string"; return false; }
    return true;
  }
  if (def.type == "int") {
    if (!v.is_int() || v.as_int() < kIntMin || v.as_int() > kIntMax) {
      *error = "expected int in int32 range"; return false;
    }
    return true;
  }
  if (def.type == "enum") {
    if (!v.is_string()) { *error = "expected enum string"; return false; }
    for (const auto& o : enum_options) {
      if (v.as_string() == o) return true;
    }
    *error = "value not in enum options";
    return false;
  }
  *error = "unknown declared type '" + def.type + "'";
  return false;
}

SchemaLoadResult SettingsSchema::Load(const std::string& json_text) {
  SchemaLoadResult r;
  JsonParseResult p = ParseJson(json_text);
  if (!p.ok) {
    r.error = "schema file is not strict JSON: " + p.error;
    return r;
  }
  const JsonValue& doc = p.value;
  if (!doc.is_object()) { r.error = "schema doc must be an object"; return r; }
  const JsonValue* sv = doc.find("schema_version");
  if (sv == nullptr || !sv->is_int()) {
    r.error = "schema doc missing integer schema_version";
    return r;
  }
  version_ = static_cast<int>(sv->as_int());

  const JsonValue* ar = doc.find("anchor_root");
  if (ar != nullptr && ar->is_string()) anchor_root_ = ar->as_string();

  // Strict row keys allowed on a SettingDef object (unknown => rejected).
  static const char* kSettingFields[] = {
      "key", "type", "default", "attention_tier", "scope", "section",
      "title_id", "desc_id", "security_relevant", "aliases", "policy", "writable"};
  static const char* kPolicyFields[] = {"kind", "field"};
  static const char* kSectionFields[] = {
      "id", "order", "title_id", "availability", "subsections", "settings"};

  // --- sections ----------------------------------------------------------
  const JsonValue* sections = doc.find("sections");
  if (sections == nullptr || !sections->is_array()) {
    r.error = "schema doc missing sections array"; return r;
  }
  for (const auto& s : sections->as_array()) {
    if (!s.is_object()) { r.error = "section row not an object"; return r; }
    for (const auto& [k, _] : s.as_object()) {
      bool known = false;
      for (const char* f : kSectionFields) known = known || (k == f);
      if (!known) { r.error = "section row: unknown field '" + k + "'"; return r; }
    }
    SectionDef def;
    std::string err;
    const JsonValue* v = GetStr(s, "id", &def.id, &err, "section");
    if (v == nullptr) { r.error = err; return r; }
    if (!GetStr(s, "title_id", &def.title_id, &err, "section " + def.id)) {
      r.error = err; return r;
    }
    def.availability = "always";
    const JsonValue* av = s.find("availability");
    if (av != nullptr && av->is_string()) def.availability = av->as_string();
    const JsonValue* order = s.find("order");
    if (order != nullptr) {
      if (!order->is_int()) { r.error = "section order must be int"; return r; }
      def.order = static_cast<int>(order->as_int());
    }
    const JsonValue* rows = s.find("settings");
    if (rows != nullptr && rows->is_array()) {
      for (const auto& k : rows->as_array()) {
        if (!k.is_string()) { r.error = "section settings must be strings"; return r; }
        def.settings.push_back(k.as_string());
      }
    }
    if (section_idx_.count(def.id)) {
      r.error = "duplicate section id '" + def.id + "'"; return r;
    }
    section_idx_[def.id] = sections_.size();
    sections_.push_back(std::move(def));
  }

  // --- settings ----------------------------------------------------------
  const JsonValue* rows = doc.find("settings");
  if (rows == nullptr || !rows->is_array()) {
    r.error = "schema doc missing settings array"; return r;
  }
  for (const auto& row : rows->as_array()) {
    if (!row.is_object()) { r.error = "setting row not an object"; return r; }
    for (const auto& [k, _] : row.as_object()) {
      bool known = false;
      for (const char* f : kSettingFields) known = known || (k == f);
      if (!known) { r.error = "setting row: unknown field '" + k + "'"; return r; }
    }
    SettingDef def;
    std::string err;
    const JsonValue* v = GetStr(row, "key", &def.key, &err, "setting");
    if (v == nullptr) { r.error = err; return r; }
    if (!GetStr(row, "type", &def.type, &err, def.key)) { r.error = err; return r; }
    if (!GetStr(row, "attention_tier", &def.attention_tier, &err, def.key)) {
      r.error = err; return r;
    }
    if (!GetStr(row, "scope", &def.scope, &err, def.key)) { r.error = err; return r; }
    if (!GetStr(row, "section", &def.section, &err, def.key)) { r.error = err; return r; }
    if (!GetStr(row, "title_id", &def.title_id, &err, def.key)) { r.error = err; return r; }
    if (row.find("desc_id") != nullptr &&
        !GetStr(row, "desc_id", &def.desc_id, &err, def.key)) {
      r.error = err; return r;
    }
    const JsonValue* sec = row.find("security_relevant");
    if (sec != nullptr && !sec->is_bool()) {
      r.error = def.key + ": security_relevant must be bool"; return r;
    }
    def.security_relevant = sec != nullptr && sec->is_bool() && sec->as_bool();
    const JsonValue* aliases = row.find("aliases");
    if (aliases != nullptr) {
      if (!aliases->is_array()) { r.error = def.key + ": aliases must be array"; return r; }
      for (const auto& a : aliases->as_array()) {
        if (!a.is_string()) { r.error = def.key + ": alias not a string"; return r; }
        def.aliases.push_back(a.as_string());
      }
    }
    const JsonValue* pol = row.find("policy");
    if (pol != nullptr) {
      if (!pol->is_object()) { r.error = def.key + ": policy must be object"; return r; }
      for (const auto& [k, _] : pol->as_object()) {
        bool known = false;
        for (const char* f : kPolicyFields) known = known || (k == f);
        if (!known) { r.error = def.key + ": policy unknown field '" + k + "'"; return r; }
      }
      if (!GetStr(*pol, "kind", &def.policy_kind, &err, def.key + ".policy")) {
        r.error = err; return r;
      }
      if (!GetStr(*pol, "field", &def.policy_field, &err, def.key + ".policy")) {
        r.error = err; return r;
      }
    }
    const JsonValue* dv = row.find("default");
    if (dv == nullptr) { r.error = def.key + ": missing default (deny-safe law)"; return r; }
    std::vector<std::string> opts;
    const JsonValue* enums = doc.find("enum_options");
    if (enums != nullptr && enums->is_object()) {
      const JsonValue* e = enums->find(def.key);
      if (e != nullptr) {
        if (!e->is_array()) { r.error = def.key + ": enum_options not array"; return r; }
        for (const auto& o : e->as_array()) {
          if (!o.is_string()) { r.error = def.key + ": enum option not string"; return r; }
          opts.push_back(o.as_string());
        }
      }
    }
    enum_options_[def.key] = opts;
    if (!ValidateSettingValue(def, opts, *dv, &err)) {
      r.error = def.key + ": default invalid — " + err; return r;
    }
    def.default_value = *dv;
    if (by_key_.count(def.key)) {
      r.error = "duplicate setting key '" + def.key + "'"; return r;
    }
    by_key_[def.key] = settings_.size();
    settings_.push_back(std::move(def));
  }

  // --- orphan detection: every section ref resolves, every setting's
  // section exists and owns it (the registry is DATA; no orphans). --------
  for (auto& s : sections_) {
    for (const auto& k : s.settings) {
      if (!by_key_.count(k)) {
        r.error = "section '" + s.id + "' references unknown setting '" + k + "'";
        return r;
      }
      SettingDef& def = settings_[by_key_[k]];
      if (def.section != s.id) {
        r.error = "setting '" + k + "' section '" + def.section +
                  "' != owning section '" + s.id + "'";
        return r;
      }
    }
  }
  for (const auto& def : settings_) {
    if (!section_idx_.count(def.section)) {
      r.error = "setting '" + def.key + "' has unknown section '" + def.section + "'";
      return r;
    }
  }
  const JsonValue* wl = doc.find("writable_in_v0");
  if (wl != nullptr) {
    if (!wl->is_array()) { r.error = "writable_in_v0 must be array"; return r; }
    for (const auto& k : wl->as_array()) {
      if (!k.is_string() || !by_key_.count(k.as_string())) {
        r.error = "writable_in_v0 entry must be a known key"; return r;
      }
      writable_in_v0_.push_back(k.as_string());
    }
  }
  r.ok = true;
  return r;
}

DocValidateResult SettingsSchema::ValidateDoc(const JsonValue& doc) const {
  DocValidateResult r;
  if (!doc.is_object()) {
    r.error = "settings doc must be an object (key -> value)";
    return r;
  }
  std::map<std::string, std::string> reasons;
  for (const auto& [k, v] : doc.as_object()) {
    auto it = by_key_.find(k);
    if (it == by_key_.end()) {
      r.unknown_keys.push_back(k);
      continue;
    }
    const SettingDef& def = settings_[it->second];
    std::string err;
    auto eo = enum_options_.find(k);
    if (!ValidateSettingValue(def, eo == enum_options_.end()
                                     ? std::vector<std::string>{}
                                     : eo->second, v, &err)) {
      r.bad_values.push_back(k + ": " + err);
    }
  }
  r.ok = r.unknown_keys.empty() && r.bad_values.empty();
  if (!r.ok) {
    r.error = "settings doc rejected (strict)";
    for (const auto& k : r.unknown_keys) r.error += "; unknown key '" + k + "'";
    for (const auto& b : r.bad_values) r.error += "; " + b;
  }
  return r;
}

const JsonValue* SettingsSchema::DefaultFor(const std::string& key) const {
  auto it = by_key_.find(key);
  if (it == by_key_.end()) return nullptr;
  return &settings_[it->second].default_value;
}

const SettingDef* SettingsSchema::FindSetting(const std::string& key) const {
  auto it = by_key_.find(key);
  if (it == by_key_.end()) return nullptr;
  return &settings_[it->second];
}

const SectionDef* SettingsSchema::FindSection(const std::string& id) const {
  auto it = section_idx_.find(id);
  if (it == section_idx_.end()) return nullptr;
  return &sections_[it->second];
}

std::vector<const SectionDef*> SettingsSchema::Sections() const {
  std::vector<const SectionDef*> out;
  for (const auto& s : sections_) out.push_back(&s);
  return out;
}

std::vector<const SettingDef*> SettingsSchema::SectionSettings(
    const SectionDef& s) const {
  std::vector<const SettingDef*> out;
  for (const auto& k : s.settings) out.push_back(&settings_[by_key_.at(k)]);
  return out;
}

std::vector<const SettingDef*> SettingsSchema::Settings() const {
  std::vector<const SettingDef*> out;
  for (const auto& s : settings_) out.push_back(&s);
  return out;
}

bool SettingsSchema::KnownKey(const std::string& key) const {
  return by_key_.count(key) != 0;
}

std::vector<std::string> SettingsSchema::EnumOptions(
    const std::string& key) const {
  auto it = enum_options_.find(key);
  if (it == enum_options_.end()) return {};
  return it->second;
}

bool SettingsSchema::WritableInV0(const std::string& key) const {
  for (const auto& k : writable_in_v0_) if (k == key) return true;
  return false;
}

}  // namespace xr::settings
