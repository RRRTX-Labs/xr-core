// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — vocabulary tables for the permissions core (see types.h).
// Unknown names are REFUSED (returns false), never mapped to a default.
#include "permissions/core/types.h"

namespace xr::permissions {

const char* CapStateName(CapState s) {
  switch (s) {
    case CapState::kDeny: return "kDeny";
    case CapState::kAsk: return "kAsk";
    case CapState::kAllow: return "kAllow";
  }
  return "kDeny";  // unreachable; deny-safe
}

bool ParseCapState(std::string_view s, CapState* out) {
  if (s == "kDeny") { *out = CapState::kDeny; return true; }
  if (s == "kAsk") { *out = CapState::kAsk; return true; }
  if (s == "kAllow") { *out = CapState::kAllow; return true; }
  return false;
}

const char* CapabilityName(Capability c) {
  switch (c) {
    case Capability::kGeolocation: return "geolocation";
    case Capability::kCamera: return "camera";
    case Capability::kMicrophone: return "microphone";
    case Capability::kNotifications: return "notifications";
  }
  return "notifications";  // unreachable; never used to widen anything
}

bool ParseCapability(std::string_view s, Capability* out) {
  if (s == "geolocation") { *out = Capability::kGeolocation; return true; }
  if (s == "camera") { *out = Capability::kCamera; return true; }
  if (s == "microphone") { *out = Capability::kMicrophone; return true; }
  if (s == "notifications") { *out = Capability::kNotifications; return true; }
  return false;
}

bool IsSensitive(Capability c) {
  return c == Capability::kGeolocation || c == Capability::kCamera ||
         c == Capability::kMicrophone;
}

const char* ScopeName(Scope s) {
  switch (s) {
    case Scope::kOnce: return "once";
    case Scope::kSession: return "session";
    case Scope::k7d: return "7d";
  }
  return "once";
}

bool ParseScope(std::string_view s, Scope* out) {
  if (s == "once") { *out = Scope::kOnce; return true; }
  if (s == "session") { *out = Scope::kSession; return true; }
  if (s == "7d") { *out = Scope::k7d; return true; }
  return false;
}

CapState MostRestrictive(CapState a, CapState b) {
  return static_cast<int>(a) <= static_cast<int>(b) ? a : b;
}

}  // namespace xr::permissions
