// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — EffectivePolicy (de)serialization (see effective_policy.h).
#include "policy/core/effective_policy.h"

namespace xr::policy {

const char* ToString(PermissionState v) {
  switch (v) {
    case PermissionState::kDeny: return "kDeny";
    case PermissionState::kAsk: return "kAsk";
    case PermissionState::kAllow: return "kAllow";
  }
  return "kDeny";
}
const char* ToString(RouteClass v) {
  switch (v) {
    case RouteClass::kDirect: return "kDirect";
    case RouteClass::kProxy: return "kProxy";
    case RouteClass::kWireguard: return "kWireguard";
    case RouteClass::kTor: return "kTor";
  }
  return "kDirect";
}
const char* ToString(FingerprintMode v) {
  switch (v) {
    case FingerprintMode::kOff: return "kOff";
    case FingerprintMode::kReduce: return "kReduce";
    case FingerprintMode::kStrict: return "kStrict";
  }
  return "kStrict";
}
const char* ToString(StorageScope v) {
  switch (v) {
    case StorageScope::kEphemeral: return "kEphemeral";
    case StorageScope::kIdentityScoped: return "kIdentityScoped";
    case StorageScope::kFortressPartition: return "kFortressPartition";
  }
  return "kEphemeral";
}

std::optional<PermissionState> PermissionStateFromString(const std::string& s) {
  if (s == "kDeny") return PermissionState::kDeny;
  if (s == "kAsk") return PermissionState::kAsk;
  if (s == "kAllow") return PermissionState::kAllow;
  return std::nullopt;
}
std::optional<RouteClass> RouteClassFromString(const std::string& s) {
  if (s == "kDirect") return RouteClass::kDirect;
  if (s == "kProxy") return RouteClass::kProxy;
  if (s == "kWireguard") return RouteClass::kWireguard;
  if (s == "kTor") return RouteClass::kTor;
  return std::nullopt;
}
std::optional<FingerprintMode> FingerprintModeFromString(const std::string& s) {
  if (s == "kOff") return FingerprintMode::kOff;
  if (s == "kReduce") return FingerprintMode::kReduce;
  if (s == "kStrict") return FingerprintMode::kStrict;
  return std::nullopt;
}
std::optional<StorageScope> StorageScopeFromString(const std::string& s) {
  if (s == "kEphemeral") return StorageScope::kEphemeral;
  if (s == "kIdentityScoped") return StorageScope::kIdentityScoped;
  if (s == "kFortressPartition") return StorageScope::kFortressPartition;
  return std::nullopt;
}

bool EffectivePolicy::operator==(const EffectivePolicy& o) const {
  return contract_version == o.contract_version &&
         block_network_ads == o.block_network_ads && block_trackers == o.block_trackers &&
         upgrade_to_https == o.upgrade_to_https && hide_cosmetic == o.hide_cosmetic &&
         block_scriptlets == o.block_scriptlets && geolocation == o.geolocation &&
         camera == o.camera && microphone == o.microphone &&
         notifications == o.notifications && block_third_party == o.block_third_party &&
         route == o.route && fingerprint_mode == o.fingerprint_mode &&
         storage_scope == o.storage_scope && in_memory == o.in_memory &&
         autofill_allowed == o.autofill_allowed && export_allowed == o.export_allowed &&
         site_isolated == o.site_isolated && dedicated_process == o.dedicated_process &&
         letterbox == o.letterbox;
}

JsonValue EffectivePolicy::ToJson() const {
  JsonValue::Object version;
  version.emplace("contract_version", JsonValue(static_cast<int64_t>(contract_version)));

  JsonValue::Object blocking;
  blocking.emplace("block_network_ads", JsonValue(block_network_ads));
  blocking.emplace("block_trackers", JsonValue(block_trackers));
  blocking.emplace("upgrade_to_https", JsonValue(upgrade_to_https));

  JsonValue::Object cosmetic;
  cosmetic.emplace("hide_cosmetic", JsonValue(hide_cosmetic));
  cosmetic.emplace("block_scriptlets", JsonValue(block_scriptlets));

  JsonValue::Object permissions;
  permissions.emplace("geolocation", JsonValue(std::string(ToString(geolocation))));
  permissions.emplace("camera", JsonValue(std::string(ToString(camera))));
  permissions.emplace("microphone", JsonValue(std::string(ToString(microphone))));
  permissions.emplace("notifications", JsonValue(std::string(ToString(notifications))));

  JsonValue::Object egress;
  egress.emplace("block_third_party", JsonValue(block_third_party));
  egress.emplace("route", JsonValue(std::string(ToString(route))));

  JsonValue::Object fingerprint;
  fingerprint.emplace("mode", JsonValue(std::string(ToString(fingerprint_mode))));

  JsonValue::Object storage;
  storage.emplace("scope", JsonValue(std::string(ToString(storage_scope))));
  storage.emplace("in_memory", JsonValue(in_memory));

  JsonValue::Object vault;
  vault.emplace("autofill_allowed", JsonValue(autofill_allowed));
  vault.emplace("export_allowed", JsonValue(export_allowed));

  JsonValue::Object process;
  process.emplace("site_isolated", JsonValue(site_isolated));
  process.emplace("dedicated_process", JsonValue(dedicated_process));

  JsonValue::Object root;
  root.emplace("version", JsonValue(std::move(version)));
  root.emplace("blocking", JsonValue(std::move(blocking)));
  root.emplace("cosmetic", JsonValue(std::move(cosmetic)));
  root.emplace("permissions", JsonValue(std::move(permissions)));
  root.emplace("egress", JsonValue(std::move(egress)));
  root.emplace("fingerprint", JsonValue(std::move(fingerprint)));
  root.emplace("storage_scope", JsonValue(std::move(storage)));
  root.emplace("vault_scope", JsonValue(std::move(vault)));
  root.emplace("process_policy", JsonValue(std::move(process)));
  root.emplace("letterbox", JsonValue(letterbox));
  return JsonValue(std::move(root));
}

namespace {

// Strict helpers: every one returns false (deny) on any deviation.
bool GetBool(const JsonValue::Object& o, const char* key, bool* out) {
  auto it = o.find(key);
  if (it == o.end() || !it->second.is_bool()) return false;
  *out = it->second.as_bool();
  return true;
}
bool GetInt(const JsonValue::Object& o, const char* key, int64_t* out) {
  auto it = o.find(key);
  if (it == o.end() || !it->second.is_int()) return false;
  *out = it->second.as_int();
  return true;
}
bool GetString(const JsonValue::Object& o, const char* key, std::string* out) {
  auto it = o.find(key);
  if (it == o.end() || !it->second.is_string()) return false;
  *out = it->second.as_string();
  return true;
}
template <typename T>
bool GetEnum(const JsonValue::Object& o, const char* key,
             std::optional<T> (*parse)(const std::string&), T* out) {
  std::string s;
  if (!GetString(o, key, &s)) return false;
  std::optional<T> v = parse(s);
  if (!v) return false;
  *out = *v;
  return true;
}

}  // namespace

std::optional<EffectivePolicy> EffectivePolicy::FromJson(const JsonValue& v) {
  if (!v.is_object()) return std::nullopt;
  EffectivePolicy p;
  const JsonValue::Object& root = v.as_object();

  const JsonValue* version = nullptr;
  const JsonValue* blocking = nullptr;
  const JsonValue* cosmetic = nullptr;
  const JsonValue* permissions = nullptr;
  const JsonValue* egress = nullptr;
  const JsonValue* fingerprint = nullptr;
  const JsonValue* storage = nullptr;
  const JsonValue* vault = nullptr;
  const JsonValue* process = nullptr;
  // additionalProperties: false — exactly the 10 frozen keys.
  if (root.size() != 10) return std::nullopt;
  if ((version = v.find("version")) == nullptr || !version->is_object() ||
      (blocking = v.find("blocking")) == nullptr || !blocking->is_object() ||
      (cosmetic = v.find("cosmetic")) == nullptr || !cosmetic->is_object() ||
      (permissions = v.find("permissions")) == nullptr || !permissions->is_object() ||
      (egress = v.find("egress")) == nullptr || !egress->is_object() ||
      (fingerprint = v.find("fingerprint")) == nullptr || !fingerprint->is_object() ||
      (storage = v.find("storage_scope")) == nullptr || !storage->is_object() ||
      (vault = v.find("vault_scope")) == nullptr || !vault->is_object() ||
      (process = v.find("process_policy")) == nullptr || !process->is_object()) {
    return std::nullopt;
  }
  if (v.find("letterbox") == nullptr || !v.find("letterbox")->is_bool()) return std::nullopt;
  p.letterbox = v.find("letterbox")->as_bool();

  const JsonValue::Object& vo = version->as_object();
  int64_t cv = 0;
  if (vo.size() != 1 || !GetInt(vo, "contract_version", &cv) || cv != 1) return std::nullopt;
  p.contract_version = static_cast<int>(cv);

  const JsonValue::Object& bo = blocking->as_object();
  if (bo.size() != 3 || !GetBool(bo, "block_network_ads", &p.block_network_ads) ||
      !GetBool(bo, "block_trackers", &p.block_trackers) ||
      !GetBool(bo, "upgrade_to_https", &p.upgrade_to_https)) {
    return std::nullopt;
  }
  const JsonValue::Object& co = cosmetic->as_object();
  if (co.size() != 2 || !GetBool(co, "hide_cosmetic", &p.hide_cosmetic) ||
      !GetBool(co, "block_scriptlets", &p.block_scriptlets)) {
    return std::nullopt;
  }
  const JsonValue::Object& po = permissions->as_object();
  if (po.size() != 4 || !GetEnum(po, "geolocation", &PermissionStateFromString, &p.geolocation) ||
      !GetEnum(po, "camera", &PermissionStateFromString, &p.camera) ||
      !GetEnum(po, "microphone", &PermissionStateFromString, &p.microphone) ||
      !GetEnum(po, "notifications", &PermissionStateFromString, &p.notifications)) {
    return std::nullopt;
  }
  const JsonValue::Object& eo = egress->as_object();
  if (eo.size() != 2 || !GetBool(eo, "block_third_party", &p.block_third_party) ||
      !GetEnum(eo, "route", &RouteClassFromString, &p.route)) {
    return std::nullopt;
  }
  const JsonValue::Object& fo = fingerprint->as_object();
  if (fo.size() != 1 || !GetEnum(fo, "mode", &FingerprintModeFromString, &p.fingerprint_mode)) {
    return std::nullopt;
  }
  const JsonValue::Object& so = storage->as_object();
  if (so.size() != 2 || !GetEnum(so, "scope", &StorageScopeFromString, &p.storage_scope) ||
      !GetBool(so, "in_memory", &p.in_memory)) {
    return std::nullopt;
  }
  const JsonValue::Object& uo = vault->as_object();
  if (uo.size() != 2 || !GetBool(uo, "autofill_allowed", &p.autofill_allowed) ||
      !GetBool(uo, "export_allowed", &p.export_allowed)) {
    return std::nullopt;
  }
  // Schema law: export_allowed is const_false in v1. A "true" is a schema
  // violation, not a policy statement — reject (never widen).
  if (p.export_allowed) return std::nullopt;

  const JsonValue::Object& ro = process->as_object();
  if (ro.size() != 2 || !GetBool(ro, "site_isolated", &p.site_isolated) ||
      !GetBool(ro, "dedicated_process", &p.dedicated_process)) {
    return std::nullopt;
  }
  return p;
}

}  // namespace xr::policy
