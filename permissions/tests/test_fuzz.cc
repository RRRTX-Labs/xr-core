// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// permissions overlay fuzz suite (P15-CLOSE C3, fleet row `permissions-overlay`):
// a seeded, deterministic op-loop over the whole core (store, ops, sweep,
// projection) plus hostile store bytes, asserting INVARIANTS after every step,
// not only the absence of crashes:
//   * a successful op yields a store whose bytes reload to the same bytes (the
//     canonical law), never a corrupt one;
//   * a refused op leaves the store byte-identical (no half-applied state);
//   * the clock never runs backward (EffectiveNow), and after a sweep at now no
//     grant past expiry, spent, or bound to an ended session is left;
//   * the projection never shows a revoked or spent grant;
//   * hostile bytes either load as corrupt or are exactly their own canonical
//     serialization (no "repaired" store ever escapes LoadStore);
//   * an extra can never be recorded as kAllow.
// Fleet contract (themes/identity test_fuzz.cc shape): XR_FUZZ_SECONDS bounds
// the wall-clock budget (default 30 s; gate >= 60 s, evidence 600 s) and
// XR_FUZZ_SEED fixes the sequence, so a violation reproduces from its seed.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "permissions/core/audit.h"
#include "permissions/core/envelope.h"
#include "permissions/core/json.h"
#include "permissions/core/ops.h"
#include "permissions/core/store.h"
#include "policy/tests/harness.h"

using namespace xr::permissions;

namespace {

const char* const kIds[] = {"xr:a", "xr:b", "xr:c", ""};
const char* const kDomains[] = {"example.com", "a.example", "x-y.example.org", "Example.com",
                                "https://e.com", "e..com", "-e.com", "e.com:80", ""};
const char* const kCaps[] = {"geolocation", "camera", "microphone", "notifications", "midi", "x"};
const char* const kStates[] = {"kDeny", "kAsk", "kAllow", "allow"};
const char* const kScopes[] = {"once", "session", "7d", "30d"};
const char* const kSessions[] = {"", "s1", "s2"};

template <size_t N>
const char* Pick(std::mt19937& rng, const char* const (&arr)[N]) {
  return arr[rng() % N];
}

int PastExpiry(const Store& s, int64_t now, const std::string& sid) {
  int n = 0;
  for (const auto& [k, rec] : s.identities) {
    (void)k;
    for (const auto& g : rec.grants) {
      if (g.revoked) continue;
      if ((g.scope == Scope::k7d && g.expires_at <= now) ||
          (g.scope == Scope::kSession && g.session_id != sid) ||
          (g.scope == Scope::kOnce && g.remaining_uses <= 0)) {
        ++n;
      }
    }
  }
  return n;
}

std::string Mutate(std::mt19937& rng, std::string s) {
  if (s.empty()) return "{";
  switch (rng() % 5) {
    case 0: s[rng() % s.size()] = static_cast<char>(rng() % 256); break;  // flip
    case 1: s.resize(rng() % s.size()); break;                            // truncate
    case 2: s.insert(rng() % s.size(), 1, " \t,{}[]\"0"[rng() % 9]); break;
    case 3: s.erase(rng() % s.size(), 1); break;
    default: {                                                            // splice
      const size_t a = rng() % s.size();
      s += s.substr(a, rng() % 16);
    }
  }
  return s;
}

}  // namespace

