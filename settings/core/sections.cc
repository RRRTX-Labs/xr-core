// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Section registry + policy-state reader (see sections.h).
#include "settings/core/sections.h"

namespace xr::settings {

StateLoadResult PolicyState::Load(const std::string& json_text) {
  StateLoadResult r;
  values_.clear();
  identity_active_ = false;
  JsonParseResult p = ParseJson(json_text);
  if (!p.ok) {
    r.error = "state doc is not strict JSON: " + p.error;
    return r;
  }
  const JsonValue& doc = p.value;
  if (!doc.is_object()) { r.error = "state doc must be an object"; return r; }
  const JsonValue* schema = doc.find("schema");
  if (schema == nullptr || !schema->is_string() ||
      schema->as_string() != kSchema) {
    r.error = "state doc: schema must be 'xr-settings-state'"; return r;
  }
  const JsonValue* sv = doc.find("schema_version");
  if (sv == nullptr || !sv->is_int() || sv->as_int() != kVersion) {
    r.error = "state doc: schema_version must be 1"; return r;
  }
  version_ = kVersion;
  const JsonValue* ctx = doc.find("context");
  if (ctx != nullptr) {
    if (!ctx->is_object()) { r.error = "state doc: context must be object"; return r; }
    for (const auto& [k, _] : ctx->as_object()) {
      if (k != "identity_id" && k != "origin") {
        r.error = "state doc: context unknown field '" + k + "'"; return r;
      }
    }
    const JsonValue* id = ctx->find("identity_id");
    identity_active_ = id != nullptr && id->is_string() && !id->as_string().empty();
  }
  const JsonValue* values = doc.find("values");
  if (values == nullptr || !values->is_object()) {
    r.error = "state doc: values object required"; return r;
  }
  for (const auto& [key, row] : values->as_object()) {
    if (!row.is_object()) { r.error = "state doc: value row for '" + key + "' not object"; return r; }
    for (const auto& [k, _] : row.as_object()) {
      if (k != "value" && k != "source" && k != "managed") {
        r.error = "state doc: row '" + key + "' unknown field '" + k + "'"; return r;
      }
    }
    const JsonValue* v = row.find("value");
    if (v == nullptr) { r.error = "state doc: row '" + key + "' missing value"; return r; }
    const JsonValue* src = row.find("source");
    std::string source = "resolver";
    if (src != nullptr && src->is_string()) source = src->as_string();
    if (source != "resolver" && source != "enterprise") {
      r.error = "state doc: row '" + key + "' source must be resolver|enterprise";
      return r;
    }
    Value val;
    val.ok = true;
    val.value = *v;
    val.source = source;
    values_[key] = val;
  }
  r.ok = true;
  return r;
}

bool PolicyState::HasValue(const std::string& key) const {
  return values_.count(key) != 0;
}

PolicyState::Value PolicyState::GetValue(const std::string& key) const {
  auto it = values_.find(key);
  if (it == values_.end()) return Value{};
  return it->second;
}

bool SectionAvailable(const SectionDef& section, const PolicyState& state,
                      std::string* reason) {
  if (section.availability == "always") return true;
  if (section.availability == "identity.active") {
    if (state.identity_active()) return true;
    if (reason) *reason = "no active identity in the pinned snapshot";
    return false;
  }
  if (reason) {
    *reason = "unknown availability predicate '" + section.availability +
              "' (deny — L3)";
  }
  return false;
}

std::vector<SectionView> BuildRegistry(const SettingsSchema& schema,
                                       const PolicyState& state) {
  std::vector<SectionView> out;
  for (const SectionDef* s : schema.Sections()) {
    SectionView v;
    v.id = s->id;
    v.order = s->order;
    v.title_id = s->title_id;
    v.availability = s->availability;
    v.anchor = schema.anchor_root() + "/" + s->id;
    std::string reason;
    v.available = SectionAvailable(*s, state, &reason);
    v.unavailable_reason = v.available ? "" : reason;
    for (const SettingDef* st : schema.SectionSettings(*s)) {
      v.setting_keys.push_back(st->key);
    }
    out.push_back(std::move(v));
  }
  return out;
}

}  // namespace xr::settings
