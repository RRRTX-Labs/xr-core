// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
#include "update/core/backoff.h"

#include <algorithm>

namespace xr::update {

namespace {
long long FailureBackoffSeconds(int fail_streak) {
  long long s = kFailureBaseSeconds;
  for (int i = 1; i < fail_streak && s < kFailureCeilingSeconds; ++i) {
    s *= 2;
  }
  return std::min(s, kFailureCeilingSeconds);
}
}  // namespace

bool MayCheckNow(const BackoffState& state, long long now_mono) {
  return now_mono >= state.next_allowed_mono;
}

long long SecondsUntilNextCheck(const BackoffState& state, long long now_mono) {
  long long d = state.next_allowed_mono - now_mono;
  return d > 0 ? d : 0;
}

long long RecordOutcome(BackoffState* state, long long now_mono, Attempt a) {
  const long long prev_next = state->next_allowed_mono;
  state->last_check_mono = now_mono;
  if (a == Attempt::kSuccess) {
    state->fail_streak = 0;
    state->next_allowed_mono = now_mono + kMinCheckPeriodSeconds;
  } else {
    state->fail_streak += 1;
    const long long backoff = FailureBackoffSeconds(state->fail_streak);
    state->next_allowed_mono =
        now_mono + std::max(kMinCheckPeriodSeconds, backoff);
  }
  // Monotonic discipline: a window never shrinks, so an out-of-order or
  // rolled-back clock reading cannot shorten the current backoff.
  state->next_allowed_mono = std::max(state->next_allowed_mono, prev_next);
  return state->next_allowed_mono;
}

}  // namespace xr::update
