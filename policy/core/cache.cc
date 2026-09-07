// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — PolicyCache implementation (see cache.h).
#include "policy/core/cache.h"

namespace xr::policy {

PolicyCache::PolicyCache(size_t max_entries) : max_entries_(max_entries == 0 ? 1 : max_entries) {}

void PolicyCache::Put(const PolicyCacheKey& key, std::shared_ptr<const EffectivePolicy> policy) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = map_.find(key);
  if (it != map_.end()) {
    it->second.policy = std::move(policy);
    it->second.global_generation = global_generation_;
    uint64_t ig = 0;
    auto git = identity_generations_.find(key.identity);
    ig = git == identity_generations_.end() ? 0 : git->second;
    it->second.identity_generation = ig;
    return;  // key already in eviction order
  }
  if (map_.size() >= max_entries_) {
    // FIFO eviction (deterministic; the cache is a performance structure,
    // never a correctness one — misses simply re-resolve).
    while (!order_.empty() && map_.size() >= max_entries_) {
      PolicyCacheKey victim = order_.front();
      order_.pop_front();
      map_.erase(victim);
      ++stats_.evictions;
    }
  }
  uint64_t ig = 0;
  auto git = identity_generations_.find(key.identity);
  ig = git == identity_generations_.end() ? 0 : git->second;
  map_[key] = Entry{std::move(policy), global_generation_, ig};
  order_.push_back(key);
}

PolicyCache::Lookup PolicyCache::Get(const PolicyCacheKey& key, const std::string& identity) {
  Lookup out;
  std::lock_guard<std::mutex> lock(mu_);
  auto it = map_.find(key);
  if (it == map_.end()) {
    ++stats_.misses;
    return out;
  }
  // Entry is valid iff no invalidation touched it since insertion.
  uint64_t ig = 0;
  auto git = identity_generations_.find(identity);
  ig = git == identity_generations_.end() ? 0 : git->second;
  if (it->second.global_generation != global_generation_ ||
      it->second.identity_generation != ig) {
    // Stale: treat as a miss and drop (lazy invalidation).
    ++stats_.misses;
    map_.erase(it);
    return out;
  }
  ++stats_.hits;
  out.hit = true;
  out.policy = it->second.policy;  // the pin: immutable shared object
  out.version = PolicyVersion{global_generation_, ig};
  return out;
}

PolicyVersion PolicyCache::CurrentVersion(const std::string& identity) {
  std::lock_guard<std::mutex> lock(mu_);
  uint64_t ig = 0;
  auto git = identity_generations_.find(identity);
  ig = git == identity_generations_.end() ? 0 : git->second;
  return PolicyVersion{global_generation_, ig};
}

bool PolicyCache::VersionIsCurrent(const PolicyVersion& v, const std::string& identity) {
  return CurrentVersion(identity) == v;
}

void PolicyCache::InvalidateAll() {
  std::lock_guard<std::mutex> lock(mu_);
  ++global_generation_;
  ++stats_.invalidations_all;
}

void PolicyCache::Invalidate(const std::string& identity) {
  std::lock_guard<std::mutex> lock(mu_);
  ++identity_generations_[identity];
  ++stats_.invalidations_identity;
}

PolicyCache::Stats PolicyCache::GetStats() const {
  std::lock_guard<std::mutex> lock(mu_);
  Stats s = stats_;
  s.size = map_.size();
  s.global_generation = global_generation_;
  return s;
}

}  // namespace xr::policy
