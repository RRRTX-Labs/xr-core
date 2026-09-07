// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — mojom-side adapter for xr.mojom.PolicyResolver (P6-T3
// service half). FARM-GATED (HG-29): the entire declaration compiles ONLY
// when XR_HAVE_MOJOM_BINDINGS is defined (the GN target that defines it is
// commented out in policy/BUILD.gn until the bindings exist on a farm
// branch). Off-farm, this header is inert — the std C++ service
// (service.h) plus the stdio façade (host/policy_host.cc) are the
// testable-today halves, and the parity suite keeps them honest against
// the frozen fake.
#pragma once

#ifdef XR_HAVE_MOJOM_BINDINGS

#include "mojo/public/cpp/bindings/receiver.h"
#include "policy/core/service.h"
#include "xr/mojom/policy_resolver.mojom.h"

namespace xr::policy {

// Implements xr.mojom.PolicyResolver by delegating to the SAME pure core
// the stdio façade uses — there is exactly one brain (L3). Purity note:
// mojom calls arrive on the IO thread; resolution itself is pure, so
// sequencing is trivially safe. The receiver never fabricates policies:
// every response comes from Resolve() or is the deny-default.
class PolicyResolverServiceImpl : public xr::mojom::PolicyResolver {
 public:
  explicit PolicyResolverServiceImpl(std::string store_dir);
  ~PolicyResolverServiceImpl() override;

  // xr::mojom::PolicyResolver:
  void Resolve(xr::mojom::IdentityIdPtr identity,
               xr::mojom::OriginKeyPtr origin,
               absl::optional<xr::mojom::TrustContext> trust_context,
               xr::mojom::RequestClass request_class,
               ResolveCallback callback) override;

 private:
  PolicyResolverService service_;  // the std-only core service (service.h)
};

}  // namespace xr::policy

#endif  // XR_HAVE_MOJOM_BINDINGS
