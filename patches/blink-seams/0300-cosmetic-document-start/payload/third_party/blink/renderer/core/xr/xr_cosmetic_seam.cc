// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P12 cosmetic seam (patch 0300) — the payload implementation.
//
// NOT COMPILED IN THIS PHASE. gn and ninja are absent from the environment this
// was written in, so this file has never been built against Blink and no
// rendered behaviour is claimed anywhere in this patch. What IS proven is the
// patch's round-trip at the pin (apply → revert byte-exact → anchor
// perturbation → budget growth), by
// xr-browser/build/webui/cosmetic_seam_roundtrip.py.

#include "third_party/blink/renderer/core/xr/xr_cosmetic_seam.h"

#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"

namespace blink::xr {
namespace {

// The runtime flag, read from the compiled argset. Default OFF.
bool CosmeticEnabled() {
#if defined(ENABLE_XR_COSMETIC)
  return true;
#else
  return false;
#endif
}

}  // namespace

void OnDocumentStart(blink::Document* document) {
  if (!document) {
    return;
  }
  if (!CosmeticEnabled()) {
    // The off state: no call site active, no observer installed. This is the
    // state asserted identical to today's product, so it returns before
    // touching the document at all rather than touching it conditionally.
    return;
  }
  blink::LocalFrame* frame = document->GetFrame();
  if (!frame || !frame->IsLocalRoot()) {
    // The key derives from the FRAME's own scope. An OOPIF child frame gets its
    // own key; the embedder's site is never consulted, because keying on the
    // top-level site would leak cross-origin (scope_key.h).
    return;
  }
  // The real derivation lives in xr-core and is reached through the injected
  // engine, exactly as the P11 network seam reaches shield/core. The seam's job
  // is to be thin: it hands over the document and reads back a verdict.
  //
  // Every refusal path below is a row in the degrade truth table, so the debug
  // page and the tests read the same table this function does.
  //
  //   * no key set for this scope  -> kBlobMissing  -> kNoWork
  //   * empty key set              -> kEmptyRuleSet -> kNoWork
  //   * blob invalid or mismatched -> kBlobInvalid / kScopeMismatch -> kDropBlob
  //   * engine unavailable         -> kEngineUnavailable -> kPageUnstyled
  //
  // Fail-OPEN is deliberate and recorded in the table: refusing to hide an ad
  // must never refuse to render a page. That is the one place this surface
  // diverges from the network layer, and it is a divergence on purpose.
  //
  // NOT IMPLEMENTED IN THIS PHASE: the engine binding. There is no
  // xr-core/renderer/cosmetic engine yet — core/ is the decision layer and
  // host/cosmetic_host.cc is its stdio façade. Wiring a real binding requires a
  // Chromium build, which this environment cannot produce, and inventing one
  // here would be exactly the synthetic-result substitution the phase forbids.
}

void OnDomMutated(blink::LocalFrame* frame) {
  if (!frame || !CosmeticEnabled()) {
    return;
  }
  // Throttled under kDomMutationStorm rather than dropped, so a busy page still
  // converges. The throttle state lives with the observer, not here.
  //
  // NOT IMPLEMENTED IN THIS PHASE — see OnDocumentStart.
}

const char* SeamGuardState() {
#if defined(ENABLE_XR_COSMETIC)
  return "compiled-in";
#else
  return "compiled-out";
#endif
}

}  // namespace blink::xr
