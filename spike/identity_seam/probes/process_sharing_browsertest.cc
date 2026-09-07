// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P4 spike probe: renderer-process sharing ACROSS identity partitions.
// Law (L2 applied to the spike itself): these probes must run with site
// isolation ON. build/spike/probe_driver.py refuses to launch a binary with
// --disable-site-isolation-trials or any --disable-features=...Site... flag,
// and that refusal is unit-tested.

#include "base/command_line.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/xr/xr_identity.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/site_instance.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace xr {

namespace {

constexpr char kSite[] = "https://xr-a.example/";
constexpr char kIdentityA[] = "11111111-2222-4333-8444-555555555555";
constexpr char kIdentityB[] = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";

}  // namespace

class IdentityProcessSharingTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    IdentityRegistry::Get().Register(Identity{kIdentityA, false, ""});
    IdentityRegistry::Get().Register(Identity{kIdentityB, false, ""});
  }
};

// Precondition: the probes are only meaningful with isolation enabled. A run
// that disabled it must be treated as no evidence at all, not as a pass.
IN_PROC_BROWSER_TEST_F(IdentityProcessSharingTest, IsolationIsEnabled) {
  const auto& cmd = *base::CommandLine::ForCurrentProcess();
  EXPECT_FALSE(cmd.HasSwitch(switches::kDisableSiteIsolationTrials));
  EXPECT_FALSE(cmd.HasSwitch("disable-site-isolation-trials"));
  for (const auto& feature :
       cmd.GetSwitchValueASCII("disable-features")) {
    EXPECT_EQ(std::string::npos, feature.find("Isolation"))
        << "site-isolation feature disabled; this probe is void";
  }
}

// Two identities on the SAME site must not land in the same BrowsingInstance:
// content/browser/browsing_instance.cc:183 CHECK_EQ()s the StoragePartition
// across a BrowsingInstance, so sharing one would be a CHECK failure.
IN_PROC_BROWSER_TEST_F(IdentityProcessSharingTest,
                       SameSiteDifferentIdentitySeparateBrowsingInstances) {
  auto* profile = browser()->profile();
  auto a = IdentityRegistry::Get().Lookup(kIdentityA);
  auto b = IdentityRegistry::Get().Lookup(kIdentityB);
  ASSERT_TRUE(a && b);
  auto si_a = SiteInstanceForIdentity(profile, GURL(kSite), *a);
  auto si_b = SiteInstanceForIdentity(profile, GURL(kSite), *b);
  ASSERT_TRUE(si_a && si_b);
  EXPECT_FALSE(si_a->IsRelatedSiteInstance(si_b.get()))
      << "two identities on one site shared a BrowsingInstance — the seam is "
         "broken; see ADR-0042 falsification trigger F1";
}

// The partition survives a cross-site navigation inside the same identity:
// browsing_instance.cc:264-267 propagates the BrowsingInstance's config to
// SiteInstances created by later navigations.
IN_PROC_BROWSER_TEST_F(IdentityProcessSharingTest, PartitionSurvivesCrossSite) {
  auto* profile = browser()->profile();
  auto a = IdentityRegistry::Get().Lookup(kIdentityA);
  ASSERT_TRUE(a);
  auto first = SiteInstanceForIdentity(profile, GURL(kSite), *a);
  ASSERT_TRUE(first);
  const auto expected = first->GetStoragePartitionConfig();
  auto second = first->GetRelatedSiteInstance(GURL("https://xr-b.example/"));
  ASSERT_TRUE(second);
  EXPECT_EQ(expected, second->GetStoragePartitionConfig())
      << "cross-site navigation escaped the identity partition — "
         "ADR-0042 falsification trigger F2";
}

}  // namespace xr
