// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the SESSION STORE carrying the identity binding (P14-T8,
// the pure-core half): what a crash/quit persists, what restore may do,
// and the one law that makes restore safe — RESTORE NEVER BLEEDS.
//
// The session document (the store's serialized shape, v1):
//   {schema, schema_version:1, tabs:[{tab_id, domain, window}]}
// Laws under test (tests/test_session_chaos.cc):
//   * restore re-binds each tab to ITS RECORDED domain — never another
//     identity's, never a silent fallback to the default partition (the
//     C-14 census row's failure mode, made impossible here);
//   * a tampered/stale session (tab bound to a domain that is not in the
//     live identity set) is REFUSED with kMalformedInput naming the tab —
//     restore never guesses (the P4 timing finding's spirit: a fallback
//     that merges identities is worse than a refusal);
//   * DISPOSABLES ARE NEVER RESTORED: in-memory identities are absent from
//     the restored set by construction (their tabs are dropped, and the
//     drop is REPORTED, not silent) — close ⇒ zero bytes, and a crash
//     must not resurrect them either (the §1.4/T6 law);
//   * the CHAOS test (P14-T8's own): seeded kill-points mid-sequence —
//     destroy/hibernate/activate interleaved with snapshot+restore —
//     assert the restored state equals the pre-kill state exactly for
//     durable identities, and zero residue for disposables.
// The browser halves (real session files, kill -9 of real processes) are
// NOT-RUN here — the method is P9-T11's drill (docs/qa/browser-harness.md).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/binding.h"
#include "core/identity.h"

namespace xr::identity {

struct SessionTab {
  uint64_t tab_id = 0;
  std::string domain;   // the recorded binding (opaque domain)
  std::string window;
};

struct SessionDoc {
  std::vector<SessionTab> tabs;
  // Durable (non-disposable) identities alive at snapshot time.
  std::vector<std::string> durable_domains;
};

// Serialize (the canonical wire shape; sorted by tab_id for determinism).
std::string SerializeSession(const SessionDoc& doc);

// Parse + validate against the LIVE identity set. `store` supplies the
// live identities; `dropped` receives the disposables' tabs (reported).
// Returns nullopt + error when a tab names a domain that is not live —
// restore never guesses, never falls back to the default partition.
struct SessionRestoreResult {
  bool ok = false;
  std::string error;                 // kMalformedInput (…tab NNN…) on tamper
  SessionDoc doc;                    // the validated doc
  std::vector<uint64_t> dropped_tabs;  // disposables' tabs (never restored)
};
SessionRestoreResult RestoreSession(const std::string& wire,
                                    const IdentityStore& store,
                                    const BindingModel& binding);

// Snapshot the CURRENT live state (what a quit would persist): every
// bound tab whose identity is durable. Disposable-bound tabs are NOT
// written (zero residue by construction) and are returned in `dropped`.
SessionDoc Snapshot(const IdentityStore& store, const BindingModel& binding,
                    std::vector<uint64_t>* dropped);

}  // namespace xr::identity
