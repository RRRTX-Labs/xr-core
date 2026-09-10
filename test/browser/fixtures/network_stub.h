// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Controllable network-stub fixture (P9-T1). A browser test that touches the
// network is a flaky leak-test; every XR browser test therefore gets a stubbed
// resolver and a pinned network state. Farm-executed; lint-only here.
//
// Attach points at pin d04cdb24d67b081f6cf80200ffc5233f44b61109:
//   net/dns/mock_host_resolver.h:69        rules()->AddRule("foo.com","1.2.3.4")
//   net/dns/mock_host_resolver.h:185       AddRule(hostname_pattern, ip_literal)
//   net/base/mock_network_change_notifier.h:19  class MockNetworkChangeNotifier
#ifndef XR_TEST_BROWSER_FIXTURES_NETWORK_STUB_H_
#define XR_TEST_BROWSER_FIXTURES_NETWORK_STUB_H_

#include <map>
#include <memory>
#include <string>

namespace content {
class BrowserContext;
}

namespace xr::test {

// Installs a MockHostResolverBase rule set (and a mock notifier) scoped to a
// test. Hosts not listed here resolve to a deterministic failure — the
// fail-closed network law (§9.11) applies to tests too: an unmapped host is
// an error the test must declare, not an accidental live fetch.
class NetworkStub {
 public:
  NetworkStub();
  ~NetworkStub();

  NetworkStub(const NetworkStub&) = delete;
  NetworkStub& operator=(const NetworkStub&) = delete;

  // Map a host pattern to a loopback/literal address. Returns *this.
  NetworkStub& MapHost(const std::string& hostname_pattern,
                       const std::string& ip_literal);

  // Pin the network state (online/offline) for the scope of the fixture.
  NetworkStub& SetOnline(bool online);

  // Install into the given context. Returns false on failure.
  bool Install(content::BrowserContext* context);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  std::map<std::string, std::string> rules_;
  bool online_ = true;
};

}  // namespace xr::test

#endif  // XR_TEST_BROWSER_FIXTURES_NETWORK_STUB_H_
