// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P4 spike probe (T7): Fortress = an off-the-record Profile coexisting with
// per-identity partition domains inside it.

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/xr/xr_identity.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace xr {

namespace {

constexpr char kIdentityA[] = "11111111-2222-4333-8444-555555555555";
constexpr char kIdentityB[] = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";

}  // namespace

class FortressProfileCoexistTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    IdentityRegistry::Get().Register(Identity{kIdentityA, false, ""});
    IdentityRegistry::Get().Register(Identity{kIdentityB, false, ""});
  }

  Profile* Otr() {
    return browser()->profile()->GetOffTheRecordProfile(
        Profile::OTRProfileID::CreateUniqueForTesting(),
        /*create_if_needed=*/true);
  }
};

// A custom partition domain coexists with an OTR profile: custom domains are
// not rejected, they are forced in-memory.
IN_PROC_BROWSER_TEST_F(FortressProfileCoexistTest, OtrCoexistsWithDomains) {
  auto* otr = Otr();
  ASSERT_TRUE(otr);
  auto a = IdentityRegistry::Get().Lookup(kIdentityA);
  auto b = IdentityRegistry::Get().Lookup(kIdentityB);
  ASSERT_TRUE(a && b);
  const auto cfg_a = StoragePartitionConfigForIdentity(otr, *a);
  const auto cfg_b = StoragePartitionConfigForIdentity(otr, *b);
  EXPECT_FALSE(cfg_a.is_default());
  EXPECT_FALSE(cfg_b.is_default());
  EXPECT_TRUE(cfg_a.in_memory());
  EXPECT_TRUE(cfg_b.in_memory());
  EXPECT_NE(cfg_a, cfg_b);
}

// OTR identity partitions are still isolated from each other.
IN_PROC_BROWSER_TEST_F(FortressProfileCoexistTest, OtrIdentitiesStaySeparate) {
  auto* otr = Otr();
  auto a = IdentityRegistry::Get().Lookup(kIdentityA);
  auto b = IdentityRegistry::Get().Lookup(kIdentityB);
  ASSERT_TRUE(otr && a && b);
  auto* pa = otr->GetStoragePartition(
      StoragePartitionConfigForIdentity(otr, *a), /*can_create=*/true);
  auto* pb = otr->GetStoragePartition(
      StoragePartitionConfigForIdentity(otr, *b), /*can_create=*/true);
  EXPECT_NE(pa, pb);
}

// The original profile and its OTR profile never collapse onto one partition.
IN_PROC_BROWSER_TEST_F(FortressProfileCoexistTest, OriginalAndOtrDoNotCollapse) {
  auto* otr = Otr();
  auto a = IdentityRegistry::Get().Lookup(kIdentityA);
  ASSERT_TRUE(otr && a);
  auto* parent_partition = browser()->profile()->GetStoragePartition(
      StoragePartitionConfigForIdentity(browser()->profile(), *a), true);
  auto* otr_partition = otr->GetStoragePartition(
      StoragePartitionConfigForIdentity(otr, *a), true);
  EXPECT_NE(parent_partition, otr_partition);
  EXPECT_TRUE(otr->IsOffTheRecord());
  EXPECT_EQ(browser()->profile(), otr->GetOriginalProfile());
}

}  // namespace xr
