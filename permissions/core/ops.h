// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — pure state-machine operations on the overlay store (P15 T1,
// T3, T5). Every operation takes its clock as a PARAMETER (now_ms), reads no
// clock, no RNG and no I/O, and returns a NEW store plus the audit rows it
// produced. Invariant 7 (ledger completeness) is structural: each real
// mutation yields exactly one row; an idempotent no-op yields none, because
// nothing changed. A refused operation returns the input store unchanged.
//
// The sweep that *calls* SweepExpired with a real clock lives outside this
// core (the host), per F3 of the brief: Resolve() must stay clock-free.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "permissions/core/audit.h"
#include "permissions/core/store.h"

namespace xr::permissions {

struct OpOutcome {
  bool ok = false;
  std::string error;           // typed refusal token; "" when ok
  Store store;                 // the new store on success; the input on refusal
  std::vector<AuditRow> rows;  // exactly one row per mutation; none when refused or no-op
};

// Request for a temporary grant. Field rules are checked by GrantTemp:
// once => no session_id; session => session_id required; 7d => no session_id.
struct GrantRequest {
  std::string identity;
  std::string domain;      // refused unless it is a bare registrable domain
  std::string capability;  // geolocation | camera | microphone | notifications
  std::string scope;       // once | session | 7d
  std::string session_id;  // session scope only
};

// Refusal tokens: corrupt-store, bad-identity, bad-domain, bad-capability,
// bad-state, bad-scope, bad-session, bad-time, unknown-grant.

// Per-identity default for one of the frozen four. State names: kDeny | kAsk
// | kAllow. Event rule (documented, monotone): kAllow => grant, else revoke.
OpOutcome SetDefault(const Store& s, std::string_view identity, std::string_view capability,
                     std::string_view state, int64_t now_ms);

// Temporary grant (once / session / 7d). Ids are "g<seq>", deterministic:
// the next free sequence number, skipping any id already present.
OpOutcome GrantTemp(const Store& s, const GrantRequest& req, int64_t now_ms);

// Fortress deny-list: the HOST writes this from identity grade as data. One
// row per capability (four rows) when the flag actually changes.
OpOutcome SetDenied(const Store& s, std::string_view identity, bool denied, int64_t now_ms);

// Spends a once grant (remaining_uses -> 0) and removes it: expiry, reason used.
OpOutcome ConsumeOnce(const Store& s, std::string_view grant_id, int64_t now_ms);

// Removes grants that have ended: 7d at expires_at <= now (fail-closed at ==),
// session grants whose session_id is not active_session_id (a restart ends the
// previous session), and spent once grants. Idempotent: a second sweep with
// the same inputs yields no rows and identical bytes.
OpOutcome SweepExpired(const Store& s, int64_t now_ms, std::string_view active_session_id);

// Tombstones every live grant for (identity, domain). Revoked grants stay in
// the store with revoked=true, so a stale copy cannot revive them.
OpOutcome RevokeSite(const Store& s, std::string_view identity, std::string_view domain,
                     int64_t now_ms);

// Revoke-all for one identity: tombstones its grants and clears its per-
// capability defaults. It deliberately does NOT clear the Fortress deny-list
// (host-owned) and does NOT clear deny-only extras, so revoke-all can never
// widen an identity beyond upstream's answer.
OpOutcome RevokeAll(const Store& s, std::string_view identity, int64_t now_ms);

// Monotone evaluation clock: time never runs backward for grant evaluation.
// A backward clock jump keeps the last-seen time, so nothing un-expires.
int64_t EffectiveNow(int64_t last_seen_ms, int64_t observed_ms);

}  // namespace xr::permissions
