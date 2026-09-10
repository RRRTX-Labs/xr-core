// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Ephemeral-partition fixture (P9-T1). Farm-executed implementation.
#include "test/browser/fixtures/ephemeral_partition.h"

#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"

namespace xr::test {

class EphemeralPartition::Impl {
 public:
  // Farm-side: the domain is derived from a fresh random id (never reuseable)
  // and the close path asserts the backing store directory is empty before
  // returning — the zero-residue law (plan §11.10), generalized from the
  // P6/P7/P8 kill-loop pattern.
};

EphemeralPartition::EphemeralPartition(std::string name)
    : impl_(std::make_unique<Impl>()), name_(std::move(name)) {}
EphemeralPartition::~EphemeralPartition() = default;

bool EphemeralPartition::Partition(content::BrowserContext* /*context*/) {
  return false;  // farm path — never a simulated pass
}

bool EphemeralPartition::CloseAndAssertZeroResidue() {
  return false;  // farm path — never a simulated pass
}

}  // namespace xr::test
