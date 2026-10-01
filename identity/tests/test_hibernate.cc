// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// concurrency cap + hibernation scheduler suite (P14-T1): the soft cap of
// 5, LRU eviction (recorded, never silent), the wake-never-resurrects law
// (the disposable negative), and the cap applying to wake as well. The
// caller supplies the monotonic tick — no clock in the core.
#include <string>

#include "harness.h"
#include "core/hibernate.h"
#include "core/identity.h"

using xr::identity::CallResult;
using xr::identity::CreateRequest;
using xr::identity::IdentityRecord;
using xr::identity::IdentityStore;
using xr::identity::Manager;
using xr::identity::Scheduler;
using xr::identity::State;

namespace {
// Create n identities via the entropy path (distinct per index).
std::vector<IdentityRecord> MakeN(Manager* m, int n) {
  std::vector<IdentityRecord> out;
  for (int i = 0; i < n; ++i) {
    CreateRequest req;
    req.entropy = "sched-entropy-" + std::to_string(i);
    IdentityRecord rec;
    xrtest::Record(m->Create(req, &rec).ok, "create " + std::to_string(i));
    out.push_back(rec);
  }
  return out;
}
}  // namespace

int main() {
  // 1. Soft cap: 6th activation evicts the LRU (first) identity, recorded.
  {
    IdentityStore store;
    Manager m(&store);
    Scheduler s(&m, 5);
    auto ids = MakeN(&m, 6);
    for (size_t i = 0; i < 5; ++i) {
      xrtest::Record(s.Activate(ids[i].domain, 100 + i).ok,
                     "activate " + std::to_string(i));
    }
    XR_EXPECT_EQ(s.ActiveCount(), size_t(5));
    XR_EXPECT_MSG(s.evictions().empty(), "no eviction under the cap");
    CallResult r = s.Activate(ids[5].domain, 200);
    XR_EXPECT_MSG(r.ok, r.error.c_str());
    XR_EXPECT_EQ(s.ActiveCount(), size_t(5));  // still 5: SOFT cap
    XR_EXPECT_EQ(s.evictions().size(), size_t(1));
    XR_EXPECT_STREQ(s.evictions()[0].domain.c_str(), ids[0].domain.c_str());
    XR_EXPECT_STREQ(s.evictions()[0].reason.c_str(), "cap");
    // The evicted identity is hibernated: record alive, state kHibernated.
    const IdentityRecord* e = store.Find(ids[0].domain);
    XR_EXPECT_MSG(e != nullptr, "evicted record survives");
    XR_EXPECT_EQ(e->state, State::kHibernated);
    // And its partition state survived the eviction (wake needs it).
    bool cookies = false;
    for (const auto& b : e->surface) cookies |= b.kind == "cookies";
    XR_EXPECT_MSG(cookies, "eviction keeps partition state");
  }

  // 2. LRU order: touching an identity moves it to the back; the NEXT
  // eviction takes the new front, not the original first.
  {
    IdentityStore store;
    Manager m(&store);
    Scheduler s(&m, 5);
    auto ids = MakeN(&m, 6);
    for (size_t i = 0; i < 5; ++i) s.Activate(ids[i].domain, 10 + i);
    s.Activate(ids[0].domain, 50);  // touch the LRU: ids[1] becomes front
    s.Activate(ids[5].domain, 60);  // over cap -> evict ids[1]
    XR_EXPECT_EQ(s.evictions().size(), size_t(1));
    XR_EXPECT_STREQ(s.evictions()[0].domain.c_str(), ids[1].domain.c_str());
  }

  // 3. Wake: a hibernated identity wakes; wake obeys the cap (an eviction
  // may fire — recorded with reason "cap", never silent).
  {
    IdentityStore store;
    Manager m(&store);
    Scheduler s(&m, 3);
    auto ids = MakeN(&m, 4);
    for (size_t i = 0; i < 3; ++i) s.Activate(ids[i].domain, 1 + i);
    s.Activate(ids[3].domain, 9);   // evicts ids[0]
    XR_EXPECT_EQ(s.evictions().size(), size_t(1));
    CallResult w = s.Wake(ids[0].domain, 20);  // wake -> evicts ids[1]
    XR_EXPECT_MSG(w.ok, w.error.c_str());
    XR_EXPECT_EQ(s.evictions().size(), size_t(2));
    XR_EXPECT_STREQ(s.evictions()[1].domain.c_str(), ids[1].domain.c_str());
    XR_EXPECT_EQ(store.Find(ids[0].domain)->state, State::kActive);
    // Waking an already-active identity is idempotent.
    CallResult again = s.Wake(ids[0].domain, 21);
    XR_EXPECT_MSG(again.ok, "idempotent wake");
    XR_EXPECT_EQ(s.evictions().size(), size_t(2));  // nothing new evicted
  }

  // 4. THE RESURRECTION NEGATIVE (security req 5): wake must never
  // re-create a purged identity — kUnknownIdentity, not a silent re-mint.
  {
    IdentityStore store;
    Manager m(&store);
    Scheduler s(&m, 5);
    auto ids = MakeN(&m, 2);
    s.Activate(ids[0].domain, 1);
    s.Hibernate(ids[0].domain, 2);           // recorded with reason "user"
    bool verified = false;
    xrtest::Record(m.Destroy(ids[0].domain, &verified).ok, "destroy ok");
    xrtest::Record(verified, "destroy verified");
    CallResult w = s.Wake(ids[0].domain, 3);
    XR_EXPECT_MSG(!w.ok, "wake refuses a purged identity");
    XR_EXPECT_MSG(w.error.find("resurrect") != std::string::npos,
                  "error says wake cannot resurrect");
    // The store did NOT gain a record: no silent re-create happened.
    XR_EXPECT_EQ(store.size(), size_t(1));
    // The audit trail still names the hibernation (history, not state).
    bool saw_user = false;
    for (const auto& ev : s.evictions()) saw_user |= ev.reason == "user";
    XR_EXPECT_MSG(saw_user, "explicit hibernation is recorded");
  }

  // 5. Explicit hibernation does not count as a cap eviction (reason user).
  {
    IdentityStore store;
    Manager m(&store);
    Scheduler s(&m, 5);
    auto ids = MakeN(&m, 2);
    s.Activate(ids[0].domain, 1);
    s.Activate(ids[1].domain, 2);
    XR_EXPECT(s.Hibernate(ids[0].domain, 3).ok);
    XR_EXPECT_EQ(s.ActiveCount(), size_t(1));
    XR_EXPECT_EQ(s.evictions().size(), size_t(1));
    XR_EXPECT_STREQ(s.evictions()[0].reason.c_str(), "user");
  }

  // 6. Unknown identity on activate is typed, not a crash.
  {
    IdentityStore store;
    Manager m(&store);
    Scheduler s(&m, 5);
    CallResult r = s.Activate("xr:00000000-0000-4000-8000-00000000dead", 1);
    XR_EXPECT_MSG(!r.ok, "unknown identity refused");
    XR_EXPECT_MSG(r.error.find("kUnknownIdentity") != std::string::npos,
                  "typed error");
  }

  return xrtest::Report("identity/hibernate");
}
