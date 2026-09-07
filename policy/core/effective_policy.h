// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — EffectivePolicy v1 value type, mirroring the FROZEN contract
// docs/contracts/effective-policy-v1.schema.json (xr-browser). The
// default-constructed object IS the fully-denying policy (deny-safe
// defaults; there is no "unset => allow" state). Serialization is canonical
// JSON (sorted keys, byte-stable). Deserialization is schema-strict:
// missing/extra/wrong-typed/unknown-enum values => nullopt (the caller
// denies); export_allowed=true is rejected (schema const_false).
#pragma once

#include <optional>
#include <string>

#include "policy/core/json.h"

namespace xr::policy {

enum class PermissionState { kDeny, kAsk, kAllow };
enum class RouteClass { kDirect, kProxy, kWireguard, kTor };
enum class FingerprintMode { kOff, kReduce, kStrict };
enum class StorageScope { kEphemeral, kIdentityScoped, kFortressPartition };

const char* ToString(PermissionState v);
const char* ToString(RouteClass v);
const char* ToString(FingerprintMode v);
const char* ToString(StorageScope v);
std::optional<PermissionState> PermissionStateFromString(const std::string& s);
std::optional<RouteClass> RouteClassFromString(const std::string& s);
std::optional<FingerprintMode> FingerprintModeFromString(const std::string& s);
std::optional<StorageScope> StorageScopeFromString(const std::string& s);

struct EffectivePolicy {
  // Deny-safe defaults == _deny_policy() in the frozen Python fake.
  int contract_version = 1;
  bool block_network_ads = true;
  bool block_trackers = true;
  bool upgrade_to_https = true;
  bool hide_cosmetic = true;
  bool block_scriptlets = true;
  PermissionState geolocation = PermissionState::kDeny;
  PermissionState camera = PermissionState::kDeny;
  PermissionState microphone = PermissionState::kDeny;
  PermissionState notifications = PermissionState::kDeny;
  bool block_third_party = true;
  RouteClass route = RouteClass::kDirect;
  FingerprintMode fingerprint_mode = FingerprintMode::kStrict;
  StorageScope storage_scope = StorageScope::kEphemeral;
  bool in_memory = true;
  bool autofill_allowed = false;
  bool export_allowed = false;  // schema: const false in v1
  bool site_isolated = true;
  bool dedicated_process = true;
  bool letterbox = true;

  bool operator==(const EffectivePolicy& o) const;
  bool operator!=(const EffectivePolicy& o) const { return !(*this == o); }

  // Canonical JSON exactly matching the frozen fake's dict shape.
  JsonValue ToJson() const;
  std::string ToCanonicalJson() const { return ToJson().Canonical(); }

  // Schema-strict parse (additionalProperties:false everywhere). Any
  // deviation => nullopt. Never throws, never guesses.
  static std::optional<EffectivePolicy> FromJson(const JsonValue& v);
};

}  // namespace xr::policy
