// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P15 T3 — the expiry engine. The clock is a parameter everywhere, so every
// boundary is tested at its exact millisecond:
//  * 7d ends AT expires_at (fail-closed at ==, matching ExceptionActive);
//  * a session grant ends when its session id is no longer active;
//  * a once grant ends when spent;
//  * sweeping is idempotent and deterministic, and time never runs backward.
#include <cstdint>
#include <string>

#include "permissions/core/ops.h"
#include "permissions/core/store.h"
#include "policy/tests/harness.h"

using namespace xr::permissions;

namespace {

const char* kA = "xr:00000000-0000-4000-8000-0000000000a1";
constexpr int64_t kT0 = 5000000;
constexpr int64_t kEnd = kT0 + kSevenDaysMillis;

Store OneWeek() {
  GrantRequest r;
  r.identity = kA;
  r.domain = "example.com";
  r.capability = "geolocation";
  r.scope = "7d";
  return GrantTemp(Store{}, r, kT0).store;
}

size_t LiveGrants(const Store& s) {
  size_t n = 0;
  for (const auto& [key, rec] : s.identities) {
    (void)key;
    n += rec.grants.size();
  }
  return n;
}

}  // namespace

int main() {
  // --- 7d boundary grid: the grant is gone exactly when expires_at <= now -----
  {
    const Store s = OneWeek();
    struct Case { int64_t now; bool removed; };
    const Case cases[] = {
        {kT0, false},          {kT0 + 1, false},      {kEnd - 1, false},
        {kEnd, true},          {kEnd + 1, true},      {kEnd + kMillisPerDay, true},
    };
    for (const auto& c : cases) {
      OpOutcome o = SweepExpired(s, c.now, "s1");
      XR_EXPECT_MSG(o.ok, "sweep ok");
      const bool removed = LiveGrants(o.store) == 0;
      XR_EXPECT_MSG(removed == c.removed, "7d boundary: removed iff expires_at <= now");
      XR_EXPECT_MSG(o.rows.empty() != c.removed, "a removal emits exactly one expiry row (none otherwise)");
      if (c.removed) {
        XR_EXPECT_MSG(o.rows.size() == 1 && o.rows[0].event == AuditEvent::kExpiry, "expiry event");
        if (o.rows.size() == 1) {  // no row: reported above; never index an empty vector
          XR_EXPECT_MSG(o.rows[0].reason == "ttl" && o.rows[0].ttl_millis == kSevenDaysMillis,
                        "ttl reason and TTL");
          XR_EXPECT_MSG(o.rows[0].ts_millis == c.now, "expiry is stamped with the caller's clock");
          XR_EXPECT_MSG(o.rows[0].deciding_layer == DecidingLayer::kGlobalFallback,
                        "after expiry the global fallback decides");
        }
      }
    }
  }

  // --- a live identity default still decides after a grant expires ------------
  {
    GrantRequest r;
    r.identity = kA;
    r.domain = "example.com";
    r.capability = "geolocation";
    r.scope = "7d";
    Store s = SetDefault(Store{}, kA, "geolocation", "kAsk", kT0).store;
    s = GrantTemp(s, r, kT0).store;
    OpOutcome o = SweepExpired(s, kEnd, "s1");
    XR_EXPECT_MSG(o.rows.size() == 1 && o.rows[0].deciding_layer == DecidingLayer::kIdentityOverlay,
                  "with an identity default in place, the overlay still decides after expiry");
  }

  // --- session grants end on a session change, never on a clock --------------
  {
    GrantRequest r;
    r.identity = kA;
    r.domain = "example.com";
    r.capability = "camera";
    r.scope = "session";
    r.session_id = "s1";
    const Store s = GrantTemp(Store{}, r, kT0).store;
    XR_EXPECT_MSG(LiveGrants(SweepExpired(s, kEnd * 10, "s1").store) == 1,
                  "a session grant survives any clock while its session is active");
    OpOutcome restart = SweepExpired(s, kT0 + 1, "s2");
    XR_EXPECT_MSG(LiveGrants(restart.store) == 0, "a new session id ends the old session's grants");
    XR_EXPECT_MSG(restart.rows.size() == 1 && restart.rows[0].reason == "session_end", "row reason: session_end");
    XR_EXPECT_MSG(restart.rows.size() == 1 && restart.rows[0].ttl_millis == 0,
                  "session grants carry no TTL");
    XR_EXPECT_MSG(LiveGrants(SweepExpired(s, kT0 + 1, "").store) == 0, "no active session ends all session grants");
  }

  // --- sweep is idempotent and deterministic ----------------------------------
  {
    const Store s = OneWeek();
    OpOutcome first = SweepExpired(s, kEnd, "s1");
    OpOutcome again = SweepExpired(first.store, kEnd, "s1");
    XR_EXPECT_MSG(again.rows.empty(), "second sweep at the same instant: no rows");
    XR_EXPECT_MSG(SerializeStore(again.store) == SerializeStore(first.store), "second sweep: identical bytes");
    OpOutcome twin = SweepExpired(s, kEnd, "s1");
    XR_EXPECT_MSG(SerializeStore(twin.store) == SerializeStore(first.store), "same inputs, same bytes");
    XR_EXPECT_MSG(twin.rows.size() == first.rows.size() &&
                      (twin.rows.empty() || twin.rows[0].ts_millis == first.rows[0].ts_millis),
                  "same inputs, same rows");
  }

  // --- time never runs backward for evaluation --------------------------------
  {
    XR_EXPECT_MSG(EffectiveNow(kEnd, kT0) == kEnd, "a backward clock jump keeps the last-seen time");
    XR_EXPECT_MSG(EffectiveNow(kT0, kEnd) == kEnd, "a forward jump is taken");
    XR_EXPECT_MSG(EffectiveNow(kT0, kT0) == kT0, "equal is equal");
    // Once swept, a grant stays gone even if the clock later reads earlier:
    // the sweep removed it, and nothing revives a removed grant.
    const Store swept = SweepExpired(OneWeek(), kEnd, "s1").store;
    const Store back = SweepExpired(swept, kT0, "s1").store;
    XR_EXPECT_MSG(LiveGrants(back) == 0, "a backward clock does not resurrect an expired grant");
    XR_EXPECT_MSG(SerializeStore(back) == SerializeStore(swept), "backward sweep changes nothing");
  }

  // --- a once grant is gone after its single use, at any clock ---------------
  {
    GrantRequest r;
    r.identity = kA;
    r.domain = "example.com";
    r.capability = "camera";
    r.scope = "once";
    const Store s = GrantTemp(Store{}, r, kT0).store;
    XR_EXPECT_MSG(LiveGrants(SweepExpired(s, kT0, "s1").store) == 1, "an unspent once grant survives a sweep");
    OpOutcome used = ConsumeOnce(s, "g1", kT0 + 1);
    XR_EXPECT_MSG(LiveGrants(SweepExpired(used.store, kEnd * 2, "s1").store) == 0, "a spent once grant stays gone");
  }

  return xrtest::Report("test_expiry");
}
