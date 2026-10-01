// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the concurrency cap + hibernation scheduler (P14-T1). Plan
// §1.4 (memory reality): SOFT CAP of 5 concurrent active identities
// (configurable); excess identities hibernate — DISCARD RENDERERS, PRESERVE
// PARTITION STATE — and wake must not exceed the §11.7 budget (≤200 ms to
// first paint; the BROWSER half of that budget is rig-measured — a perf
// row, never claimed here — while the state machine this file owns is pure
// core and fully tested).
//
// Laws under test (tests/test_hibernate.cc):
//   * the cap is a SOFT cap: activating past it EVICTS the least-recently-
//     active identity to hibernation (caller-supplied monotonic tick — no
//     clock in the core), and the eviction is RECORDED (never silent);
//   * hibernation discards only the volatile surface (renderers/cache);
//     partition state (cookies/prefs/storage) survives — wake needs it;
//   * WAKE DOES NOT RESURRECT A PURGED/DESTROYED IDENTITY (the disposable
//     law, T6): kUnknownIdentity / kNotPermitted, never a silent re-create
//     — the "wake resurrects a purged disposable" case is a planted
//     negative that must redden.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/identity.h"

namespace xr::identity {

// Plan §1.4: soft cap of 5 concurrent active identities.
inline constexpr size_t kDefaultActiveCap = 5;

struct EvictionEvent {
  std::string domain;    // who was hibernated
  uint64_t at_tick = 0;  // caller's monotonic tick (no clock in the core)
  std::string reason;    // "cap" (over soft cap) | "user" (explicit)
};

class Scheduler {
 public:
  Scheduler(Manager* manager, size_t active_cap = kDefaultActiveCap)
      : manager_(manager), active_cap_(active_cap == 0 ? 1 : active_cap) {}

  // Activate `domain` at `tick`. Past the soft cap, the least-recently-
  // activated ACTIVE identity is hibernated first (eviction recorded).
  // Destroys: hibernating an identity removes its volatile surface but the
  // record survives — state is preserved for wake.
  CallResult Activate(std::string_view domain, uint64_t tick);
  // Explicit hibernation (user/system-initiated; recorded with reason
  // "user").
  CallResult Hibernate(std::string_view domain, uint64_t tick);
  // Wake a hibernated identity (activate without cap eviction pressure is
  // NOT special: wake obeys the same cap). A destroyed or purged identity
  // is REFUSED — the resurrection negative.
  CallResult Wake(std::string_view domain, uint64_t tick);

  const std::vector<EvictionEvent>& evictions() const { return evictions_; }
  size_t ActiveCount() const { return active_order_.size(); }
  size_t cap() const { return active_cap_; }

 private:
  void EvictLru(uint64_t tick);
  Manager* manager_;
  size_t active_cap_;
  // LRU list of active domains; front = least recently activated.
  std::vector<std::string> active_order_;
  std::vector<EvictionEvent> evictions_;
};

}  // namespace xr::identity
