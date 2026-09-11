// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: deterministic staged-rollout bucketing (P10-T5). Same local id +
// channel always maps to the same bucket; buckets stay in range; channels
// separate; the ramp comparison is a pure data law with boundary tests.
#include "update/core/cohort.h"

#include "harness.h"

using namespace xr::update;

int main() {
  // determinism + stability
  const int b1 = CohortBucket("0f3a1c-device-uuid", "stable", 100);
  const int b2 = CohortBucket("0f3a1c-device-uuid", "stable", 100);
  XR_EXPECT_MSG(b1 == b2, "same id+channel -> same bucket");
  XR_EXPECT_MSG(b1 >= 0 && b1 < 100, "bucket in range");

  // the golden-vector cross-check value (kept in lockstep with
  // docs/contracts/vectors/update-v1.json)
  XR_EXPECT_MSG(CohortBucket("0f3a1c-device-uuid", "stable", 100) == 79,
                "vector value cohort-deterministic");
  XR_EXPECT_MSG(CohortBucket("0f3a1c-device-uuix", "stable", 100) == 41,
                "vector value cohort-differs-by-install");
  XR_EXPECT_MSG(CohortBucket("0f3a1c-device-uuid", "nightly", 100) == 94,
                "vector value cohort-differs-by-channel");

  // channels separate (an id can be early on nightly and late on stable)
  int flips = 0;
  for (int i = 0; i < 50; ++i) {
    const std::string id = "device-" + std::to_string(i);
    if ((CohortBucket(id, "nightly", 100) < 50) !=
        (CohortBucket(id, "stable", 100) < 50)) {
      ++flips;
    }
  }
  XR_EXPECT_MSG(flips > 5, "channels distribute independently");

  // distribution sanity: buckets spread (no clustering into one bucket)
  int hits[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < 1000; ++i) {
    ++hits[CohortBucket("install-" + std::to_string(i), "stable", 10)];
  }
  for (int i = 0; i < 10; ++i) {
    XR_EXPECT_MSG(hits[i] > 40 && hits[i] < 160,
                  "bucket " + std::to_string(i) + " spread ok");
  }

  // ramp boundaries: 0% nothing, 100% everything, k% exactly buckets < k
  XR_EXPECT_MSG(!InRamp(0, 0), "0% ramp is empty");
  XR_EXPECT_MSG(InRamp(99, 100), "100% ramp covers the last bucket");
  XR_EXPECT_MSG(InRamp(49, 50) && !InRamp(50, 50),
                "50% ramp boundary lands exactly");
  XR_EXPECT_MSG(InRamp(0, 1) && !InRamp(1, 1), "1% ramp covers bucket 0 only");

  return xrtest::Report("test_cohort");
}
