// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
#include "update/core/cohort.h"

#include "update/core/sha256.h"

namespace xr::update {

int CohortBucket(const std::string& install_id, const std::string& channel,
                 int buckets) {
  if (buckets <= 0) return 0;
  const std::string mixed = install_id + "\x1f" + channel;
  const std::array<uint8_t, 32> d = Sha256(mixed);
  // Top 8 bytes, big-endian, modulo buckets (buckets is small; a fixed
  // two-word reduction avoids inventing any encoding).
  uint64_t acc = 0;
  for (int i = 0; i < 8; ++i) acc = (acc << 8) | d[i];
  return static_cast<int>(acc % static_cast<uint64_t>(buckets));
}

bool InRamp(int bucket, int percent) {
  if (percent <= 0) return false;
  if (percent >= 100) return true;
  return bucket < percent;  // ramp grows from bucket 0: pure data comparison
}

}  // namespace xr::update
