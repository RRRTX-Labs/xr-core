// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P4 spike probe: the storage isolation matrix across two identity partitions.
// Graduates into the Plan §11.4 isolation matrix — every case emits a
// spike-result-v1 row (docs/contracts/spike-result-v1.md) so the farm output
// is schema-stable and comparable with future runs.

#include <string>

#include "base/json/json_writer.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/xr/xr_identity.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace xr {

namespace {

// RFC 2606 reserved TLD: no probe ever touches a real domain.
constexpr char kSiteA[] = "https://xr-a.example/";
constexpr char kSiteB[] = "https://xr-b.example/";

// Two identities, UUIDv4-shaped (xr_identity.cc validates the shape).
constexpr char kIdentityA[] = "11111111-2222-4333-8444-555555555555";
constexpr char kIdentityB[] = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";

base::Value::Dict Row(const std::string& probe,
                      const std::string& claim,
                      const std::string& measured,
                      bool isolated) {
  base::Value::Dict row;
  row.Set("probe", probe);
  row.Set("claim", claim);
  row.Set("expectation", "identity partitions do not share this state");
  row.Set("measured", measured);
  row.Set("source", "runtime");
  row.Set("verdict", isolated ? "PASS" : "FAIL");
  return row;
}

}  // namespace

class IdentityIsolationMatrixTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(IdentityRegistry::Get().Register(
        Identity{kIdentityA, /*ephemeral=*/false, ""}));
    ASSERT_TRUE(IdentityRegistry::Get().Register(
        Identity{kIdentityB, /*ephemeral=*/false, ""}));
  }

  content::StoragePartitionConfig ConfigFor(const std::string& id) {
    auto identity = IdentityRegistry::Get().Lookup(id);
    CHECK(identity.has_value());
    return StoragePartitionConfigForIdentity(browser()->profile(), *identity);
  }

  std::vector<base::Value::Dict> rows_;
};

// The seam itself: two identities in one Profile resolve to two different,
// non-default StoragePartitions.
IN_PROC_BROWSER_TEST_F(IdentityIsolationMatrixTest, PartitionsAreDistinct) {
  const auto a = ConfigFor(kIdentityA);
  const auto b = ConfigFor(kIdentityB);
  EXPECT_FALSE(a.is_default());
  EXPECT_FALSE(b.is_default());
  EXPECT_NE(a.partition_domain(), b.partition_domain());
  EXPECT_NE(a, b);
  EXPECT_NE(a.partition_domain(),
            content::StoragePartitionConfig::CreateDefault(
                browser()->profile()).partition_domain());
  rows_.push_back(Row("T2.partitions", "identity -> distinct partition domain",
                      base::StringPrintf("%s vs %s", a.partition_domain().c_str(),
                                         b.partition_domain().c_str()),
                      a != b));
}

// Storage keys: the partition owns LocalStorage / IndexedDB / CacheStorage /
// ServiceWorker / cookies. This test asserts the *ownership* (distinct
// StoragePartition objects); the per-API behaviour is asserted by the web
// platform tests once a farm browser exists.
IN_PROC_BROWSER_TEST_F(IdentityIsolationMatrixTest, StorageBackendsAreDistinct) {
  auto* profile = browser()->profile();
  auto* pa = profile->GetStoragePartition(ConfigFor(kIdentityA),
                                          /*can_create=*/true);
  auto* pb = profile->GetStoragePartition(ConfigFor(kIdentityB),
                                          /*can_create=*/true);
  EXPECT_NE(pa, pb);
  EXPECT_NE(pa->GetPath(), pb->GetPath());
  rows_.push_back(Row("T2.storage", "distinct StoragePartition objects + paths",
                      base::StringPrintf("%p vs %p", pa, pb), pa != pb));
}

// Same site, two identities: the partition differs even though the URL does
// not. This is the row that the (context, site)-keyed embedder override
// CANNOT satisfy — see S-01 in measured-shared-state.md.
IN_PROC_BROWSER_TEST_F(IdentityIsolationMatrixTest, SameSiteDifferentPartition) {
  auto* profile = browser()->profile();
  const GURL url(kSiteA);
  auto identity_a = IdentityRegistry::Get().Lookup(kIdentityA);
  auto identity_b = IdentityRegistry::Get().Lookup(kIdentityB);
  ASSERT_TRUE(identity_a && identity_b);
  auto si_a = SiteInstanceForIdentity(profile, url, *identity_a);
  auto si_b = SiteInstanceForIdentity(profile, url, *identity_b);
  ASSERT_TRUE(si_a && si_b);
  EXPECT_NE(si_a->GetStoragePartitionConfig(),
            si_b->GetStoragePartitionConfig());
  rows_.push_back(Row("T2.same_site",
                      "same site + different identity => different partition",
                      "two fixed-partition SiteInstances for the same URL",
                      si_a->GetStoragePartitionConfig() !=
                          si_b->GetStoragePartitionConfig()));
}

}  // namespace xr
