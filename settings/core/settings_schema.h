// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the settings schema (Plan P8-T1). Consumes the FROZEN
// settings-schema-v1 contract WITHOUT redefinition: every user-facing
// setting has a schema entry {key, type (bool/enum/int/string), default
// (deny-safe where security-relevant), attention_tier, scope (global |
// per-identity)}; a UI toggle without a schema entry does not ship. The
// machine-readable data lives in settings_schema_v1.json (the single
// source the section registry, router anchors, search index and recall
// corpus are generated from — no hand-maintained parallel lists).
//
// Two strict validations live here:
//   1. the schema DOC itself (unknown fields/sections/keys rejected);
//   2. a user-facing settings doc (a flat key -> value map) against the
//      schema — unknown keys are rejected (strict, frozen contract), each
//      value must match the declared type/enum/int range, and a subset of
//      keys is a valid doc (deny-safe defaults fill the rest).
//
// std-only; no exceptions; typed results; never guesses.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "settings/core/json.h"

namespace xr::settings {

struct SettingDef {
  std::string key;
  std::string type;             // bool | enum | int | string
  JsonValue default_value;      // validated against type at schema load
  std::string attention_tier;   // tier0 | tier1 | tier2
  std::string scope;            // global | per-identity
  std::string section;          // owning section id (orphan-checked)
  std::string title_id;         // message id into l10n/xr_strings.grdp
  std::string desc_id;
  bool security_relevant = false;
  std::vector<std::string> aliases;  // search-only terms (never copy)
  std::string policy_kind;      // resolver | identity-list | "" (no policy)
  std::string policy_field;
};

struct SectionDef {
  std::string id;
  int order = 0;
  std::string title_id;
  std::string availability;     // always | identity.active (deny unknown)
  std::vector<std::string> settings;  // ordered row keys
};

struct SchemaLoadResult {
  bool ok = false;
  std::string error;  // cites the offending field (strict)
};

// Strict per-key type validation result for a user-facing settings doc.
struct DocValidateResult {
  bool ok = false;
  std::string error;
  std::vector<std::string> unknown_keys;  // rejected (never ignored)
  std::vector<std::string> bad_values;    // key: reason
};

class SettingsSchema {
 public:
  // Parse + strict-validate settings_schema_v1.json text.
  SchemaLoadResult Load(const std::string& json_text);

  // Strict validation of a flat user-facing doc (subset allowed; unknown
  // keys rejected; every present value must match the declared type/enum).
  DocValidateResult ValidateDoc(const JsonValue& doc) const;

  // Deny-safe default for a key (only valid after Load).
  const JsonValue* DefaultFor(const std::string& key) const;

  const SettingDef* FindSetting(const std::string& key) const;
  const SectionDef* FindSection(const std::string& id) const;

  // Sections in declared order; a section's settings in declared order.
  std::vector<const SectionDef*> Sections() const;
  std::vector<const SettingDef*> SectionSettings(const SectionDef& s) const;
  std::vector<const SettingDef*> Settings() const;  // schema order

  bool KnownKey(const std::string& key) const;
  size_t Count() const { return settings_.size(); }

  // Schema version (data; refuse unknown versions elsewhere).
  int version() const { return version_; }
  const std::string& anchor_root() const { return anchor_root_; }

  // All declared enum options for an enum key (empty when not an enum).
  std::vector<std::string> EnumOptions(const std::string& key) const;

  // Whether v0 may write this key (writable_in_v0 list in the schema data).
  bool WritableInV0(const std::string& key) const;

 private:
  int version_ = 0;
  std::string anchor_root_ = "xr://settings";
  std::vector<SettingDef> settings_;
  std::vector<SectionDef> sections_;
  std::map<std::string, size_t> by_key_;     // key -> index into settings_
  std::map<std::string, size_t> section_idx_;  // id -> index into sections_
  std::map<std::string, std::vector<std::string>> enum_options_;
  std::vector<std::string> writable_in_v0_;
};

// Validate a single value against a declared type + (for enums) options.
// ints are range-checked against a sane int32 window (rejects huge/NaN-ish).
bool ValidateSettingValue(const SettingDef& def,
                          const std::vector<std::string>& enum_options,
                          const JsonValue& v, std::string* error);

}  // namespace xr::settings
