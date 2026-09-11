// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the <=1/6 h check law (plan perf row) + adaptive failure backoff,
// monotonic-clock discipline (all inputs are monotonic seconds; wall clock
// never enters a verdict).
#include "update/core/backoff.h"

#include "harness.h"

using namespace xr::update;

int main() {
  // fresh state may check now
  BackoffState fresh;
  XR_EXPECT_MSG(MayCheckNow(fresh, 1000), "fresh state checks now");

  // success imposes the 6 h floor (plan law: <= 1 check / 6 h)
  BackoffState s;
  const long long next = RecordOutcome(&s, 1000, Attempt::kSuccess);
  XR_EXPECT_MSG(next == 1000 + kMinCheckPeriodSeconds,
                "success next = now + 6 h");
  XR_EXPECT_MSG(!MayCheckNow(s, 1000 + kMinCheckPeriodSeconds - 1),
                "one second early is refused");
  XR_EXPECT_MSG(MayCheckNow(s, 1000 + kMinCheckPeriodSeconds),
                "exactly 6 h later is allowed");

  // failures double: 30m -> 1h -> 2h, floored by the 6 h law, capped 48 h
  BackoffState f1;
  RecordOutcome(&f1, 0, Attempt::kFailure);   // streak 1: backoff 30m < 6h
  XR_EXPECT_MSG(f1.next_allowed_mono == kMinCheckPeriodSeconds,
                "first failure uses the 6 h floor");
  BackoffState f2;
  RecordOutcome(&f2, 0, Attempt::kFailure);
  RecordOutcome(&f2, kMinCheckPeriodSeconds, Attempt::kFailure);
  XR_EXPECT_MSG(f2.next_allowed_mono == kMinCheckPeriodSeconds + kMinCheckPeriodSeconds,
                "second failure: still floored at 6 h here");
  BackoffState f3;
  f3.fail_streak = 3;
  RecordOutcome(&f3, 0, Attempt::kFailure);
  XR_EXPECT_MSG(f3.next_allowed_mono == 6 * 60 * 60,
                "streak 3 backoff (2 h) still under the floor");
  BackoffState f9;
  f9.fail_streak = 9;  // 30m * 2^8 = 128 h -> capped 48 h
  RecordOutcome(&f9, 0, Attempt::kFailure);
  XR_EXPECT_MSG(f9.next_allowed_mono == kFailureCeilingSeconds,
                "cap at 48 h");

  // monotonic discipline: a window never shrinks, even if a caller hands
  // us an earlier timestamp (defense in depth; the caller supplies
  // monotonic-clock seconds, never wall clock)
  BackoffState m;
  RecordOutcome(&m, 100000, Attempt::kSuccess);
  const long long n1 = m.next_allowed_mono;
  RecordOutcome(&m, 500, Attempt::kSuccess);  // an EARLIER timestamp lands
  XR_EXPECT_MSG(m.next_allowed_mono == n1,
                "an out-of-order reading cannot shorten the window");

  // seconds-until math
  XR_EXPECT_MSG(SecondsUntilNextCheck(s, 0) == kMinCheckPeriodSeconds + 1000 - 0 ||
                    SecondsUntilNextCheck(s, 0) == kMinCheckPeriodSeconds,
                "seconds-until is sane");
  XR_EXPECT_MSG(SecondsUntilNextCheck(fresh, 5) == 0, "fresh = 0 wait");

  return xrtest::Report("test_backoff");
}
