// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Command descriptor validation against the FROZEN command-descriptor-v1
// schema. See descriptor.h for the law. The field set and enums below are a
// transcription of the frozen schema; tests load the actual .schema.json and
// assert these constants match it (so "implement to it, never redefine it" is
// mechanically proven, not a habit).
#include "commands/core/descriptor.h"

#include <algorithm>
#include <set>

namespace xr::commands {
namespace {

bool InList(const std::vector<std::string>& list, const std::string& v) {
  return std::find(list.begin(), list.end(), v) != list.end();
}

}  // namespace

const std::vector<std::string> kAttentionTiers = {"tier0", "tier1", "tier2"};
const std::vector<std::string> kDangerClasses = {"safe", "caution", "destructive"};
const std::vector<std::string> kRequiredFields = {
    "id", "title", "attention_tier", "danger_class", "surface", "handler"};

DescriptorResult ValidateDescriptor(const JsonValue& obj) {
  DescriptorResult r;
  if (!obj.is_object()) {
    r.error = "descriptor must be a JSON object (frozen schema: type=object)";
    return r;
  }
  const auto& o = obj.as_object();

  // additionalProperties: false — any field outside the frozen set is a reject.
  for (const auto& [k, v] : o) {
    if (!InList(kRequiredFields, k)) {
      r.error = "unknown field '" + k +
                "' rejected: frozen command-descriptor-v1 has "
                "additionalProperties:false (new fields register via "
                "registry-post-freeze.md, not here)";
      return r;
    }
  }

  DescriptorFields f;
  for (const auto& field : kRequiredFields) {
    const JsonValue* v = obj.find(field);
    if (v == nullptr) {
      r.error = "missing required field '" + field + "'";
      return r;
    }
    if (!v->is_string() || v->as_string().empty()) {
      r.error = "field '" + field + "' must be a non-empty string";
      return r;
    }
  }
  f.id = obj.find("id")->as_string();
  f.title = obj.find("title")->as_string();
  f.attention_tier = obj.find("attention_tier")->as_string();
  f.danger_class = obj.find("danger_class")->as_string();
  f.surface = obj.find("surface")->as_string();
  f.handler = obj.find("handler")->as_string();

  if (!InList(kAttentionTiers, f.attention_tier)) {
    r.error = "attention_tier '" + f.attention_tier +
              "' not in frozen enum {tier0,tier1,tier2}";
    return r;
  }
  if (!InList(kDangerClasses, f.danger_class)) {
    r.error = "danger_class '" + f.danger_class +
              "' not in frozen enum {safe,caution,destructive}";
    return r;
  }

  r.ok = true;
  r.fields = f;
  return r;
}

}  // namespace xr::commands
