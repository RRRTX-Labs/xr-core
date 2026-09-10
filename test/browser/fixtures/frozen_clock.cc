// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Frozen-clock fixture (P9-T1). Farm-executed implementation.
#include "test/browser/fixtures/frozen_clock.h"

namespace xr::test {

class FrozenClock::Impl {
 public:
  // Farm-side: swaps the process DefaultTickClock for a fixed one
  // (base/time/default_tick_clock.h:19) for the scope of the test.
  base::TimeTicks frozen;
};

FrozenClock::FrozenClock(base::TimeTicks at)
    : impl_(std::make_unique<Impl>()) {
  impl_->frozen = at;
}

FrozenClock::~FrozenClock() = default;

void FrozenClock::Advance(base::TimeDelta delta) {
  impl_->frozen += delta;
}

base::TimeTicks FrozenClock::now() const { return impl_->frozen; }

}  // namespace xr::test
