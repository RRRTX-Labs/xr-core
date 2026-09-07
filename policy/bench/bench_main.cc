// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-bench — microbenchmark for the policy core: cold resolve,
// warm-cache read (pinned), and snapshot apply. Methodology: steady_clock,
// warmup excluded, ≥10^5 measured iterations, p50/p99/p99.9 reported, -O2
// (exact flags recorded in the build log; no LTO games). Results are
// MEASURED numbers with hardware caveats — budgets are verdicts, not
// promises; the reference-hardware re-check is HG-28. Output: human table
// on stdout + bench-results.json (canonical).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "policy/core/cache.h"
#include "policy/core/resolve.h"
#include "policy/core/service.h"
#include "policy/core/snapshot.h"
#include "policy/core/json.h"

using namespace xr::policy;
using Clock = std::chrono::steady_clock;

namespace {

struct Sample {
  double p50, p99, p999;
};

Sample Percentiles(std::vector<double>& v) {
  std::sort(v.begin(), v.end());
  auto at = [&](double q) { return v[static_cast<size_t>(q * static_cast<double>(v.size() - 1))]; };
  return Sample{at(0.50), at(0.99), at(0.999)};
}

const char* kReq =
    "{\"identity\":{\"value\":\"xr:00000000-0000-4000-8000-000000000001\"},"
    "\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"example.com\"},"
    "\"request_class\":\"kNavigation\",\"trust_context\":\"kShield\"}";

}  // namespace

int main() {
  const int kWarmup = 20000;
  const int kIters = 100000;
  std::vector<double> cold(kIters), warm(kIters), snap(kIters);

  // ---- cold resolve (parse + resolve + serialize, no cache) ----
  {
    auto parsed = ParseJson(kReq);
    for (int i = 0; i < kWarmup; ++i) {
      RequestParse rp = ParseResolveRequest(parsed.value);
      ResolveOutputToCanonicalJson(Resolve(rp.request));
    }
    for (int i = 0; i < kIters; ++i) {
      auto t0 = Clock::now();
      RequestParse rp = ParseResolveRequest(parsed.value);
      ResolveOutputToCanonicalJson(Resolve(rp.request));
      auto t1 = Clock::now();
      cold[i] = std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
  }

  // ---- warm cache pinned read ----
  {
    PolicyCache cache(1024);
    PolicyCacheKey key{"xr:00000000-0000-4000-8000-000000000001", "example.com", "kShield"};
    auto parsed = ParseJson(kReq);
    RequestParse rp = ParseResolveRequest(parsed.value);
    cache.Put(key, std::make_shared<const EffectivePolicy>(Resolve(rp.request).policy));
    const std::string id = key.identity;
    for (int i = 0; i < kWarmup; ++i) (void)cache.Get(key, id);
    for (int i = 0; i < kIters; ++i) {
      auto t0 = Clock::now();
      auto got = cache.Get(key, id);
      auto t1 = Clock::now();
      if (!got.hit) { std::fprintf(stderr, "bench bug: warm miss\n"); return 1; }
      warm[i] = std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
  }

  // ---- snapshot apply (decode full, 40 entries) ----
  {
    std::vector<SnapshotEntry> state;
    for (int i = 0; i < 40; ++i) {
      SnapshotEntry e;
      e.identity = "xr:00000000-0000-4000-8000-00000000000" + std::to_string(i % 10);
      e.site = "site" + std::to_string(i) + ".example.com";
      e.trust = "kShield";
      e.policy.letterbox = i % 2 == 0;
      state.push_back(e);
    }
    auto enc = EncodeFullSnapshot(1, state);
    if (!enc.ok) { std::fprintf(stderr, "bench bug: encode failed\n"); return 1; }
    for (int i = 0; i < kWarmup / 10; ++i) (void)DecodeFullSnapshot(enc.blob);
    for (int i = 0; i < kIters; ++i) {
      auto t0 = Clock::now();
      auto dec = DecodeFullSnapshot(enc.blob);
      auto t1 = Clock::now();
      if (!dec.ok) { std::fprintf(stderr, "bench bug: decode failed\n"); return 1; }
      snap[i] = std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
  }

  Sample s_cold = Percentiles(cold), s_warm = Percentiles(warm), s_snap = Percentiles(snap);

  // Budget verdicts (measured vs plan budgets: ≤200µs cold, ≤5µs warm,
  // ≤2ms snapshot apply; honest verdicts, deviations recorded, no fudge).
  const char* v_cold = s_cold.p99 <= 200.0 ? "MET" : "DEVIATION";
  const char* v_warm = s_warm.p99 <= 5.0 ? "MET" : "DEVIATION";
  const char* v_snap = s_snap.p99 <= 2000.0 ? "MET" : "DEVIATION";

  std::printf("%-28s %10s %10s %10s  %s\n", "benchmark (us)", "p50", "p99", "p99.9", "budget");
  std::printf("%-28s %10.3f %10.3f %10.3f  cold <=200us: %s\n", "resolve_cold", s_cold.p50, s_cold.p99, s_cold.p999, v_cold);
  std::printf("%-28s %10.3f %10.3f %10.3f  warm <=5us: %s\n", "cache_warm_pinned_read", s_warm.p50, s_warm.p99, s_warm.p999, v_warm);
  std::printf("%-28s %10.3f %10.3f %10.3f  apply <=2000us: %s\n", "snapshot_apply_40", s_snap.p50, s_snap.p99, s_snap.p999, v_snap);

  JsonValue::Object root;
  root.emplace("unit", JsonValue(std::string("microseconds")));
  root.emplace("iterations", JsonValue(static_cast<int64_t>(kIters)));
  root.emplace("warmup_excluded", JsonValue(true));
  auto mk = [](const Sample& s) {
    JsonValue::Object o;
    o.emplace("p50", JsonValue(s.p50));
    o.emplace("p99", JsonValue(s.p99));
    o.emplace("p99.9", JsonValue(s.p999));
    return o;
  };
  root.emplace("resolve_cold", JsonValue(mk(s_cold)));
  root.emplace("cache_warm_pinned_read", JsonValue(mk(s_warm)));
  root.emplace("snapshot_apply_40", JsonValue(mk(s_snap)));
  JsonValue::Object budgets;
  budgets.emplace("resolve_cold_us", JsonValue(200.0));
  budgets.emplace("cache_warm_pinned_read_us", JsonValue(5.0));
  budgets.emplace("snapshot_apply_us", JsonValue(2000.0));
  root.emplace("budgets", JsonValue(std::move(budgets)));
  JsonValue::Object verdicts;
  verdicts.emplace("resolve_cold", JsonValue(std::string(v_cold)));
  verdicts.emplace("cache_warm_pinned_read", JsonValue(std::string(v_warm)));
  verdicts.emplace("snapshot_apply_40", JsonValue(std::string(v_snap)));
  root.emplace("verdicts", JsonValue(std::move(verdicts)));
  JsonValue::Object env;
  env.emplace("compiler", JsonValue(std::string("g++ 14.2.0 (recorded in build log)")));
  env.emplace("flags", JsonValue(std::string("-std=c++20 -O2 -Wall -Wextra -Werror")));
  root.emplace("environment", JsonValue(std::move(env)));
  std::printf("%s\n", JsonValue(std::move(root)).Canonical().c_str());
  return 0;
}
