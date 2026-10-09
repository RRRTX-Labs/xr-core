// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the permissions core's vocabulary (P15, ADR-0051).
// The four capabilities are the FROZEN mojom PermissionPolicy fields
// (xr-core/mojom/policy_resolver.mojom); they are the only capabilities that
// have a PermissionState slot. The extras (sensors, midi, clipboard-read-write,
// pics) are deny-only data (envelope.h) and have no output slot anywhere.
// No trust-tier vocabulary appears in this core: tiers are the resolver's
// alone (policy/mode_lint.cfg, L3). Every function here is pure.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace xr::permissions {

// Same ORDER as the frozen PermissionState (kDeny=0 < kAsk=1 < kAllow=2), so
// "more restrictive" is "smaller".
enum class CapState { kDeny = 0, kAsk = 1, kAllow = 2 };

// The frozen four. Order matches PermissionPolicy's field order.
enum class Capability { kGeolocation = 0, kCamera = 1, kMicrophone = 2, kNotifications = 3 };

// Exception-style scopes, mirroring policy/core/resolve.h ExceptionEntry.
enum class Scope { kOnce = 0, kSession = 1, k7d = 2 };

inline constexpr int64_t kMillisPerDay = 86400000LL;
inline constexpr int64_t kSevenDaysMillis = 7 * kMillisPerDay;

const char* CapStateName(CapState s);  // "kDeny" | "kAsk" | "kAllow"
bool ParseCapState(std::string_view s, CapState* out);

const char* CapabilityName(Capability c);  // "geolocation" | "camera" | ...
bool ParseCapability(std::string_view s, Capability* out);

// Sensitive = a prompt that reaches physical sensors. Drives the one-time-first
// button order (plan line 808) and the identity-scope copy (plan line 809).
bool IsSensitive(Capability c);

const char* ScopeName(Scope s);  // "once" | "session" | "7d"
bool ParseScope(std::string_view s, Scope* out);

// Narrowing = taking the lower of two states (deny < ask < allow).
CapState MostRestrictive(CapState a, CapState b);

}  // namespace xr::permissions
