// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-bench — microbenchmark for the permission firewall (P15 T10):
// strict store load (cold), projection (warm read), the resolver with a
// populated overlay, and a single overlay mutation. Methodology mirrors
// policy/bench/bench_main.cc: steady_clock, warmup excluded, >=10^5 measured
// iterations per case, p50/p99/p99.9 reported, -O2 (flags in the build log).
// These are MEASURED numbers on ONE host. Budgets are verdicts for this host,
// not promises; reference-hardware re-check is HG-28 (human-gates.md).
// Output: human table on stderr; canonical bench-results.json on stdout.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "permissions/core/json.h"
#include "permissions/core/ops.h"
#include "permissions/core/store.h"
#include "policy/core/resolve.h"

using namespace xr::permissions;

namespace {

constexpr int kIters = 100000;
constexpr int kWarmup = 2000;

struct Stats {
  double p50 = 0, p99 = 0, p999 = 0;
};

Stats Percentiles(std::vector<double>& ns) {
  std::sort(ns.begin(), ns.end());
  auto at = [&](double q) {
    size_t i = static_cast<size_t>(std::ceil(q * static_cast<double>(ns.size()))) - 1;
    return ns[std::min(i, ns.size() - 1)];
  };
  return {at(0.50), at(0.99), at(0.999)};
}

template <typename Fn>
Stats Measure(Fn&& fn) {
  for (int i = 0; i < kWarmup; ++i) fn();
  std::vector<double> ns;
  ns.reserve(kIters);
  volatile uint64_t sink = 0;
  for (int i = 0; i < kIters; ++i) {
    auto t0 = std::chrono::steady_clock::now();
    sink = sink + fn();
    auto t1 = std::chrono::steady_clock::now();
    ns.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
  }
  return Percentiles(ns);
}

std::string Id(int n) {
  return "xr:00000000-0000-4000-8000-" + std::string(12 - std::to_string(n).size(), '0') + std::to_string(n);
}

// 50 identities, four defaults each, 200 live 7d grants: a busy profile.
Store BusyStore() {
  Store s;
  for (int i = 0; i < 50; ++i) {
    s = SetDefault(s, Id(i), "camera", "kAllow", 1000).store;
    s = SetDefault(s, Id(i), "geolocation", "kDeny", 1000).store;
    s = SetDefault(s, Id(i), "microphone", "kAsk", 1000).store;
    s = SetDefault(s, Id(i), "notifications", "kAsk", 1000).store;
  }
  for (int i = 0; i < 200; ++i) {
    GrantRequest r;
    r.identity = Id(i % 50);
    r.domain = "site" + std::to_string(i) + ".example";
    r.capability = "geolocation";
    r.scope = "7d";
    s = GrantTemp(s, r, 1000).store;
  }
  return s;
}

std::string RequestText(const std::string& view) {
  return "{\"identity\":{\"value\":\"" + Id(3) + "\"},\"origin\":{\"registrable_domain\":\"site3.example\","
         "\"scheme\":\"https\"},\"request_class\":\"kNavigation\",\"now_ms\":2000,\"session_id\":\"s1\","
         "\"identities\":[{\"value\":\"" + Id(3) + "\",\"ephemeral\":false,\"fortress\":false}],"
         "\"permission_overlay\":" + view + "}";
}

}  // namespace

int main() {
  const Store busy = BusyStore();
  const std::string text = SerializeStore(busy);
  const std::string view = ProjectViewJson(busy);
  const std::string req_text = RequestText(view);
  JsonParseResult req_json = ParseJson(req_text);

  Stats load = Measure([&] { return LoadStore(text).identities.size(); });
  Stats project = Measure([&] { return ProjectViewJson(busy).size(); });
  Stats resolve = Measure([&] {
    auto rp = xr::policy::ParseResolveRequest(req_json.value);
    return static_cast<uint64_t>(xr::policy::Resolve(rp.request).policy.camera);
  });
  GrantRequest one;
  one.identity = Id(7);
  one.domain = "bench.example";
  one.capability = "camera";
  one.scope = "once";
  Stats mutate = Measure([&] { return GrantTemp(busy, one, 1000).store.next_seq; });

  std::fprintf(stderr, "%-34s %12s %12s %12s  (ns, %d iters)\n", "case", "p50", "p99", "p99.9", kIters);
  auto row = [](const char* name, const Stats& s) {
    std::fprintf(stderr, "%-34s %12.0f %12.0f %12.0f\n", name, s.p50, s.p99, s.p999);
  };
  row("store load (strict, cold)", load);
  row("warm projection (read)", project);
  row("resolve with overlay (cold)", resolve);
  row("single overlay mutation", mutate);
  std::fprintf(stderr, "caveat: one host, shared CPU, -O2; not reference hardware (HG-28)\n");

  std::printf("{\"bench\":\"permission-firewall-p15\",\"iters\":%d,\"warmup\":%d,"
              "\"hardware_caveat\":\"single host, shared CPU, not reference hardware (HG-28)\","
              "\"cases\":{\"store_load\":{\"p50_ns\":%.0f,\"p99_ns\":%.0f,\"p999_ns\":%.0f},"
              "\"projection\":{\"p50_ns\":%.0f,\"p99_ns\":%.0f,\"p999_ns\":%.0f},"
              "\"resolve_overlay\":{\"p50_ns\":%.0f,\"p99_ns\":%.0f,\"p999_ns\":%.0f},"
              "\"mutation\":{\"p50_ns\":%.0f,\"p99_ns\":%.0f,\"p999_ns\":%.0f}}}\n",
              kIters, kWarmup, load.p50, load.p99, load.p999, project.p50, project.p99, project.p999,
              resolve.p50, resolve.p99, resolve.p999, mutate.p50, mutate.p99, mutate.p999);
  return 0;
}
