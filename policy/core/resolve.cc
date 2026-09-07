// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — resolver implementation (see resolve.h). The tier table is
// the fake's truth table, transcribed exactly; deviations are documented in
// the research log and are deny-safe only.
#include "policy/core/resolve.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace xr::policy {
namespace {

}  // namespace

int TrustTierIndex(const std::string& t) {
  if (t == "kStandard") return 0;
  if (t == "kShield") return 1;
  if (t == "kFortress") return 2;
  return -1;
}

namespace {


// _tiered_policy from the frozen fake, transcribed.
EffectivePolicy TieredPolicy(int t, bool ephemeral, bool fortress_id,
                             const std::string& request_class) {
  EffectivePolicy p;
  // blocking: all true in every tier (deny defaults already true).
  p.hide_cosmetic = t >= 1;
  p.block_scriptlets = t >= 1;
  const PermissionState def = (t == 0) ? PermissionState::kAsk : PermissionState::kDeny;
  p.geolocation = def;
  p.camera = def;
  p.microphone = def;
  p.notifications = (t <= 1) ? PermissionState::kAsk : PermissionState::kDeny;
  p.block_third_party = t >= 1;
  static constexpr std::array<RouteClass, 3> kRoute = {
      RouteClass::kDirect, RouteClass::kProxy, RouteClass::kTor};
  p.route = kRoute[static_cast<size_t>(t)];
  p.fingerprint_mode = (t == 2) ? FingerprintMode::kStrict : FingerprintMode::kReduce;
  if (ephemeral) {
    p.storage_scope = StorageScope::kEphemeral;
    p.in_memory = true;
  } else if (fortress_id || t == 2) {
    p.storage_scope = StorageScope::kFortressPartition;
    p.in_memory = false;
  } else {
    p.storage_scope = StorageScope::kIdentityScoped;
    p.in_memory = false;
  }
  p.autofill_allowed =
      (request_class == "kNavigation" || request_class == "kPermission");
  p.export_allowed = false;
  p.site_isolated = true;
  p.dedicated_process = fortress_id || t == 2;
  p.letterbox = (t == 2);
  return p;
}

// Exception activity: pure evaluation against caller-supplied now/session.
bool ExceptionActive(const ExceptionEntry& e, const ResolveRequest& req) {
  if (e.scope == "once") return e.remaining_uses > 0;
  if (e.scope == "session") return !e.session_id.empty() && e.session_id == req.session_id;
  if (e.scope == "7d") return e.expires_at > req.now_ms;  // fail-closed at ==
  if (e.scope == "permanent") return e.granted_by == "settings";  // Settings-only
  return false;
}

bool ExceptionMatches(const ExceptionEntry& e, const std::string& domain,
                      const std::string& identity) {
  return e.domain == domain && (e.identity.empty() || e.identity == identity);
}


// Deterministic winner: highest created_at, then lexicographically greatest
// id (documented tie-break; identical inputs => identical choice).
template <typename T>
bool Prefer(const T& a, const T& b) {
  if (a.created_at != b.created_at) return a.created_at > b.created_at;
  if constexpr (std::is_same_v<T, ExceptionEntry>) {
    return a.id > b.id;
  } else {
    return a.identity > b.identity;
  }
}

}  // namespace

ResolveOutput Resolve(const ResolveRequest& req) {
  ResolveOutput out;
  if (!req.has_request) {
    out.ok = true;  // total: deny policy, not an error
    out.policy = EffectivePolicy();
    return out;
  }
  if (req.version_mismatch) {
    out.ok = false;  // the single error arm, deny-safe (surfaced to caller)
    out.error = "kVersionMismatch";
    return out;
  }
  if (!req.core_valid) {
    out.ok = true;
    out.policy = EffectivePolicy();  // fully denying
    return out;
  }

  // ---- tier selection (precedence ladder, documented in policy.md) ----
  int t = 0;
  bool from_explicit = req.has_trust;
  if (from_explicit) {
    t = TrustTierIndex(req.trust);  // parse guarantees validity; -1 impossible
  } else {
    // 1) active exception (in-flow, expiring) — deterministic winner
    const ExceptionEntry* win_ex = nullptr;
    for (const auto& e : req.exceptions) {
      if (!ExceptionMatches(e, req.registrable_domain, req.identity)) continue;
      if (!ExceptionActive(e, req)) continue;
      if (win_ex == nullptr || Prefer(e, *win_ex)) win_ex = &e;
    }
    // 2) identity-specific binding (⌥), 3) site default binding
    const TrustBinding* win_b = nullptr;
    for (const auto& b : req.bindings) {
      if (b.domain != req.registrable_domain) continue;
      if (!b.identity.empty() && b.identity != req.identity) continue;
      if (win_b == nullptr || Prefer(b, *win_b)) win_b = &b;
    }
    if (win_ex != nullptr) {
      t = TrustTierIndex(win_ex->trust);
    } else if (win_b != nullptr) {
      t = TrustTierIndex(win_b->trust);
    }
  }
  if (t < 0) t = 2;  // unreachable by parse; deny-safe guard (never guess)

  // ---- enterprise layer: floor clamps UP only; forces are final ----
  if (req.enterprise.present) {
    int floor = TrustTierIndex(req.enterprise.trust_floor);
    if (floor > t) t = floor;
  }

  // ---- identity attributes ----
  bool ephemeral = false, fortress_id = false;
  for (const auto& id : req.identities) {
    if (id.value == req.identity) {
      ephemeral = id.ephemeral;
      fortress_id = id.fortress;
      break;
    }
  }

  EffectivePolicy p = TieredPolicy(t, ephemeral, fortress_id, req.request_class);

  if (req.enterprise.present) {
    if (!req.enterprise.force_fingerprint.empty()) {
      auto m = FingerprintModeFromString(req.enterprise.force_fingerprint);
      if (m) p.fingerprint_mode = *m;
    }
    if (!req.enterprise.force_route.empty()) {
      auto r = RouteClassFromString(req.enterprise.force_route);
      if (r) p.route = *r;
    }
    if (req.enterprise.force_letterbox) p.letterbox = true;
    if (req.enterprise.force_block_third_party) p.block_third_party = true;
  }

  // Extension context never widens: autofill mediation is off (Guard
  // dispatch itself is P21; this is the conservative v1 input rule).
  if (req.extension.present) p.autofill_allowed = false;

  out.ok = true;
  out.policy = p;
  return out;
}

JsonValue ResolveOutputToJson(const ResolveOutput& out) {
  if (!out.ok) {
    JsonValue::Object o;
    o.emplace("error", JsonValue(out.error));
    return JsonValue(std::move(o));
  }
  JsonValue::Object o;
  o.emplace("ok", out.policy.ToJson());
  return JsonValue(std::move(o));
}

std::string ResolveOutputToCanonicalJson(const ResolveOutput& out) {
  return ResolveOutputToJson(out).Canonical();
}

}  // namespace xr::policy
