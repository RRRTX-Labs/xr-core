// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — PolicyCache: generational invalidation correctness,
// the TOCTOU pin (a version validated by a consumer keeps reading the
// exact same bytes through an invalidation storm), and the deterministic
// 8-thread storm with fixed seeds. Threading here uses std::thread only;
// assertions are collected per-thread into atomics to keep failure
// reporting deterministic.
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "policy/core/cache.h"
#include "harness.h"

using namespace xr::policy;

namespace {

std::shared_ptr<const EffectivePolicy> MakePolicy(bool letterbox) {
  EffectivePolicy p;
  p.letterbox = letterbox;
  return std::make_shared<const EffectivePolicy>(p);
}

}  // namespace

int main() {
  // ---- basic hit/miss + generational invalidation ----
  {
    PolicyCache c(16);
    PolicyCacheKey k{"id1", "example.com", "kStandard"};
    auto miss = c.Get(k, "id1");
    XR_EXPECT(!miss.hit);
    c.Put(k, MakePolicy(false));
    auto hit = c.Get(k, "id1");
    XR_EXPECT(hit.hit);
    XR_EXPECT(!hit.policy->letterbox);
    c.InvalidateAll();
    auto after = c.Get(k, "id1");
    XR_EXPECT(!after.hit);  // global invalidation => stale => miss
    XR_EXPECT_EQ(c.GetStats().invalidations_all, 1u);
  }
  {
    PolicyCache c(16);
    PolicyCacheKey k{"id1", "example.com", "kStandard"};
    PolicyCacheKey k2{"id2", "example.com", "kStandard"};
    c.Put(k, MakePolicy(false));
    c.Put(k2, MakePolicy(true));
    c.Invalidate("id1");
    XR_EXPECT(!c.Get(k, "id1").hit);    // invalidated identity
    XR_EXPECT(c.Get(k2, "id2").hit);    // untouched identity
    XR_EXPECT_EQ(c.GetStats().invalidations_identity, 1u);
  }

  // ---- TOCTOU pin: pinned version reads identically mid-storm ----
  {
    PolicyCache c(64);
    PolicyCacheKey k{"id1", "example.com", "kStandard"};
    auto pinned_policy = MakePolicy(false);
    c.Put(k, pinned_policy);
    auto pinned = c.Get(k, "id1");
    XR_EXPECT(pinned.hit);
    PolicyVersion pinned_version = pinned.version;
    std::atomic<bool> stop{false};
    std::atomic<int> mutations{0};
    std::thread mutator([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        c.InvalidateAll();
        c.Put(k, MakePolicy(true));  // different bytes
        ++mutations;
      }
    });
    // The pinned read must stay byte-stable through the entire storm.
    for (int i = 0; i < 200000; ++i) {
      XR_EXPECT_MSG(pinned.policy && pinned.policy->letterbox == false,
                    "TOCTOU: pinned policy mutated mid-flight");
      XR_EXPECT_MSG(pinned.policy.get() == pinned_policy.get(),
                    "TOCTOU: pinned object identity changed");
      if (xrtest::g_failures > 0) break;
    }
    stop.store(true);
    mutator.join();
    // The pinned VERSION is now stale (invalidations happened).
    XR_EXPECT_MSG(!c.VersionIsCurrent(pinned_version, "id1"),
                  "pinned version must be stale after invalidations");
    (void)mutations;
  }

  // ---- 8-thread storm: resolve-put-get + invalidations, no crashes, ----
  // ---- deterministic final state                                 ----
  {
    PolicyCache c(256);
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
      threads.emplace_back([&, t] {
        // Deterministic per-thread key/policy sequence (seeded by t).
        for (int i = 0; i < 4000; ++i) {
          std::string id = "id" + std::to_string((t + i) % 8);
          PolicyCacheKey k{id, "site" + std::to_string(i % 32), "kStandard"};
          c.Put(k, MakePolicy((i + t) % 2 == 0));
          auto got = c.Get(k, id);
          if (got.hit && got.policy->letterbox != ((i + t) % 2 == 0)) ++failures;
          if (i % 500 == 0) c.Invalidate("id" + std::to_string(t));
          if (i % 1000 == 0) c.InvalidateAll();
          // Pinned read inside the storm must remain schema-valid bytes.
          if (got.hit) {
            std::string blob = got.policy->ToCanonicalJson();
            if (blob.find("\"letterbox\":") == std::string::npos) ++failures;
          }
        }
      });
    }
    for (auto& th : threads) th.join();
    XR_EXPECT_EQ(failures.load(), 0);
    // Final invalidation => everything stale.
    c.InvalidateAll();
    PolicyCacheKey probe{"id0", "site0", "kStandard"};
    c.Put(probe, MakePolicy(false));
    c.InvalidateAll();
    XR_EXPECT(!c.Get(probe, "id0").hit);
    auto s = c.GetStats();
    XR_EXPECT(s.invalidations_all == 34);  // 2 explicit + 8 threads x 4 each (deterministic)
    XR_EXPECT(s.invalidations_identity >= 8);  // at least one per thread
  }

  // ---- eviction: bounded, deterministic FIFO ----
  {
    PolicyCache c(4);
    for (int i = 0; i < 10; ++i) {
      PolicyCacheKey k{"id", "site" + std::to_string(i), "kStandard"};
      c.Put(k, MakePolicy(false));
    }
    auto s = c.GetStats();
    XR_EXPECT(s.size <= 4);
    XR_EXPECT(s.evictions >= 6);
    // Capacity is honored POSITIVELY (not merely "not exceeded"): the newest
    // 4 entries coexist and are all retrievable; older ones were FIFO-evicted.
    // (Guards the capacity-clamp constructor: a degenerate capacity-1 cache
    // passes the <=/>= assertions above, so coexistence must be pinned.)
    XR_EXPECT(s.size == 4);
    for (int i = 6; i < 10; ++i) {
      PolicyCacheKey k{"id", "site" + std::to_string(i), "kStandard"};
      XR_EXPECT(c.Get(k, "id").hit);
    }
    for (int i = 0; i < 6; ++i) {
      PolicyCacheKey k{"id", "site" + std::to_string(i), "kStandard"};
      XR_EXPECT(!c.Get(k, "id").hit);
    }
  }

  // ---- zero-capacity clamp: PolicyCache(0) means "smallest useful" (1), ----
  // ---- never an unbounded or broken store                              ----
  {
    PolicyCache c(0);
    PolicyCacheKey a{"id", "a.com", "kStandard"}, b{"id", "b.com", "kStandard"};
    c.Put(a, MakePolicy(false));
    XR_EXPECT(c.Get(a, "id").hit);
    c.Put(b, MakePolicy(false));  // evicts a: capacity clamped to exactly 1
    XR_EXPECT(!c.Get(a, "id").hit);
    XR_EXPECT(c.Get(b, "id").hit);
    XR_EXPECT(c.GetStats().size == 1);
  }

  return xrtest::Report("test_cache");
}
