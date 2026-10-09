// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — overlay operations (see ops.h). Each op works on a COPY of the
// input store and returns it only when every validation has passed, so a
// refusal can never leave a half-applied state behind.
#include "permissions/core/ops.h"

#include <initializer_list>
#include <limits>
#include <utility>

namespace xr::permissions {
namespace {

OpOutcome Refuse(const Store& in, const char* token) {
  OpOutcome o;
  o.ok = false;
  o.error = token;
  o.store = in;
  return o;
}

AuditRow MakeRow(AuditEvent e, std::string_view identity, Capability cap, std::string origin,
                 std::string scope, int64_t ts, int64_t ttl, std::string reason,
                 DecidingLayer layer) {
  AuditRow r;
  r.event = e;
  r.identity = std::string(identity);
  r.capability = cap;
  r.origin = std::move(origin);
  r.scope = std::move(scope);
  r.ts_millis = ts;
  r.ttl_millis = ttl;
  r.reason = std::move(reason);
  r.deciding_layer = layer;
  return r;
}

// After a revoke or expiry, the identity's own default (if any) still decides
// for that capability; otherwise the global fallback does. Labelled honestly.
DecidingLayer DecidingAfter(const IdentityRecord* rec, Capability cap) {
  if (rec != nullptr && rec->defaults.count(cap) != 0) return DecidingLayer::kIdentityOverlay;
  return DecidingLayer::kGlobalFallback;
}

const IdentityRecord* FindIdentity(const Store& s, std::string_view identity) {
  auto it = s.identities.find(std::string(identity));
  return it == s.identities.end() ? nullptr : &it->second;
}

bool IdTaken(const Store& s, const std::string& id) {
  for (const auto& [key, rec] : s.identities) {
    (void)key;
    for (const auto& g : rec.grants) {
      if (g.id == id) return true;
    }
  }
  return false;
}

}  // namespace

OpOutcome SetDefault(const Store& in, std::string_view identity, std::string_view capability,
                     std::string_view state, int64_t now_ms) {
  if (in.corrupt) return Refuse(in, "corrupt-store");
  if (identity.empty()) return Refuse(in, "bad-identity");
  Capability cap;
  if (!ParseCapability(capability, &cap)) return Refuse(in, "bad-capability");
  CapState st;
  if (!ParseCapState(state, &st)) return Refuse(in, "bad-state");

  OpOutcome o;
  o.store = in;
  o.ok = true;
  const IdentityRecord* cur = FindIdentity(in, identity);
  if (cur != nullptr) {
    auto d = cur->defaults.find(cap);
    if (d != cur->defaults.end() && d->second == st) return o;  // no change, no row
  }
  o.store.identities[std::string(identity)].defaults[cap] = st;
  const AuditEvent ev = (st == CapState::kAllow) ? AuditEvent::kGrant : AuditEvent::kRevoke;
  o.rows.push_back(MakeRow(ev, identity, cap, "", "identity_default", now_ms, 0, "user-default",
                           DecidingLayer::kIdentityOverlay));
  return o;
}

OpOutcome GrantTemp(const Store& in, const GrantRequest& req, int64_t now_ms) {
  if (in.corrupt) return Refuse(in, "corrupt-store");
  if (req.identity.empty()) return Refuse(in, "bad-identity");
  std::string origin;
  if (!RedactOrigin(req.domain, &origin)) return Refuse(in, "bad-domain");
  Capability cap;
  if (!ParseCapability(req.capability, &cap)) return Refuse(in, "bad-capability");
  Scope sc;
  if (!ParseScope(req.scope, &sc)) return Refuse(in, "bad-scope");
  if (sc == Scope::kSession && req.session_id.empty()) return Refuse(in, "bad-session");
  if (sc != Scope::kSession && !req.session_id.empty()) return Refuse(in, "bad-session");
  if (sc == Scope::k7d && now_ms > std::numeric_limits<int64_t>::max() - kSevenDaysMillis) {
    return Refuse(in, "bad-time");
  }

  OpOutcome o;
  o.store = in;
  StoredGrant g;
  g.identity = std::string(req.identity);
  g.domain = origin;
  g.capability = cap;
  g.scope = sc;
  g.created_at = now_ms;
  switch (sc) {
    case Scope::kOnce: g.remaining_uses = 1; break;
    case Scope::kSession: g.session_id = req.session_id; break;
    case Scope::k7d: g.expires_at = now_ms + kSevenDaysMillis; break;
  }
  do {
    g.id = "g" + std::to_string(o.store.next_seq);
    o.store.next_seq += 1;
  } while (IdTaken(o.store, g.id));
  o.store.identities[g.identity].grants.push_back(g);
  o.ok = true;
  o.rows.push_back(MakeRow(AuditEvent::kGrant, g.identity, cap, origin, ScopeName(sc), now_ms,
                           sc == Scope::k7d ? kSevenDaysMillis : 0, "user-grant",
                           DecidingLayer::kIdentityOverlay));
  return o;
}

OpOutcome SetDenied(const Store& in, std::string_view identity, bool denied, int64_t now_ms) {
  if (in.corrupt) return Refuse(in, "corrupt-store");
  if (identity.empty()) return Refuse(in, "bad-identity");
  OpOutcome o;
  o.store = in;
  o.ok = true;
  const IdentityRecord* cur = FindIdentity(in, identity);
  const bool prev = cur != nullptr && cur->denied;
  if (prev == denied) return o;  // no change, no rows (a false->false set creates nothing)
  o.store.identities[std::string(identity)].denied = denied;
  const AuditEvent ev = denied ? AuditEvent::kRevoke : AuditEvent::kGrant;
  for (Capability c : {Capability::kGeolocation, Capability::kCamera, Capability::kMicrophone,
                       Capability::kNotifications}) {
    o.rows.push_back(MakeRow(ev, identity, c, "", "identity_deny_list", now_ms, 0,
                             "fortress-deny-list", DecidingLayer::kIdentityOverlay));
  }
  return o;
}

OpOutcome ConsumeOnce(const Store& in, std::string_view grant_id, int64_t now_ms) {
  if (in.corrupt) return Refuse(in, "corrupt-store");
  OpOutcome o;
  o.store = in;
  for (auto& [key, rec] : o.store.identities) {
    for (auto it = rec.grants.begin(); it != rec.grants.end(); ++it) {
      if (it->revoked || it->id != grant_id) continue;
      if (it->scope != Scope::kOnce || it->remaining_uses <= 0) return Refuse(in, "unknown-grant");
      AuditRow row = MakeRow(AuditEvent::kExpiry, key, it->capability, it->domain, "once", now_ms,
                             0, "used", DecidingAfter(&rec, it->capability));
      rec.grants.erase(it);  // the single use is spent: the grant is removed
      o.rows.push_back(std::move(row));
      o.ok = true;
      return o;
    }
  }
  return Refuse(in, "unknown-grant");
}

OpOutcome SweepExpired(const Store& in, int64_t now_ms, std::string_view active_session_id) {
  if (in.corrupt) return Refuse(in, "corrupt-store");
  OpOutcome o;
  o.store = in;
  o.ok = true;
  for (auto& [key, rec] : o.store.identities) {
    std::vector<StoredGrant> kept;
    kept.reserve(rec.grants.size());
    for (auto& g : rec.grants) {
      if (g.revoked) {  // tombstones are never expiry rows
        kept.push_back(std::move(g));
        continue;
      }
      const char* reason = nullptr;
      if (g.scope == Scope::kOnce && g.remaining_uses <= 0) {
        reason = "used";
      } else if (g.scope == Scope::k7d && g.expires_at <= now_ms) {  // fail-closed at ==
        reason = "ttl";
      } else if (g.scope == Scope::kSession && g.session_id != active_session_id) {
        reason = "session_end";
      }
      if (reason == nullptr) {
        kept.push_back(std::move(g));
        continue;
      }
      o.rows.push_back(MakeRow(AuditEvent::kExpiry, key, g.capability, g.domain, ScopeName(g.scope),
                               now_ms, g.scope == Scope::k7d ? kSevenDaysMillis : 0, reason,
                               DecidingAfter(&rec, g.capability)));
    }
    rec.grants = std::move(kept);
  }
  return o;
}

OpOutcome RevokeSite(const Store& in, std::string_view identity, std::string_view domain,
                     int64_t now_ms) {
  if (in.corrupt) return Refuse(in, "corrupt-store");
  if (identity.empty()) return Refuse(in, "bad-identity");
  std::string origin;
  if (!RedactOrigin(domain, &origin)) return Refuse(in, "bad-domain");
  OpOutcome o;
  o.store = in;
  o.ok = true;
  auto it = o.store.identities.find(std::string(identity));
  if (it == o.store.identities.end()) return o;  // nothing to revoke
  IdentityRecord& rec = it->second;
  for (auto& g : rec.grants) {
    if (g.revoked || g.domain != origin) continue;
    g.revoked = true;
    g.revoked_at = now_ms;
    o.rows.push_back(MakeRow(AuditEvent::kRevoke, identity, g.capability, origin, ScopeName(g.scope),
                             now_ms, g.scope == Scope::k7d ? kSevenDaysMillis : 0, "revoke_site",
                             DecidingAfter(&rec, g.capability)));
  }
  return o;
}

OpOutcome RevokeAll(const Store& in, std::string_view identity, int64_t now_ms) {
  if (in.corrupt) return Refuse(in, "corrupt-store");
  if (identity.empty()) return Refuse(in, "bad-identity");
  OpOutcome o;
  o.store = in;
  o.ok = true;
  auto it = o.store.identities.find(std::string(identity));
  if (it == o.store.identities.end()) return o;
  IdentityRecord& rec = it->second;
  // Defaults first, so every row written below reads the post-clear layer.
  for (auto d = rec.defaults.begin(); d != rec.defaults.end();) {
    o.rows.push_back(MakeRow(AuditEvent::kRevoke, identity, d->first, "", "identity_default", now_ms,
                             0, "revoke_all", DecidingLayer::kGlobalFallback));
    d = rec.defaults.erase(d);
  }
  for (auto& g : rec.grants) {
    if (g.revoked) continue;
    g.revoked = true;
    g.revoked_at = now_ms;
    o.rows.push_back(MakeRow(AuditEvent::kRevoke, identity, g.capability, g.domain, ScopeName(g.scope),
                             now_ms, g.scope == Scope::k7d ? kSevenDaysMillis : 0, "revoke_all",
                             DecidingLayer::kGlobalFallback));
  }
  return o;
}

int64_t EffectiveNow(int64_t last_seen_ms, int64_t observed_ms) {
  return observed_ms > last_seen_ms ? observed_ms : last_seen_ms;
}

}  // namespace xr::permissions
