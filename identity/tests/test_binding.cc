// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// binding suite (P14-T3): default-identity-per-window; the destructive move
// (after-nav refused with the reason; never a silent reload); groups cannot
// span identities (enforced HERE, in the move logic, not in the UI); and
// the planted auto-switch — a suggestion that changes a binding MUST fail.
#include <map>
#include <string>

#include "harness.h"
#include "core/binding.h"
#include "core/session.h"

using xr::identity::BindingModel;
using xr::identity::IdentityStore;
using xr::identity::CallResult;
using xr::identity::ChangeCause;

namespace {
const char* kA = "xr:00000000-0000-4000-8000-000000000001";
const char* kB = "xr:00000000-0000-4000-8000-000000000002";
const char* kC = "xr:00000000-0000-4000-8000-00000000000ef";
}  // namespace

int main() {
  BindingModel b;

  // 1. Default identity per window; tabs are born at the window default.
  XR_EXPECT(b.SetWindowDefault("win-1", kA).ok);
  XR_EXPECT(b.SetWindowDefault("win-2", kB).ok);
  XR_EXPECT(b.OpenTab("win-1", 101, "").ok);
  XR_EXPECT(b.OpenTab("win-2", 202, "").ok);
  XR_EXPECT_STREQ(b.TabIdentity(101)->c_str(), kA);
  XR_EXPECT_STREQ(b.TabIdentity(202)->c_str(), kB);
  // An opener may pass an explicit domain (right-click "Open tab in
  // identity…"): the tab is born in the chosen identity, recorded.
  XR_EXPECT(b.OpenTab("win-1", 103, kC).ok);
  XR_EXPECT_STREQ(b.TabIdentity(103)->c_str(), kC);
  // A window with no default refuses to open tabs (fail-closed).
  XR_EXPECT_MSG(!b.OpenTab("win-x", 1, "").ok,
                "window without default refuses");
  // Malformed domains are refused everywhere.
  XR_EXPECT_MSG(!b.SetWindowDefault("w", "not-a-domain").ok,
                "malformed default refused");

  // 2. The destructive law: moving a tab that ALREADY NAVIGATED is refused
  // with the confirm+reload reason; the binding is NOT changed.
  XR_EXPECT(b.OpenTab("win-1", 105, "").ok);
  CallResult mv = b.MoveTab(kA, kB, 105, /*after_nav=*/true, {});
  XR_EXPECT_MSG(!mv.ok, "after-nav move refused");
  XR_EXPECT_MSG(mv.error.find("confirm + reload") != std::string::npos,
                "refusal names the ceremony");
  XR_EXPECT_STREQ(b.TabIdentity(105)->c_str(), kA);  // unchanged
  // The pre-navigation move succeeds (the UI confirmed; this call IS the
  // confirmed action) and is audited with the user-confirmed cause.
  XR_EXPECT_MSG(b.MoveTab(kA, kB, 105, false, {}).ok, "pre-nav move ok");
  XR_EXPECT_STREQ(b.TabIdentity(105)->c_str(), kB);
  XR_EXPECT_MSG(!b.changes().empty() && b.changes().back().tab_id == 105 &&
                    b.changes().back().from == kA && b.changes().back().to == kB,
                "the audit row records where the tab came FROM, not just where it went");
  bool saw_confirmed = false;
  for (const auto& ch : b.changes()) {
    saw_confirmed |= ch.tab_id == 105 && ch.cause == ChangeCause::kUserConfirmedMove;
  }
  XR_EXPECT_MSG(saw_confirmed, "confirmed move audited");

  // 3. Groups cannot span identities: a move that would split the group is
  // refused HERE and says WHICH peer blocks it. `group_peers` carries each
  // peer's domain AFTER the pending move (the UI moves a group as one
  // confirmed action, naming the group's target).
  XR_EXPECT(b.OpenTab("win-1", 201, "").ok);   // in kA
  XR_EXPECT(b.OpenTab("win-1", 202, "").ok);   // in kA (group {201,202})
  CallResult split = b.MoveTab(kA, kB, 201, false, {{202, kA}});
  XR_EXPECT_MSG(!split.ok, "group split refused (peer stays behind)");
  XR_EXPECT_MSG(split.error.find("peer 202") != std::string::npos,
                "refusal names the peer");
  XR_EXPECT_MSG(split.error.find("another identity") != std::string::npos,
                "refusal names the law");
  XR_EXPECT_STREQ(b.TabIdentity(201)->c_str(), kA);  // unchanged
  // Moving the WHOLE group together (peers named at the target) is fine.
  XR_EXPECT(b.MoveTab(kA, kB, 201, false, {{202, kB}}).ok);
  XR_EXPECT(b.MoveTab(kA, kB, 202, false, {{201, kB}}).ok);
  XR_EXPECT_STREQ(b.TabIdentity(201)->c_str(), kB);
  XR_EXPECT_STREQ(b.TabIdentity(202)->c_str(), kB);
  // A lone tab moves freely — including to the frozen 40-char domain.
  XR_EXPECT(b.OpenTab("win-1", 203, "").ok);
  XR_EXPECT(b.MoveTab(kA, kC, 203, false, {}).ok);
  XR_EXPECT_STREQ(b.TabIdentity(203)->c_str(), kC);

  // 4. Suggestion NEVER switches: recorded + surfaced, binding unchanged.
  XR_EXPECT(b.OpenTab("win-2", 301, "").ok);   // in kB
  CallResult sg = b.RecordSuggestion("mail.example", kA);
  XR_EXPECT_MSG(sg.ok, "suggestion recorded");
  XR_EXPECT_EQ(b.suggestions().size(), size_t(1));
  XR_EXPECT_MSG(b.suggestions()[0].surfaced, "suggestion is surfaced");
  XR_EXPECT_STREQ(b.TabIdentity(301)->c_str(), kB);  // nothing switched
  bool any_suggestion_cause = false;
  for (const auto& ch : b.changes()) {
    any_suggestion_cause |= ch.cause == ChangeCause::kSuggestion;
  }
  XR_EXPECT_MSG(!any_suggestion_cause,
                "audit trail contains NO suggestion-caused change");

  // 5. THE PLANTED AUTO-SWITCH (the locked "never auto-switch" law): force
  // the forbidden path; it must fail with kNotPermitted.
  CallResult forced = b.TestPlantedAutoSwitch(301, kA);
  XR_EXPECT_MSG(!forced.ok, "planted auto-switch FAILS");
  XR_EXPECT_MSG(forced.error.find("never auto-switch") != std::string::npos,
                "refusal names the law");
  XR_EXPECT_STREQ(b.TabIdentity(301)->c_str(), kB);  // still unchanged
  any_suggestion_cause = false;
  for (const auto& ch : b.changes()) {
    any_suggestion_cause |= ch.cause == ChangeCause::kSuggestion;
  }
  XR_EXPECT_MSG(!any_suggestion_cause,
                "audit trail STILL contains no suggestion-caused change");

  // 6. Restore (session store, T8): records with the restore cause; the
  // restored binding is what the session said, and it is audited.
  XR_EXPECT(b.RestoreTab(900, kC).ok);
  XR_EXPECT_STREQ(b.TabIdentity(900)->c_str(), kC);
  bool saw_restore = false;
  for (const auto& ch : b.changes()) {
    saw_restore |= ch.tab_id == 900 && ch.cause == ChangeCause::kRestore;
  }
  XR_EXPECT_MSG(saw_restore, "restore audited with its cause");

  // 7. Move from a mismatched identity is refused (the caller's view of
  // `from` must match the binding — no blind writes).
  XR_EXPECT_MSG(!b.MoveTab(kA, kB, 900, false, {}).ok,
                "mismatched from-domain refused");

  // 11. COMPACT AUDIT (P14 bounded-campaign memory): the audit keeps the
  // LATEST row per tab — exactly what a session Snapshot folds — so
  // compaction is contract-preserving by construction; these cases prove
  // it rather than trust it.
  {
    BindingModel c;
    XR_EXPECT(c.SetWindowDefault("w", kA).ok);
    XR_EXPECT(c.OpenTab("w", 7, "").ok);            // tab 7 -> A
    XR_EXPECT(c.OpenTab("w", 9, "").ok);            // tab 9 -> A
    XR_EXPECT(c.MoveTab(kA, kB, 9, false, {}).ok);  // tab 9 -> B (move)
    XR_EXPECT(c.MoveTab(kB, kC, 9, false, {}).ok);  // tab 9 -> C (move)
    XR_EXPECT(c.RestoreTab(7, kC).ok);              // tab 7 -> C (restore)
    XR_EXPECT_EQ(c.changes().size(), size_t(5));
    // The pre-compaction session (what a crash would snapshot).
    const std::string before =
        xr::identity::SerializeSession(
            xr::identity::Snapshot(IdentityStore(), c,
                                   nullptr));
    const size_t dropped = c.CompactAudit(2);
    XR_EXPECT_EQ(dropped, size_t(3));               // 5 rows -> 2 tabs
    XR_EXPECT_EQ(c.changes().size(), size_t(2));
    // Ascending tab order, latest binding per tab, cause preserved.
    XR_EXPECT_EQ(c.changes()[0].tab_id, uint64_t(7));
    XR_EXPECT_STREQ(c.changes()[0].to.c_str(), kC);
    XR_EXPECT(c.changes()[0].cause == ChangeCause::kRestore);
    XR_EXPECT_EQ(c.changes()[1].tab_id, uint64_t(9));
    XR_EXPECT_STREQ(c.changes()[1].to.c_str(), kC);
    XR_EXPECT(c.changes()[1].cause == ChangeCause::kUserConfirmedMove);
    XR_EXPECT_STREQ(c.TabIdentity(7)->c_str(), kC);
    XR_EXPECT_STREQ(c.TabIdentity(9)->c_str(), kC);
    // THE CONTRACT: the compacted session is byte-identical.
    const std::string after =
        xr::identity::SerializeSession(
            xr::identity::Snapshot(IdentityStore(), c,
                                   nullptr));
    XR_EXPECT_MSG(before == after,
                  "compaction preserves the Snapshot bytes");
    // Never-auto-switch survives compaction (nothing may carry the
    // suggestion cause).
    for (const auto& ch : c.changes()) {
      XR_EXPECT(ch.cause != ChangeCause::kSuggestion);
    }
    // Suggestions keep their most recent entries only.
    for (int i = 0; i < 4; ++i) {
      XR_EXPECT(c.RecordSuggestion("s" + std::to_string(i), kA).ok);
    }
    (void)c.CompactAudit(2);
    XR_EXPECT_EQ(c.suggestions().size(), size_t(2));
    XR_EXPECT_STREQ(c.suggestions()[0].site.c_str(), "s2");
    XR_EXPECT_STREQ(c.suggestions()[1].site.c_str(), "s3");
  }

  return xrtest::Report("identity/binding");
}
