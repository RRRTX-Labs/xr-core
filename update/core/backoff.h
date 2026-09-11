// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: update-check backoff. Two laws, both from the plan (§11.7 perf
// row + P10 Tasks T1): the client checks at most once per 6 hours
// (21600 s), and failures back off adaptively. All time is MONOTONIC — the
// caller supplies seconds from a monotonic clock; wall-clock never enters a
// verdict (frozen-clock law, house style).
#pragma once

#include <cstdint>

namespace xr::update {

struct BackoffState {
  long long last_check_mono = -1;  // seconds, monotonic clock (-1 = never)
  long long next_allowed_mono = 0;
  int fail_streak = 0;             // consecutive failed attempts
};

// The plan's floor: at most one check per 6 h.
constexpr long long kMinCheckPeriodSeconds = 6 * 60 * 60;
// Adaptive failure backoff: 30 min doubling to a 48 h ceiling.
constexpr long long kFailureBaseSeconds = 30 * 60;
constexpr long long kFailureCeilingSeconds = 48 * 60 * 60;

// Outcome of a decided attempt (input to RecordOutcome).
enum class Attempt { kSuccess, kFailure };

// Whether a check may run at `now_mono`. Pure function of state.
bool MayCheckNow(const BackoffState& state, long long now_mono);

// Seconds until the next allowed check (0 = now).
long long SecondsUntilNextCheck(const BackoffState& state, long long now_mono);

// Record an attempt outcome; returns the updated next-allowed time.
// Success: next = max(now + 6 h period). Failure: the streak doubles the
// failure backoff (30 min -> 1 h -> 2 h -> ... -> 48 h cap), and the next
// allowed is max(6 h floor, failure backoff).
long long RecordOutcome(BackoffState* state, long long now_mono, Attempt a);

}  // namespace xr::update
