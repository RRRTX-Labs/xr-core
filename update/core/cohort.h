// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: deterministic staged-rollout bucketing (P10-T5), computed from a
// LOCAL random install id. The id never leaves the client; the server sees
// only coarse cohort counts (opt-out per platform repo, docs/release/
// PRIVACY.md). No server-side personal state exists by construction.
#pragma once

#include <string>

namespace xr::update {

// Deterministic cohort bucket in [0, buckets). Same (install_id, channel)
// always maps to the same bucket — a device's ramp position is stable and
// auditable from its local id. Uses the in-tree FIPS SHA-256 for the mix
// (identification, not authenticity; see update/core/sha256.h).
int CohortBucket(const std::string& install_id, const std::string& channel,
                 int buckets);

// True when `bucket` is inside the ramp for `percent` (0..100): the ramp
// grows from bucket 0 upward, so a paused ramp and a grown ramp are both
// pure data comparisons (release/rollout/policy.yaml).
bool InRamp(int bucket, int percent);

}  // namespace xr::update
