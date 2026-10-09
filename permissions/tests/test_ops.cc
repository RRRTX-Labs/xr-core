// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P15 T1/T3/T5 — overlay operations: validation, refusals that leave the store
// byte-identical, idempotence, and invariant 7 (ledger completeness): a row is
// emitted IFF the store changed. A planted silent mutation must be caught by
// that same check, so the check is shown to have teeth.
#include <cstdint>
#include <limits>
#include <string>

#include "permissions/core/audit.h"
#include "permissions/core/ops.h"
#include "permissions/core/store.h"
#include "policy/tests/harness.h"

using namespace xr::permissions;

namespace {

const char* kA = "xr:00000000-0000-4000-8000-0000000000a1";
const char* kB = "xr:00000000-0000-4000-8000-0000000000b2";
constexpr int64_t kT0 = 1000000;

GrantRequest Req(const char* identity, const char* domain, const char* cap, const char* scope,
                 const char* session = "") {
  GrantRequest r;
  r.identity = identity;
  r.domain = domain;
  r.capability = cap;
  r.scope = scope;
  r.session_id = session;
  return r;
}

// Invariant 7 as a predicate: rows non-empty IFF the canonical bytes changed.
bool LedgerComplete(const Store& before, const OpOutcome& o) {
  const bool changed = SerializeStore(before) != SerializeStore(o.store);
  return changed == !o.rows.empty();
}

}  // namespace

