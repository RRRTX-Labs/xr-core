// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Network-stub fixture (P9-T1). Farm-executed implementation.
#include "test/browser/fixtures/network_stub.h"

#include "content/public/browser/browser_context.h"

namespace xr::test {

class NetworkStub::Impl {
 public:
  // Farm-side: installs a MockHostResolverBase rule set
  // (net/dns/mock_host_resolver.h:69) + MockNetworkChangeNotifier
  // (net/base/mock_network_change_notifier.h:19) scoped to the test.
};

NetworkStub::NetworkStub() : impl_(std::make_unique<Impl>()) {}
NetworkStub::~NetworkStub() = default;

NetworkStub& NetworkStub::MapHost(const std::string& hostname_pattern,
                                  const std::string& ip_literal) {
  rules_[hostname_pattern] = ip_literal;
  return *this;
}

NetworkStub& NetworkStub::SetOnline(bool online) {
  online_ = online;
  return *this;
}

bool NetworkStub::Install(content::BrowserContext* /*context*/) {
  return false;  // farm path — never a simulated pass
}

}  // namespace xr::test
