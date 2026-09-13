// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the services/network XR Shield seam (P11 patch 0200, category
// network_seams): the ONLY consult point between the upstream network
// service and the XR shield decision engine (xr-core/shield/core —
// BlockingEngine injection contract; the vendored adblock-rust engine
// rides the xr_shield_engine C ABI, xr-core/shield/engine/
// xr_shield_engine.h). This gate is a THIN DATA PATH: it redacts the
// request to the v1 match surface (scheme://host/path, lowercased,
// port/query/fragment stripped), asks the injected engine, and maps the
// answer onto the load. All policy — lists, scopes, exceptions, posture —
// lives in xr-core; nothing here decides anything.
//
// Posture law (xr-core/shield/core/posture.h): engine death or absence is
// FAIL-OPEN for browsing (ShouldBlock returns false; the count is kept so
// the posture core can turn the chip amber). Route loss — the consult call
// site itself disappearing from url_loader.cc — is FAIL-CLOSED and is
// caught at build/rebase time by the round-trip markers, never at runtime.
//
// Scope note: v1 hooks the FIRST-PACKET consult in URLLoader::
// ScheduleStart (before any renderer exists — the ordering law of P11).
// Redirect restarts (URLLoader::ResumeStart) re-enter after the consult
// has already ruled on the original request; per-hop redirect re-consult
// is deliberately future work (see patchinfo rebase-notes).
#pragma once

namespace net {
class URLRequest;
}

namespace network {

class NetworkContext;

namespace xr {

// The network-service-side shield gate. All methods are process-wide: the
// network service process hosts one engine installation (per-profile
// engines are a post-v1 concern; OnNetworkContextCreated is the recorded
// attachment point for that growth).
class XrShieldGate {
 public:
  // The glue-injected consult (//xr startup glue, farm side). Signature
  // mirrors xr_shield_engine_match's redacted inputs exactly:
  //   match_url          "scheme://host/path", lowercased
  //   host               lowercased host, no port
  //   path               percent-encoded path as GURL sees it
  //   registrable_domain eTLD+1 (private registries included) or ""
  // Returns true ONLY for a blocking verdict; allow-by-default is the
  // engine contract (0 == no opinion == proceed). `engine` is the opaque
  // XrShieldEngine* handle; the gate never interprets it.
  using ConsultFn = bool (*)(void* engine, const char* match_url,
                             const char* host, const char* path,
                             const char* registrable_domain);

  // Called ONCE by the //xr glue after the signed bundle has been loaded
  // and verified (xr-core/xr-lists pipeline) and the engine created
  // through the C ABI. Until installed — and after UninstallEngine —
  // every consult fails open and is counted (posture: amber, browsing
  // continues). Passing nullptr for `consult` uninstalls.
  static void InstallEngine(void* engine, ConsultFn consult);

  // Recorded attachment point: the network service notifies the gate that
  // a NetworkContext came up (counted; per-context engines are future).
  static void OnNetworkContextCreated(NetworkContext* context);

  // The load-path consult (url_loader.cc, ScheduleStart). Fail-open on
  // every non-verdict: no engine, no glue, redaction failure.
  static bool ShouldBlock(const net::URLRequest& request);

  // Counters for the posture core / xr://shield dev page (count-only; no
  // URLs, no origins — the redaction boundary is the gate itself).
  static unsigned long Consults();
  static unsigned long Blocks();
  static unsigned long FailOpens();
  static unsigned long ContextsCreated();
};

}  // namespace xr
}  // namespace network