int main() {
  long long budget_s = 30;
  if (const char* e = std::getenv("XR_FUZZ_SECONDS")) budget_s = std::atoll(e);
  uint64_t seed = 20260910;
  if (const char* e = std::getenv("XR_FUZZ_SEED")) seed = std::atoll(e);
  std::mt19937 rng(static_cast<unsigned>(seed));  // the seed IS the repro key
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(budget_s);

  long long iter = 0, ok_ops = 0, refused = 0, hostile = 0, hostile_loaded = 0;
  Store st;
  int64_t last_seen = 1790000000000LL;
  std::vector<std::string> history;  // recent canonical stores, the hostile seeds
  while (std::chrono::steady_clock::now() < deadline || iter < 100) {
    ++iter;
    // The clock jumps both ways; EffectiveNow folds it monotone.
    const int64_t jump = static_cast<int64_t>(rng() % (9 * kMillisPerDay)) - 2 * kMillisPerDay;
    const int64_t now = EffectiveNow(last_seen, last_seen + jump);
    XR_EXPECT_MSG(now >= last_seen, "EffectiveNow ran backward");
    last_seen = now;
    const std::string before = SerializeStore(st);
    OpOutcome o;
    const std::string sid = Pick(rng, kSessions);
    switch (rng() % 8) {
      case 0: o = SetDefault(st, Pick(rng, kIds), Pick(rng, kCaps), Pick(rng, kStates), now); break;
      case 1:
      case 2: {
        GrantRequest r;
        r.identity = Pick(rng, kIds);
        r.domain = Pick(rng, kDomains);
        r.capability = Pick(rng, kCaps);
        r.scope = Pick(rng, kScopes);
        r.session_id = sid;
        o = GrantTemp(st, r, now);
        break;
      }
      case 3: o = SetDenied(st, Pick(rng, kIds), rng() % 2 == 0, now); break;
      case 4: o = ConsumeOnce(st, "g" + std::to_string(1 + rng() % (st.next_seq + 1)), now); break;
      case 5: {
        o = SweepExpired(st, now, sid);
        if (o.ok) XR_EXPECT_EQ(PastExpiry(o.store, now, sid), 0);
        break;
      }
      case 6: o = RevokeSite(st, Pick(rng, kIds), Pick(rng, kDomains), now); break;
      default: o = RevokeAll(st, Pick(rng, kIds), now); break;
    }
    if (o.ok) {
      ++ok_ops;
      const std::string after = SerializeStore(o.store);
      const Store back = LoadStore(after);
      XR_EXPECT_MSG(!back.corrupt, "a successful op produced a store that does not reload");
      XR_EXPECT_MSG(SerializeStore(back) == after, "reload is not byte-identical");
      st = o.store;
      if (history.size() < 64) history.push_back(after); else history[rng() % 64] = after;
    } else {
      ++refused;
      XR_EXPECT_MSG(o.rows.empty(), "a refused op emitted audit rows");
      XR_EXPECT_MSG(SerializeStore(o.store) == before, "a refused op changed the store");
    }
    for (const auto& r : o.rows) {
      std::string red;
      XR_EXPECT_MSG(r.origin.empty() || (RedactOrigin(r.origin, &red) && red == r.origin),
                    "an audit row carries a non-registrable origin");
    }
    // The projection shows exactly the live grants: never a revoked or spent one.
    size_t live = 0;
    for (const auto& [k, rec] : st.identities) {
      (void)k;
      for (const auto& [name, state] : rec.extras) {
        (void)name;
        XR_EXPECT_MSG(state != CapState::kAllow, "an extra was recorded as kAllow");
      }
      for (const auto& g : rec.grants) {
        if (!g.revoked && !(g.scope == Scope::kOnce && g.remaining_uses <= 0)) ++live;
      }
    }
    const JsonParseResult view = ParseJson(ProjectViewJson(st));
    XR_EXPECT_MSG(view.ok && view.value.find("grants") != nullptr &&
                      view.value.find("grants")->as_array().size() == live,
                  "the projection does not show exactly the live grants");
    // Hostile bytes: corrupt, or exactly canonical. Never a repaired store.
    if (!history.empty()) {
      ++hostile;
      const std::string bad = Mutate(rng, history[rng() % history.size()]);
      const Store h = LoadStore(bad);
      if (!h.corrupt) {
        ++hostile_loaded;
        XR_EXPECT_MSG(SerializeStore(h) == bad, "a non-canonical store loaded");
      }
      XR_EXPECT_MSG(LoadStore(SerializeStore(h)).corrupt == h.corrupt,
                    "corruption did not persist through a save");
    }
    if (st.next_seq > 4000) st = Store{};  // keep each episode bounded
  }
  const int rc = xrtest::Report("permissions/fuzz");
  std::printf("permissions fuzz: %d violations (seed %llu, %lld s budget, %lld iters, "
              "%lld ok, %lld refused, %lld hostile (%lld loaded))\n",
              xrtest::g_failures, static_cast<unsigned long long>(seed), budget_s, iter,
              ok_ops, refused, hostile, hostile_loaded);
  return rc;
}
