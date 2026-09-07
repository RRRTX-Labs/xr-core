// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P4 spike probe: an ephemeral identity writes ZERO bytes to disk.
//
// The claim is measured, never asserted: build/spike/fsdiff.py hashes every
// path under the profile before and after create/use/Destroy() and the probe
// driver compares the two manifests. An assertion without a hash is not
// evidence of zero residual (L5).

#include "base/files/file_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/xr/xr_identity.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace xr {

namespace {

constexpr char kEphemeralId[] = "11111111-2222-4333-8444-555555555555";

int64_t CountFilesUnder(const base::FilePath& dir) {
  if (dir.empty() || !base::DirectoryExists(dir)) {
    return 0;
  }
  base::FileEnumerator it(dir, /*recursive=*/true,
                          base::FileEnumerator::FILES);
  int64_t n = 0;
  for (base::FilePath name = it.Next(); !name.empty(); name = it.Next()) {
    ++n;
  }
  return n;
}

}  // namespace

class EphemeralZeroWriteTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    IdentityRegistry::Get().Register(Identity{kEphemeralId, true, ""});
  }
};

IN_PROC_BROWSER_TEST_F(EphemeralZeroWriteTest, EphemeralPartitionIsInMemory) {
  auto* profile = browser()->profile();
  auto identity = IdentityRegistry::Get().Lookup(kEphemeralId);
  ASSERT_TRUE(identity.has_value());
  const auto cfg = StoragePartitionConfigForIdentity(profile, *identity);
  EXPECT_TRUE(cfg.in_memory());
  auto* partition = profile->GetStoragePartition(cfg, /*can_create=*/true);
  ASSERT_TRUE(partition);
  EXPECT_TRUE(partition->GetPath().empty())
      << "an in-memory partition must not have a filesystem path";
  EXPECT_EQ(0, CountFilesUnder(partition->GetPath()));
}

// An off-the-record profile forces in-memory partitions regardless of the
// identity's own flag (content/browser/browser_context.cc:144-151 CHECKs it).
// Fortress (T7) depends on this.
IN_PROC_BROWSER_TEST_F(EphemeralZeroWriteTest, OtrForcesInMemory) {
  auto* otr = browser()->profile()->GetOffTheRecordProfile(
      Profile::OTRProfileID::CreateUniqueForTesting(),
      /*create_if_needed=*/true);
  ASSERT_TRUE(otr);
  Identity durable{kEphemeralId, /*ephemeral=*/false, ""};
  const auto cfg = StoragePartitionConfigForIdentity(otr, durable);
  EXPECT_TRUE(cfg.in_memory())
      << "OTR + durable identity must still be in-memory, or the CHECK in "
         "BrowserContext::GetStoragePartition fires";
}

}  // namespace xr