int main() {
  const Store empty;
  const std::string empty_bytes = SerializeStore(empty);

  // --- SetDefault: validation refuses without touching the store --------------
  {
    OpOutcome o = SetDefault(empty, "", "camera", "kAllow", kT0);
    XR_EXPECT_MSG(!o.ok && o.error == "bad-identity", "empty identity refused");
    XR_EXPECT_MSG(SerializeStore(o.store) == empty_bytes && o.rows.empty(), "refusal leaves store unchanged");
    o = SetDefault(empty, kA, "sensors", "kAllow", kT0);
    XR_EXPECT_MSG(!o.ok && o.error == "bad-capability", "an extra is not a default capability");
    o = SetDefault(empty, kA, "camera", "kAllowAll", kT0);
    XR_EXPECT_MSG(!o.ok && o.error == "bad-state", "unknown state refused");
    Store corrupt = LoadStore("garbage");
    o = SetDefault(corrupt, kA, "camera", "kAllow", kT0);
    XR_EXPECT_MSG(!o.ok && o.error == "corrupt-store", "a corrupt store is never mutated");
    XR_EXPECT_MSG(SerializeStore(o.store) == SerializeStore(corrupt), "corrupt store stays corrupt");
  }

  // --- SetDefault: real changes emit exactly one identity-overlay row ----------
  {
    OpOutcome a = SetDefault(empty, kA, "camera", "kAllow", kT0);
    XR_EXPECT_MSG(a.ok && a.rows.size() == 1, "allow default: one row");
    XR_EXPECT_MSG(a.rows[0].event == AuditEvent::kGrant, "allow is a grant (widening)");
    XR_EXPECT_MSG(a.rows[0].deciding_layer == DecidingLayer::kIdentityOverlay, "the overlay decides");
    XR_EXPECT_MSG(a.rows[0].scope == "identity_default" && a.rows[0].ttl_millis == 0, "identity-wide default scope, no TTL");
    XR_EXPECT_MSG(LedgerComplete(empty, a), "invariant 7 on allow");

    OpOutcome same = SetDefault(a.store, kA, "camera", "kAllow", kT0 + 1);
    XR_EXPECT_MSG(same.ok && same.rows.empty(), "idempotent: same state, no row");
    XR_EXPECT_MSG(SerializeStore(same.store) == SerializeStore(a.store), "idempotent: bytes unchanged");

    OpOutcome narrowed = SetDefault(a.store, kA, "camera", "kDeny", kT0 + 2);
    XR_EXPECT_MSG(narrowed.ok && narrowed.rows.size() == 1 && narrowed.rows[0].event == AuditEvent::kRevoke,
                  "narrowing to deny is a revoke");
    XR_EXPECT_MSG(LedgerComplete(a.store, narrowed), "invariant 7 on narrowing");
  }

  // --- GrantTemp: scope rules, redaction and TTL ------------------------------
  {
    OpOutcome once = GrantTemp(empty, Req(kA, "example.com", "geolocation", "once"), kT0);
    XR_EXPECT_MSG(once.ok && once.rows.size() == 1, "once grant issued");
    XR_EXPECT_MSG(once.store.identities.at(kA).grants.at(0).id == "g1", "first id is g1");
    XR_EXPECT_MSG(once.store.next_seq == 2, "sequence advanced");
    XR_EXPECT_MSG(once.store.identities.at(kA).grants.at(0).remaining_uses == 1, "once = one use");
    XR_EXPECT_MSG(once.rows[0].ttl_millis == 0 && once.rows[0].reason == "user-grant", "once: no TTL, user-grant");
    XR_EXPECT_MSG(LedgerComplete(empty, once), "invariant 7 on once");

    OpOutcome sess = GrantTemp(empty, Req(kA, "example.com", "camera", "session", "s-42"), kT0);
    XR_EXPECT_MSG(sess.ok && sess.store.identities.at(kA).grants.at(0).session_id == "s-42", "session bound to session id");
    XR_EXPECT_MSG(!GrantTemp(empty, Req(kA, "example.com", "camera", "session", ""), kT0).ok, "session needs an id");
    XR_EXPECT_MSG(GrantTemp(empty, Req(kA, "example.com", "camera", "session", "s-42"), kT0).error.empty(), "no error on ok");
    XR_EXPECT_MSG(GrantTemp(empty, Req(kA, "example.com", "camera", "once", "s-42"), kT0).error == "bad-session",
                  "a session id on a once grant is refused");

    OpOutcome week = GrantTemp(empty, Req(kA, "example.com", "microphone", "7d"), kT0);
    XR_EXPECT_MSG(week.ok && week.store.identities.at(kA).grants.at(0).expires_at == kT0 + kSevenDaysMillis,
                  "7d expiry = created + 7 days");
    XR_EXPECT_MSG(week.rows[0].ttl_millis == kSevenDaysMillis, "7d row carries the TTL");
    XR_EXPECT_MSG(LedgerComplete(empty, week), "invariant 7 on 7d");

    XR_EXPECT_MSG(GrantTemp(empty, Req(kA, "example.com", "camera", "forever"), kT0).error == "bad-scope",
                  "permanent grants are not a scope here (Settings-only)");
    XR_EXPECT_MSG(GrantTemp(empty, Req(kA, "https://example.com/x", "camera", "once"), kT0).error == "bad-domain",
                  "a full origin is refused, never truncated");
    XR_EXPECT_MSG(GrantTemp(empty, Req(kA, "Example.com", "camera", "once"), kT0).error == "bad-domain",
                  "uppercase origin refused");
    XR_EXPECT_MSG(GrantTemp(empty, Req(kA, "example.com", "sensors", "once"), kT0).error == "bad-capability",
                  "extras cannot be granted (no slot)");
    XR_EXPECT_MSG(GrantTemp(empty, Req(kA, "example.com", "camera", "7d"), std::numeric_limits<int64_t>::max()).error == "bad-time",
                  "a 7d grant near the clock limit is refused, not wrapped");
    XR_EXPECT_MSG(GrantTemp(empty, Req(kA, "example.com", "camera", "once"), kT0).store.identities.at(kA).grants.at(0).id ==
                      GrantTemp(empty, Req(kA, "example.com", "camera", "once"), kT0).store.identities.at(kA).grants.at(0).id,
                  "ids are deterministic (same inputs, same id)");
  }

  // --- SetDenied: the Fortress deny-list, four rows, idempotent ---------------
  {
    OpOutcome on = SetDenied(empty, kA, true, kT0);
    XR_EXPECT_MSG(on.ok && on.rows.size() == 4, "deny-list on writes four rows (one per capability)");
    XR_EXPECT_MSG(on.store.identities.at(kA).denied, "flag set");
    XR_EXPECT_MSG(LedgerComplete(empty, on), "invariant 7 on deny-list");
    XR_EXPECT_MSG(SetDenied(on.store, kA, true, kT0 + 1).rows.empty(), "deny-list set twice: no rows");
    OpOutcome off = SetDenied(on.store, kA, false, kT0 + 2);
    XR_EXPECT_MSG(off.ok && off.rows.size() == 4 && !off.store.identities.at(kA).denied, "lifting the deny writes four rows");
    XR_EXPECT_MSG(SetDenied(empty, kB, false, kT0).rows.empty() && SerializeStore(SetDenied(empty, kB, false, kT0).store) == empty_bytes,
                  "lifting a deny that was never set creates nothing");
  }

  // --- ConsumeOnce: a single use is spent and removed --------------------------
  {
    OpOutcome g = GrantTemp(empty, Req(kA, "example.com", "camera", "once"), kT0);
    OpOutcome used = ConsumeOnce(g.store, "g1", kT0 + 5);
    XR_EXPECT_MSG(used.ok && used.rows.size() == 1, "first use spends the grant");
    XR_EXPECT_MSG(used.rows[0].event == AuditEvent::kExpiry && used.rows[0].reason == "used", "spent = expiry (used)");
    XR_EXPECT_MSG(used.store.identities.at(kA).grants.empty(), "spent grant is removed");
    XR_EXPECT_MSG(LedgerComplete(g.store, used), "invariant 7 on consume");
    XR_EXPECT_MSG(ConsumeOnce(used.store, "g1", kT0 + 6).error == "unknown-grant", "second use refused (no double spend)");
    XR_EXPECT_MSG(ConsumeOnce(g.store, "g9", kT0).error == "unknown-grant", "unknown id refused");
    OpOutcome week = GrantTemp(empty, Req(kA, "example.com", "camera", "7d"), kT0);
    XR_EXPECT_MSG(ConsumeOnce(week.store, "g1", kT0).error == "unknown-grant", "only a once grant can be consumed");
  }

  // --- RevokeSite: tombstones, not deletions; site-scoped ---------------------
  {
    Store s = GrantTemp(empty, Req(kA, "example.com", "camera", "7d"), kT0).store;
    s = GrantTemp(s, Req(kA, "other.example", "camera", "7d"), kT0).store;
    OpOutcome r = RevokeSite(s, kA, "example.com", kT0 + 9);
    XR_EXPECT_MSG(r.ok && r.rows.size() == 1 && r.rows[0].origin == "example.com", "revoke-site touches only its site");
    XR_EXPECT_MSG(r.rows[0].reason == "revoke_site" && r.rows[0].event == AuditEvent::kRevoke, "row: revoke, reason revoke_site");
    XR_EXPECT_MSG(SerializeStore(r.store).find("\"revoked\":true") != std::string::npos, "tombstone kept (no resurrection)");
    XR_EXPECT_MSG(ProjectViewJson(r.store).find("example.com") == std::string::npos,
                  "a tombstone is never projected");
    XR_EXPECT_MSG(ProjectViewJson(r.store).find("other.example") != std::string::npos, "other site still projected");
    XR_EXPECT_MSG(RevokeSite(r.store, kA, "example.com", kT0 + 10).rows.empty(), "revoke-site is idempotent");
    XR_EXPECT_MSG(RevokeSite(s, kA, "https://example.com", kT0).error == "bad-domain", "revoke-site refuses a full origin");
    XR_EXPECT_MSG(RevokeSite(s, kB, "example.com", kT0).rows.empty() && RevokeSite(s, kB, "example.com", kT0).ok,
                  "revoke-site for an unknown identity is a no-op");
    XR_EXPECT_MSG(LedgerComplete(s, r), "invariant 7 on revoke-site");
  }

  // --- RevokeAll: per-identity reset; never widens, never clears the deny ----
  {
    Store s = SetDefault(empty, kA, "camera", "kAllow", kT0).store;
    s = GrantTemp(s, Req(kA, "example.com", "camera", "once"), kT0).store;
    s = GrantTemp(s, Req(kA, "example.com", "microphone", "7d"), kT0).store;
    s = SetDenied(s, kA, true, kT0).store;
    s = SetDefault(s, kB, "camera", "kAllow", kT0).store;
    OpOutcome r = RevokeAll(s, kA, kT0 + 3);
    XR_EXPECT_MSG(r.ok && r.rows.size() == 3, "revoke-all: one row per default and per grant");
    for (const auto& row : r.rows) {
      XR_EXPECT_MSG(row.reason == "revoke_all" && row.deciding_layer == DecidingLayer::kGlobalFallback,
                    "revoke-all rows: reason revoke_all, global fallback decides after");
    }
    XR_EXPECT_MSG(r.store.identities.at(kA).defaults.empty(), "defaults cleared");
    XR_EXPECT_MSG(r.store.identities.at(kA).denied, "Fortress deny-list is host-owned: not cleared by revoke-all");
    XR_EXPECT_MSG(r.store.identities.at(kB).defaults.size() == 1, "revoke-all never touches another identity");
    XR_EXPECT_MSG(LedgerComplete(s, r), "invariant 7 on revoke-all");
    XR_EXPECT_MSG(RevokeAll(r.store, kA, kT0 + 4).rows.empty(), "revoke-all is idempotent");
    XR_EXPECT_MSG(RevokeAll(empty, kA, kT0).rows.empty(), "revoke-all on nothing is a no-op");
  }

  // --- invariant 7 over a scripted sequence (rows iff bytes changed) ----------
  {
    Store s;
    int checked = 0;
    auto step = [&](const OpOutcome& o, const Store& before) {
      XR_EXPECT_MSG(LedgerComplete(before, o), "invariant 7 along the scripted sequence");
      ++checked;
      return o.store;
    };
    s = step(SetDefault(s, kA, "camera", "kAllow", kT0), s);
    s = step(GrantTemp(s, Req(kA, "a.example", "geolocation", "once"), kT0), s);
    s = step(GrantTemp(s, Req(kA, "a.example", "camera", "session", "s1"), kT0), s);
    s = step(GrantTemp(s, Req(kB, "b.example", "camera", "7d"), kT0), s);
    s = step(SweepExpired(s, kT0 + 1, "s1"), s);
    s = step(SweepExpired(s, kT0 + 1, "s1"), s);            // idempotent: no rows, no change
    s = step(SweepExpired(s, kT0 + 1, "s2"), s);            // restart: session grant ends
    s = step(ConsumeOnce(s, "g2", kT0 + 2), s);
    s = step(RevokeSite(s, kB, "b.example", kT0 + 3), s);
    s = step(SetDenied(s, kB, true, kT0 + 4), s);
    s = step(RevokeAll(s, kA, kT0 + 5), s);
    XR_EXPECT_MSG(checked == 11, "every scripted step checked");
  }

  // --- the check has teeth: a planted SILENT grant must fail invariant 7 ------
  {
    OpOutcome honest = GrantTemp(empty, Req(kA, "example.com", "camera", "once"), kT0);
    OpOutcome planted = honest;
    planted.rows.clear();  // the defect: the store changed, the ledger was not told
    XR_EXPECT_MSG(!LedgerComplete(empty, planted), "a silent mutation is detected (invariant 7 has teeth)");
    XR_EXPECT_MSG(LedgerComplete(empty, honest), "the honest twin passes");
  }

  return xrtest::Report("test_ops");
}
