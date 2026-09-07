// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — (identity, site, trust) → EffectivePolicy cache with
// GENERATIONAL invalidation counters (Plan P6-T1). Design: a single mutex
// guards the map AND the generation counters; entries are immutable
// shared_ptrs, so a consumer that pinned a version keeps reading the exact
// bytes it validated with even while an invalidation storm runs (TOCTOU
// law, §1.11 snapshot note). A mutex (not a seqlock) is deliberate: the
// pinned-read guarantee makes lock-free reading unnecessary for
// correctness, and the ≤5 µs p99 budget is measured, not assumed
// (policy/bench). Resolution itself never runs under the lock.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "policy/core/effective_policy.h"

namespace xr::policy {

struct PolicyCacheKey {
  std::string identity;
  std::string site;   // registrable domain
  std::string trust;  // effective tier (kStandard/kShield/kFortress)

  bool operator==(const PolicyCacheKey& o) const {
    return identity == o.identity && site == o.site && trust == o.trust;
  }
};

struct PolicyCacheKeyHash {
  size_t operator()(const PolicyCacheKey& k) const {
    std::hash<std::string> h;
    return h(k.identity) * 1000003u ^ (h(k.site) * 31u) ^ h(k.trust);
  }
};

// The version a consumer validated with. Comparable to the cache's current
// version; entries read through a pinned version are stable snapshots.
struct PolicyVersion {
  uint64_t global_generation = 0;
  uint64_t identity_generation = 0;
  bool operator==(const PolicyVersion& o) const {
    return global_generation == o.global_generation && identity_generation == o.identity_generation;
  }
  bool operator!=(const PolicyVersion& o) const { return !(*this == o); }
};

class PolicyCache {
 public:
  struct Stats {
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t evictions = 0;
    uint64_t invalidations_all = 0;
    uint64_t invalidations_identity = 0;
    size_t size = 0;
    uint64_t global_generation = 0;
  };

  explicit PolicyCache(size_t max_entries = 1024);

  // Inserts/replaces an entry. Safe to call from any thread.
  void Put(const PolicyCacheKey& key, std::shared_ptr<const EffectivePolicy> policy);

  // Pinned read: returns the entry AND the version it was validated under.
  // The returned policy object is immutable; holding the shared_ptr pins
  // the exact bytes regardless of later invalidations (TOCTOU guarantee).
  struct Lookup {
    bool hit = false;
    PolicyVersion version;
    std::shared_ptr<const EffectivePolicy> policy;
  };
  Lookup Get(const PolicyCacheKey& key, const std::string& identity);

  PolicyVersion CurrentVersion(const std::string& identity);
  bool VersionIsCurrent(const PolicyVersion& v, const std::string& identity);

  void InvalidateAll();
  void Invalidate(const std::string& identity);

  Stats GetStats() const;

 private:
  struct Entry {
    std::shared_ptr<const EffectivePolicy> policy;
    uint64_t global_generation;
    uint64_t identity_generation;
  };
  mutable std::mutex mu_;
  std::unordered_map<PolicyCacheKey, Entry, PolicyCacheKeyHash> map_;
  std::deque<PolicyCacheKey> order_;  // FIFO eviction order
  std::unordered_map<std::string, uint64_t> identity_generations_;
  uint64_t global_generation_ = 0;
  size_t max_entries_;
  Stats stats_;
};

}  // namespace xr::policy
