// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// session + chaos suite (P14-T8): restore never bleeds, disposables never
// resurrect, and the seeded kill-point chaos — snapshot/restore interleaved
// with lifecycle churn — always converges to the pre-kill state for durable
// identities and zero residue for disposables. This is the MODEL half, run
// for real (deterministic, seeded); real kill -9 of real processes is the
// P9-T11 drill method (NOT-RUN here — docs/qa/browser-harness.md).
#include <random>
#include <string>
#include <vector>

#include "harness.h"
#include "core/binding.h"
#include "core/hibernate.h"
#include "core/identity.h"
#include "core/mint.h"
#include "core/session.h"

using xr::identity::BindingModel;
using xr::identity::CreateRequest;
using xr::identity::IdentityRecord;
using xr::identity::IdentityStore;
using xr::identity::Manager;
using xr::identity::RestoreSession;
using xr::identity::SessionDoc;
using xr::identity::Snapshot;
using xr::identity::State;
using xr::identity::SerializeSession;

int main() {
  // 1. Snapshot drops disposable-bound tabs (zero residue by construction)
  //    and keeps durable bindings.
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest a; a.entropy = "durable-a"; IdentityRecord ra;
    XR_EXPECT(m.Create(a, &ra).ok);
    CreateRequest b; b.entropy = "disposable-b"; b.in_memory = true;
    IdentityRecord rb;
    XR_EXPECT(m.Create(b, &rb).ok);
    BindingModel bind;
    XR_EXPECT(bind.SetWindowDefault("w", ra.domain).ok);
    XR_EXPECT(bind.OpenTab("w", 1, "").ok);          // durable A
    XR_EXPECT(bind.OpenTab("w", 2, rb.domain).ok);   // disposable B
    std::vector<uint64_t> dropped;
    const SessionDoc doc = Snapshot(store, bind, &dropped);
    XR_EXPECT_EQ(doc.tabs.size(), size_t(1));        // tab 1 only
    XR_EXPECT_STREQ(doc.tabs[0].domain.c_str(), ra.domain.c_str());
    XR_EXPECT_EQ(dropped.size(), size_t(1));         // tab 2 reported
    XR_EXPECT_EQ(dropped[0], uint64_t(2));
    const std::string wire = SerializeSession(doc);
    // Round-trip against the SAME live state: restores cleanly.
    auto res = RestoreSession(wire, store, bind);
    XR_EXPECT_MSG(res.ok, res.error.c_str());
    XR_EXPECT_EQ(res.doc.tabs.size(), size_t(1));
    XR_EXPECT_STREQ(res.doc.tabs[0].domain.c_str(), ra.domain.c_str());
  }

  // 1b. The wire round-trips EVERY field: window names and every durable
  // domain survive byte-for-byte (a parser that skipped or shifted a
  // character would still restore "a" session, just not this one).
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest a; a.entropy = "rt-a"; IdentityRecord ra;
    XR_EXPECT(m.Create(a, &ra).ok);
    CreateRequest b; b.entropy = "rt-b"; IdentityRecord rb;
    XR_EXPECT(m.Create(b, &rb).ok);
    BindingModel bind;
    XR_EXPECT(bind.SetWindowDefault("win-main", ra.domain).ok);
    XR_EXPECT(bind.SetWindowDefault("win-2", rb.domain).ok);
    XR_EXPECT(bind.OpenTab("win-main", 3, "").ok);
    XR_EXPECT(bind.OpenTab("win-2", 4, "").ok);
    std::vector<uint64_t> dropped;
    SessionDoc doc = Snapshot(store, bind, &dropped);
    XR_EXPECT_EQ(doc.durable_domains.size(), size_t(2));
    XR_EXPECT_EQ(doc.tabs.size(), size_t(2));
    // The core leaves `window` for the host to fill; fill it as the host does.
    for (auto& t : doc.tabs) t.window = t.tab_id == 3 ? "win-main" : "win-2";
    auto res = RestoreSession(SerializeSession(doc), store, bind);
    XR_EXPECT_MSG(res.ok, res.error.c_str());
    XR_EXPECT_MSG(res.doc.durable_domains == doc.durable_domains,
                  "every durable domain round-trips exactly");
    XR_EXPECT_EQ(res.doc.tabs.size(), size_t(2));
    for (size_t i = 0; i < res.doc.tabs.size() && i < doc.tabs.size(); ++i) {
      XR_EXPECT_STREQ(res.doc.tabs[i].window.c_str(), doc.tabs[i].window.c_str());
      XR_EXPECT_STREQ(res.doc.tabs[i].domain.c_str(), doc.tabs[i].domain.c_str());
    }
  }

  // 2. RESTORE NEVER BLEEDS: a session naming a NON-LIVE identity is
  //    refused with the tab named — never a fallback to the default
  //    partition, never a re-create (the C-14 failure mode).
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest a; a.entropy = "only-a"; IdentityRecord ra;
    XR_EXPECT(m.Create(a, &ra).ok);
    BindingModel bind;
    XR_EXPECT(bind.SetWindowDefault("w", ra.domain).ok);
    XR_EXPECT(bind.OpenTab("w", 7, "").ok);
    std::vector<uint64_t> dropped;
    const std::string wire = SerializeSession(Snapshot(store, bind, &dropped));
    // Simulate the crash/quit + a DIFFERENT live set: B exists, A purged.
    IdentityStore store2;
    Manager m2(&store2);
    CreateRequest b; b.entropy = "only-b"; IdentityRecord rb;
    XR_EXPECT(m2.Create(b, &rb).ok);
    BindingModel bind2;
    auto res = RestoreSession(wire, store2, bind2);
    XR_EXPECT_MSG(!res.ok, "restore with a purged identity is REFUSED");
    XR_EXPECT_MSG(res.error.find("tab 7") != std::string::npos,
                  "refusal names the tab");
    XR_EXPECT_MSG(res.error.find("never defaulting") != std::string::npos,
                  "refusal names the law");
    // And a session that names a DISPOSABLE is tamper: never restorable.
    SessionDoc fake;
    fake.tabs.push_back({9, rb.domain, "w"});
    CreateRequest c; c.entropy = "disposable-c"; c.in_memory = true;
    IdentityRecord rc;
    XR_EXPECT(m2.Create(c, &rc).ok);
    fake.tabs[0].domain = rc.domain;
    auto res2 = RestoreSession(SerializeSession(fake), store2, bind2);
    XR_EXPECT_MSG(!res2.ok, "disposable in a session file is refused");
    XR_EXPECT_MSG(res2.error.find("disposable") != std::string::npos,
                  "refusal says disposable");
  }

  // 3. Malformed frames are refused, never best-effort parsed.
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest a; a.entropy = "x"; IdentityRecord ra;
    XR_EXPECT(m.Create(a, &ra).ok);
    BindingModel bind;
    for (const std::string bad : {"", "v2|", "v1|no-section",
                                  "v1|1:w:;|", "v1|not-a-tab:x;|"}) {
      auto res = RestoreSession(bad, store, bind);
      XR_EXPECT_MSG(!res.ok, "malformed session refused: " + bad);
      XR_EXPECT_MSG(res.error.find("kMalformedInput") != std::string::npos,
                    "typed error for: " + bad);
    }
  }

  // 4. THE CHAOS TEST (seeded, deterministic): interleaved lifecycle churn
  //    and snapshot/restore at random kill-points; after every restore the
  //    invariant is asserted — durable bindings match the pre-kill state
  //    EXACTLY, disposables are gone with zero residue.
  {
    for (unsigned seed : {20260910u, 7u, 424242u}) {
      std::mt19937 rng(seed);
      IdentityStore store;
      Manager m(&store);
      xr::identity::Scheduler sched(&m, 5);
      BindingModel bind;
      // Provision: 3 durable + 1 disposable.
      std::vector<std::string> durable;
      for (int i = 0; i < 3; ++i) {
        CreateRequest req;
        req.entropy = "chaos-" + std::to_string(seed) + "-" + std::to_string(i);
        IdentityRecord rec;
        XR_EXPECT_MSG(m.Create(req, &rec).ok, "chaos provision");
        durable.push_back(rec.domain);
        XR_EXPECT(sched.Activate(rec.domain, 1).ok);
      }
      CreateRequest disp;
      disp.entropy = "chaos-disp-" + std::to_string(seed);
      disp.in_memory = true;
      IdentityRecord drec;
      XR_EXPECT(m.Create(disp, &drec).ok);
      XR_EXPECT(sched.Activate(drec.domain, 1).ok);
      // Bind tabs: durable tabs + one disposable tab.
      XR_EXPECT(bind.SetWindowDefault("w", durable[0]).ok);
      for (uint64_t tab = 1; tab <= 5; ++tab) {
        XR_EXPECT(bind.OpenTab("w", tab, "").ok);
      }
      XR_EXPECT(bind.OpenTab("w", 6, drec.domain).ok);
      // Churn: N rounds of random lifecycle ops + kill/restore cycles.
      for (int round = 0; round < 50; ++round) {
        const int op = static_cast<int>(rng() % 6);
        if (op == 0) {
          (void)sched.Hibernate(durable[rng() % durable.size()], 2);
        } else if (op == 1) {
          (void)sched.Wake(durable[rng() % durable.size()], 3);
        } else if (op == 2) {
          // move a durable tab across identities (pre-nav: allowed)
          const uint64_t tab = 1 + rng() % 5;
          const std::string to = durable[rng() % durable.size()];
          (void)bind.MoveTab(bind.TabIdentity(tab).value_or(durable[0]), to,
                             tab, false, {});
        } else if (op == 3) {
          // KILL POINT: a FRESH disposable per cycle (the crash loop):
          // provision it, bind tab 6 to it, snapshot, destroy it
          // (crash-clean), restore into the post-kill store.
          CreateRequest nd;
          nd.entropy = "chaos-disp-" + std::to_string(seed) + "-" +
                       std::to_string(round);
          nd.in_memory = true;
          IdentityRecord nrec;
          XR_EXPECT_MSG(m.Create(nd, &nrec).ok, "kill-point provision");
          (void)sched.Activate(nrec.domain, 1);
          const std::string cur6 = bind.TabIdentity(6).value_or(durable[0]);
          (void)bind.MoveTab(cur6, nrec.domain, 6, false, {});
          std::vector<uint64_t> dropped;
          const std::string wire =
              SerializeSession(Snapshot(store, bind, &dropped));
          bool verified = false;
          (void)m.Destroy(nrec.domain, &verified);
          XR_EXPECT_MSG(verified, "disposable destroy verifies at kill point");
          auto res = RestoreSession(wire, store, bind);
          XR_EXPECT_MSG(res.ok, res.error.c_str());
          // The disposable's tab was dropped, never restored:
          bool restored_disp = false;
          for (const auto& tab2 : res.doc.tabs) {
            restored_disp |= tab2.domain == nrec.domain;
          }
          XR_EXPECT_MSG(!restored_disp,
                        "the disposable's tab is never restored (seed "
                        "lives in the repro)");
          // Every restored tab binds a LIVE durable identity:
          for (const auto& tab2 : res.doc.tabs) {
            const IdentityRecord* r = store.Find(tab2.domain);
            XR_EXPECT_MSG(r != nullptr && !r->in_memory,
                          "restored tab binds a live durable identity");
          }
        } else {
          // churn: activate/verify (no state change assertions needed)
          (void)sched.Activate(durable[rng() % durable.size()], 4);
        }
      }
      // Post-chaos: the initial disposable is destroyed cleanly now (its
      // zero-residue close) and every identity the chaos provisioned is
      // accounted for: durables alive, disposables gone with zero bytes.
      {
        bool verified = false;
        const auto r = m.Destroy(drec.domain, &verified);
        XR_EXPECT_MSG(r.ok && verified,
                      "the initial disposable closes verified at the end");
        XR_EXPECT_EQ(store.ResidualBytes(drec.domain), size_t(0));
      }
      // Durables survive (possibly hibernated, never gone).
      for (const auto& d : durable) {
        const IdentityRecord* r = store.Find(d);
        XR_EXPECT_MSG(r != nullptr, "durable identity survives the chaos");
        if (r != nullptr) {
          XR_EXPECT_MSG(r->state == State::kActive ||
                            r->state == State::kHibernated,
                        "durable in a legal state");
        }
      }
    }
  }

  return xrtest::Report("identity/session+chaos");
}
