// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Settings deep-link router (see router.h).
#include "settings/core/router.h"

#include <algorithm>

namespace xr::settings {

Router::Router(const SettingsSchema& schema) : schema_(&schema) {}

std::string Router::SectionAnchor(const std::string& section) const {
  if (schema_->FindSection(section) == nullptr) return "";
  return schema_->anchor_root() + "/" + section;
}

std::string Router::SettingAnchor(const std::string& setting) const {
  const SettingDef* def = schema_->FindSetting(setting);
  if (def == nullptr) return "";
  return schema_->anchor_root() + "/" + def->section + "/" +
         AnchorSuffix(def->section, def->key);
}

std::string Router::AnchorSuffix(const std::string& section,
                                 const std::string& key) {
  // The canonical per-setting anchor drops the "<section>." key prefix:
  // xr://settings/network/adblock (key network.adblock). When a key is not
  // prefixed by its section the full key is the suffix.
  if (key.size() > section.size() + 1 &&
      key.rfind(section + ".", 0) == 0) {
    return key.substr(section.size() + 1);
  }
  return key;
}

std::string Router::SectionOf(const std::string& setting) const {
  const SettingDef* def = schema_->FindSetting(setting);
  if (def == nullptr) return "";
  return def->section;
}

std::vector<std::string> Router::SectionAnchors() const {
  std::vector<std::string> out;
  for (const SectionDef* s : schema_->Sections()) {
    out.push_back(schema_->anchor_root() + "/" + s->id);
  }
  return out;
}

namespace {

// Split on '/' dropping empty segments; "" input => zero segments.
std::vector<std::string> SplitPath(const std::string& s) {
  std::vector<std::string> parts;
  std::string cur;
  for (const char c : s) {
    if (c == '/') {
      if (!cur.empty()) parts.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) parts.push_back(cur);
  return parts;
}

}  // namespace

ResolveResult Router::Resolve(const std::string& anchor) const {
  ResolveResult r;
  std::string path = anchor;
  // Normalize: accept full xr://settings/... or any suffix form.
  const std::string prefix = "xr://settings";
  if (path.rfind(prefix, 0) == 0) path = path.substr(prefix.size());
  if (path.rfind("/", 0) == 0) path = path.substr(1);
  const std::vector<std::string> parts = SplitPath(path);
  if (parts.empty()) {
    r.ok = true;
    r.kind = ResolveKind::kHome;
    r.canonical = schema_->anchor_root();
    return r;
  }
  const SectionDef* sec = schema_->FindSection(parts[0]);
  if (sec == nullptr) {
    r.kind = ResolveKind::kUnknown;
    r.error = "unknown section '" + parts[0] +
              "' (anchors derive from the schema — no section, no anchor)";
    for (const SectionDef* s : schema_->Sections()) {
      if (s->id.rfind(parts[0], 0) == 0 || parts[0].rfind(s->id, 0) == 0) {
        r.suggestions.push_back(schema_->anchor_root() + "/" + s->id);
      }
    }
    return r;
  }
  if (parts.size() == 1) {
    r.ok = true;
    r.kind = ResolveKind::kSection;
    r.section = sec->id;
    r.canonical = schema_->anchor_root() + "/" + sec->id;
    return r;
  }
  if (parts.size() == 2) {
    // Accept both the suffix form (network/adblock) and the full dotted key.
    const SettingDef* def = schema_->FindSetting(parts[1]);
    if ((def == nullptr || def->section != sec->id) &&
        parts[1].find('.') == std::string::npos) {
      for (const auto* cand : schema_->Settings()) {
        if (cand->section == sec->id &&
            AnchorSuffix(sec->id, cand->key) == parts[1]) {
          def = cand;
          break;
        }
      }
    }
    if (def != nullptr && def->section == sec->id) {
      r.ok = true;
      r.kind = ResolveKind::kSetting;
      r.section = sec->id;
      r.setting = def->key;
      r.canonical = schema_->anchor_root() + "/" + sec->id + "/" +
                    AnchorSuffix(sec->id, def->key);
      return r;
    }
    r.kind = ResolveKind::kUnknown;
    r.error = "unknown setting '" + parts[1] + "' in section '" + sec->id + "'";
    return r;
  }
  r.kind = ResolveKind::kUnknown;
  r.error = "anchor path too deep (xr://settings/<section>[/<setting>])";
  return r;
}

}  // namespace xr::settings
