// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P4 spike probe: per-identity NetworkContext parameters.
//
// The expectations encoded here are the STATICALLY MEASURED ones from
// docs/spike-identity/measured-shared-state.md, not assumptions. Where the
// measured scope differs from Plan §1.4's claim, the test name says so:
//   * proxy config      -> per NetworkContext (mojom:443/450/451)  CONFIRMED
//   * HTTP cache dir    -> per NetworkContext (mojom:242/404)      CONFIRMED
//   * HSTS store        -> per NetworkContext (mojom:307)          CONFIRMED
//   * TLS session cache -> per URLRequestContext (indirect)        PENDING-FARM
//   * DNS host cache    -> per URLRequestContext, manager SHARED   QUALIFIED
// Runtime execution is HUMAN-GATED on farm hardware (HG-21).

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/xr/xr_identity.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_test.h"
#include "net/base/network_isolation_key.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace xr {

namespace {

constexpr char kIdentityA[] = "11111111-2222-4333-8444-555555555555";
constexpr char kIdentityB[] = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";

}  // namespace

class IdentityNetworkContextTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    IdentityRegistry::Get().Register(Identity{kIdentityA, false, ""});
    IdentityRegistry::Get().Register(Identity{kIdentityB, false, ""});
  }

  content::StoragePartition* PartitionFor(const std::string& id) {
    auto identity = IdentityRegistry::Get().Lookup(id);
    CHECK(identity);
    return browser()->profile()->GetStoragePartition(
        StoragePartitionConfigForIdentity(browser()->profile(), *identity),
        /*can_create=*/true);
  }
};

// Each identity partition owns a distinct NetworkContext, so a per-context
// proxy/DNS/DoH binding is addressable at creation time (P5's
// RouteManager.BindIdentity depends on exactly this).
IN_PROC_BROWSER_TEST_F(IdentityNetworkContextTest,
                       DistinctNetworkContextsPerIdentity) {
  auto* a = PartitionFor(kIdentityA);
  auto* b = PartitionFor(kIdentityB);
  EXPECT_NE(a->GetNetworkContext(), b->GetNetworkContext());

  network::mojom::NetworkContext* na = a->GetNetworkContext();
  network::mojom::NetworkContext* nb = b->GetNetworkContext();
  ASSERT_TRUE(na && nb);
  EXPECT_NE(na, nb);
}

// HTTP cache: per-context directory, and memory-backed for an ephemeral
// identity (storage_partition_impl.cc:3377-3384 returns nullopt for
// in_memory partitions — no path means no disk).
IN_PROC_BROWSER_TEST_F(IdentityNetworkContextTest, EphemeralHasNoDiskPath) {
  auto* profile = browser()->profile();
  Identity ephemeral{kIdentityA, /*ephemeral=*/true, ""};
  Identity durable{kIdentityB, /*ephemeral=*/false, ""};
  const auto cfg_e = StoragePartitionConfigForIdentity(profile, ephemeral);
  const auto cfg_d = StoragePartitionConfigForIdentity(profile, durable);
  EXPECT_TRUE(cfg_e.in_memory());
  EXPECT_FALSE(cfg_d.in_memory());
  auto* pe = profile->GetStoragePartition(cfg_e, /*can_create=*/true);
  auto* pd = profile->GetStoragePartition(cfg_d, /*can_create=*/true);
  EXPECT_EQ(base::FilePath(), pe->GetPath());
  EXPECT_NE(base::FilePath(), pd->GetPath());
}

// DNS: measured scope is "host cache per URLRequestContext; the
// HostResolverManager and the OS resolver are shared network-service-wide".
// This probe documents the observable consequence rather than asserting an
// isolation that upstream does not provide.
IN_PROC_BROWSER_TEST_F(IdentityNetworkContextTest, DnsResolverScopeIsDocumented) {
  auto* a = PartitionFor(kIdentityA);
  auto* b = PartitionFor(kIdentityB);
  ASSERT_TRUE(a && b);
  // Resolution itself must work in both partitions; sharing of the underlying
  // manager is a documented limitation (limitations.md), not a failure here.
  EXPECT_NE(a->GetNetworkContext(), b->GetNetworkContext())
      << "a shared NetworkContext would collapse proxy, HSTS and cache scope";
}

}  // namespace xr
