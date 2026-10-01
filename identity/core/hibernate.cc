// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — scheduler implementation (header has the laws). No clock:
// every ordering decision uses the caller's monotonic tick.
#include "core/hibernate.h"

#include <algorithm>
#include <utility>

namespace xr::identity {

void Scheduler::EvictLru(uint64_t tick) {
  if (active_order_.empty()) return;
  // front of active_order_ = least recently activated (see Activate).
  const std::string victim = active_order_.front();
  IdentityRecord* r = manager_->FindInStore(victim);
  if (r && r->state == State::kActive) {
    manager_->Hibernate(victim);
    evictions_.push_back({victim, tick, "cap"});
  }
  active_order_.erase(active_order_.begin());
}

CallResult Scheduler::Activate(std::string_view domain, uint64_t tick) {
  CallResult res;
  IdentityRecord* r = manager_->FindInStore(domain);
  if (!r) { res.error = "kUnknownIdentity"; return res; }
  if (r->state == State::kDestroyed) { res.error = "kNotPermitted"; return res; }
  // Soft cap: evict the LRU active identity until there is room. The
  // identity being activated may itself be hibernated (a wake) — remove it
  // from the LRU list first so waking never evicts the waker.
  active_order_.erase(std::remove(active_order_.begin(), active_order_.end(),
                                  std::string(domain)),
                      active_order_.end());
  while (active_order_.size() + 1 > active_cap_) {
    EvictLru(tick);
  }
  res = manager_->Activate(domain);
  if (res.ok) {
    active_order_.push_back(std::string(domain));
  }
  return res;
}

CallResult Scheduler::Hibernate(std::string_view domain, uint64_t tick) {
  CallResult res = manager_->Hibernate(domain);
  if (res.ok) {
    active_order_.erase(std::remove(active_order_.begin(), active_order_.end(),
                                    std::string(domain)),
                        active_order_.end());
    IdentityRecord* r = manager_->FindInStore(domain);
    (void)r;
    evictions_.push_back({std::string(domain), tick, "user"});
  }
  return res;
}

CallResult Scheduler::Wake(std::string_view domain, uint64_t tick) {
  CallResult res;
  IdentityRecord* r = manager_->FindInStore(domain);
  if (!r) {
    // The resurrection law: a purged identity (record gone) is UNKNOWN —
    // wake must not re-create it, not even from the eviction history.
    res.error = "kUnknownIdentity (purged; wake cannot resurrect)";
    return res;
  }
  if (r->state == State::kDestroyed) {
    res.error = "kNotPermitted (destroyed; wake cannot resurrect)";
    return res;
  }
  if (r->state == State::kActive) {
    res.ok = true;  // already awake; idempotent, no state change
    return res;
  }
  // Hibernated: wake = activate under the same soft cap (an eviction may
  // fire — that is the cap's contract, and it is recorded).
  return Activate(domain, tick);
}

}  // namespace xr::identity
