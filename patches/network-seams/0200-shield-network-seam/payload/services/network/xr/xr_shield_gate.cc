// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// See xr_shield_gate.h for the seam contract. This translation unit is the
// THIN DATA PATH only: redact -> consult -> map. No policy, no parsing of
// lists, no decision logic (all of that is xr-core/shield/core behind the
// BlockingEngine injection contract; the vendored adblock-rust engine sits
// behind the xr_shield_engine C ABI the //xr glue installs here).

#include "services/network/xr/xr_shield_gate.h"

#include <atomic>
#include <cstddef>

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/url_request/url_request.h"
#include "url/gurl.h"

namespace network::xr {
namespace {

// Process-wide installation (the network service process hosts one engine
// in v1). Atomics: the load path runs on IO threads, the glue installs
// once at startup. Relaxed ordering is sufficient — the gate is a
// best-effort consult and every torn/uninstalled read fails OPEN.
std::atomic<void*> g_engine{nullptr};
std::atomic<XrShieldGate::ConsultFn> g_consult{nullptr};

std::atomic<unsigned long> g_consults{0};
std::atomic<unsigned long> g_blocks{0};
std::atomic<unsigned long> g_fail_opens{0};
std::atomic<unsigned long> g_contexts{0};

// The v1 redaction (ABI law): scheme://host/path, lowercased, port /
// query / fragment stripped. GURL already lowercases scheme and host;
// the path is lowercased here to match the engine's match surface (the
// same normalization xr-core/shield/core/match.cc applies corpus-wide).
// Empty result == redaction failure == fail open.
std::string RedactMatchUrl(const GURL& url) {
  if (!url.is_valid() || url.host().empty()) {
    return std::string();
  }
  std::string path = url.path();
  for (char& c : path) {
    c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  }
  return url.scheme() + "://" + url.host() + path;
}

std::string LowerHost(const GURL& url) { return url.host(); }

std::string RegistrableDomain(const GURL& url) {
  return net::registry_controlled_domains::GetDomainAndRegistry(
             url, net::registry_controlled_domains::
                      INCLUDE_PRIVATE_REGISTRIES)
      .string();
}

}  // namespace

void XrShieldGate::InstallEngine(void* engine, ConsultFn consult) {
  g_engine.store(engine, std::memory_order_relaxed);
  g_consult.store(consult, std::memory_order_relaxed);
}

void XrShieldGate::OnNetworkContextCreated(NetworkContext* context) {
  (void)context;  // counted attachment point; per-context engines: future.
  g_contexts.fetch_add(1, std::memory_order_relaxed);
}

bool XrShieldGate::ShouldBlock(const net::URLRequest& request) {
  g_consults.fetch_add(1, std::memory_order_relaxed);
  void* engine = g_engine.load(std::memory_order_relaxed);
  ConsultFn consult = g_consult.load(std::memory_order_relaxed);
  if (engine == nullptr || consult == nullptr) {
    // Fail-open (posture law): no engine installed — browsing continues,
    // the posture core reads FailOpens() and turns the chip amber.
    g_fail_opens.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const GURL& url = request.url();
  std::string match_url = RedactMatchUrl(url);
  if (match_url.empty()) {
    g_fail_opens.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  std::string host = LowerHost(url);
  std::string path = url.path();
  std::string domain = RegistrableDomain(url);
  // The engine contract: true only for a blocking verdict; 0/no-opinion
  // is allow-by-default. Panics never cross the ABI (the shim catches
  // them and reports death via xr_shield_engine_alive — the glue
  // uninstalls on death, and from then on this gate fails open).
  bool block = consult(engine, match_url.c_str(), host.c_str(),
                       path.c_str(), domain.c_str());
  if (block) {
    g_blocks.fetch_add(1, std::memory_order_relaxed);
  }
  return block;
}

unsigned long XrShieldGate::Consults() {
  return g_consults.load(std::memory_order_relaxed);
}
unsigned long XrShieldGate::Blocks() {
  return g_blocks.load(std::memory_order_relaxed);
}
unsigned long XrShieldGate::FailOpens() {
  return g_fail_opens.load(std::memory_order_relaxed);
}
unsigned long XrShieldGate::ContextsCreated() {
  return g_contexts.load(std::memory_order_relaxed);
}

}  // namespace network::xr
