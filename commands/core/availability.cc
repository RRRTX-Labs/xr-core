// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Availability predicate VM-lite. See availability.h for the pinned-snapshot
// law. Each data-id is a pure read of the snapshot; unknown => deny-default.
#include "commands/core/availability.h"

#include <algorithm>

namespace xr::commands {
namespace {

AvailabilityVerdict Deny(const std::string& reason) {
  return AvailabilityVerdict{false, reason};
}
AvailabilityVerdict Allow() { return AvailabilityVerdict{true, ""}; }

}  // namespace

const std::vector<std::string>& Availability::Registered() {
  static const std::vector<std::string> kIds = {
      "always",
      "policy.trust-dial-writable",
      "tor.engine-ready",
      "identity.active",
      "build.channel-dev",
  };
  return kIds;
}

bool Availability::KnownPredicate(const std::string& predicate_id) const {
  return std::find(Registered().begin(), Registered().end(), predicate_id) !=
         Registered().end();
}

AvailabilityVerdict Availability::Evaluate(
    const std::string& predicate_id, const JsonValue& snapshot) const {
  if (predicate_id == "always") {
    return Allow();
  }

  if (predicate_id == "policy.trust-dial-writable") {
    const JsonValue* dw = snapshot.find("dial_writable");
    if (dw && dw->is_bool() && dw->as_bool()) return Allow();
    return Deny("trust dial is not writable for this scope (resolver snapshot)");
  }

  if (predicate_id == "tor.engine-ready") {
    const JsonValue* caps = snapshot.find("capabilities");
    if (caps && caps->is_array()) {
      for (const auto& c : caps->as_array())
        if (c.is_string() && c.as_string() == "tor.engine") return Allow();
    }
    // The Tor/VPN engine lands in P31; until then this command is registered,
    // disabled WITH a reason — never absent, never a "coming soon" rail.
    return Deny("Tor engine not wired (P31) — placeholder, disabled");
  }

  if (predicate_id == "identity.active") {
    const JsonValue* ai = snapshot.find("active_identity");
    if (ai && ai->is_string() && !ai->as_string().empty()) return Allow();
    return Deny("no active identity");
  }

  if (predicate_id == "build.channel-dev") {
    // P11-T6: the xr://shield debug page is a DEV-build surface. The
    // snapshot's capabilities carry the build channel; anywhere else the
    // command stays registered, disabled WITH a reason (never absent).
    // The host-side gate (shield_host --build-channel) is the enforcement;
    // this predicate is the registry's honest visibility of it.
    const JsonValue* caps = snapshot.find("capabilities");
    if (caps && caps->is_array()) {
      for (const auto& c : caps->as_array())
        if (c.is_string() && c.as_string() == "build.channel-dev")
          return Allow();
    }
    return Deny("build channel is not dev (capabilities snapshot)");
  }

  // Deny-default: an unknown predicate id is never a guess (L3).
  return Deny("unknown predicate '" + predicate_id + "' (deny-default)");
}

}  // namespace xr::commands
