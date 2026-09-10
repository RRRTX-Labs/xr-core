// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Ephemeral-partition fixture (P9-T1). Yields a disposable identity whose
// store must leave zero bytes after close — the browser-side half of the
// P6/P7/P8 kill-loop durability pattern, generalized (plan §11.4/§11.10).
//
// Attach points at pin d04cdb24d67b081f6cf80200ffc5233f44b61109:
//   content/public/browser/storage_partition.h  (StoragePartition API)
#ifndef XR_TEST_BROWSER_FIXTURES_EPHEMERAL_PARTITION_H_
#define XR_TEST_BROWSER_FIXTURES_EPHEMERAL_PARTITION_H_

#include <memory>
#include <string>

namespace content {
class BrowserContext;
class StoragePartition;
}  // namespace content

namespace xr::test {

// Creates an ephemeral StoragePartition domain for a disposable identity.
// The fixture is the ONLY place a test may mint one; the close path asserts
// the backing store directory is empty (zero-residue law) before returning.
class EphemeralPartition {
 public:
  // |name| is a test-local tag for diagnostics; the real domain is derived
  // from a fresh random id (never reuseable, never guessable).
  explicit EphemeralPartition(std::string name);
  ~EphemeralPartition();

  EphemeralPartition(const EphemeralPartition&) = delete;
  EphemeralPartition& operator=(const EphemeralPartition&) = delete;

  // Partition the given context; returns false on failure (never throws).
  bool Partition(content::BrowserContext* context);

  // Writes a marker, then tears down and asserts zero bytes remain.
  bool CloseAndAssertZeroResidue();

  const std::string& name() const { return name_; }

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  std::string name_;
};

}  // namespace xr::test

#endif  // XR_TEST_BROWSER_FIXTURES_EPHEMERAL_PARTITION_H_
