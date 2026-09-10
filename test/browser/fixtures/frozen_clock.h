// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Frozen-clock fixture (P9-T1). Time-dependent assertions (attention-budget
// decay, ledger counters, prompt re-arm windows) must be reproducible; the
// frozen clock is how upstream browser tests do it.
//
// Attach points at pin d04cdb24d67b081f6cf80200ffc5233f44b61109:
//   base/time/default_tick_clock.h:19   class DefaultTickClock : public TickClock
//   base/test/scoped_feature_list.h:83  class ScopedFeatureList
#ifndef XR_TEST_BROWSER_FIXTURES_FROZEN_CLOCK_H_
#define XR_TEST_BROWSER_FIXTURES_FROZEN_CLOCK_H_

#include <memory>

#include "base/time/time.h"

namespace xr::test {

// Scopes a fixed TickClock so every Now() inside the scope returns the same
// value. Mirrors the P9 frozen-clock law (--as-of) on the browser side: two
// runs at the same instant are byte-identical.
class FrozenClock {
 public:
  // |at| is the frozen instant (documented per test, never wall-clock).
  explicit FrozenClock(base::TimeTicks at);
  ~FrozenClock();

  FrozenClock(const FrozenClock&) = delete;
  FrozenClock& operator=(const FrozenClock&) = delete;

  // Advance the frozen instant by |delta| (deterministic, test-controlled).
  void Advance(base::TimeDelta delta);

  base::TimeTicks now() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace xr::test

#endif  // XR_TEST_BROWSER_FIXTURES_FROZEN_CLOCK_H_
