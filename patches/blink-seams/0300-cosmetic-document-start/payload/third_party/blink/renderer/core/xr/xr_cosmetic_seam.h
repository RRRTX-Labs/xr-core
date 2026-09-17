// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P12 cosmetic seam (patch 0300) — the guarded entry point.
//
// This header is the ONLY thing the upstream file includes. Every call site in
// the patch goes through one of these functions, and every one of them is a
// no-op unless ENABLE_XR_COSMETIC is defined, so an unguarded call site would
// still not execute anything — but tools/blink_guard_lint.py requires the guard
// at the call site anyway, because "the guard is inside the callee" is a
// property a reader cannot verify from the diff, and a future edit to the
// callee would silently make the hook live.
//
// NO CHROMIUM BEHAVIOUR IS CLAIMED HERE. gn and ninja are absent from the
// environment this was written in, so this file has never been compiled against
// Blink. It is the payload half of a patch whose round-trip (apply → revert
// byte-exact → anchor perturbation → budget growth) is proven by
// xr-browser/build/webui/cosmetic_seam_roundtrip.py at the pin.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_XR_XR_COSMETIC_SEAM_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_XR_XR_COSMETIC_SEAM_H_

namespace blink {
class Document;
class LocalFrame;
}  // namespace blink

namespace blink::xr {

// The document-start entry point. Called from Document::WillInsertBody
// (patch 0300) inside `#if defined(ENABLE_XR_COSMETIC)`.
//
// What "document-start" means here, precisely, because the phrase is load-bearing:
//   * It is BEFORE first layout. Document::ShouldScheduleLayout() refuses to
//     schedule layout until "we have a body element, or parsing has finished
//     and we don't have a body element", so nothing can paint before the body
//     exists, and this fires immediately before the body is inserted.
//   * It is AFTER the root element exists — `<html>` is already parsed, so the
//     frame's site and identity are available for the scope key.
//   * It is AFTER `<head>` scripts have run. That is the honest limit of this
//     seam and it is why scriptlet injection is NOT wired to it: injecting a
//     page script here would run it after the head-phase scripts it exists to
//     pre-empt. Element hiding is unaffected (head content is not renderable),
//     and scriptlet EXECUTION is off in P12 anyway
//     (`xr_shield_scriptlets` false, `main_world_permitted: false`), so the gap
//     is not yet consequential. Closing it is a separate seam.
//   * A document with no `<body>` (SVG, XML) never reaches this hook, so the
//     cosmetic set is never installed there. That is fail-open: the document
//     renders unstyled. Upstream's own ShouldScheduleLayout() branch for
//     "parsing has finished and we don't have a body element" confirms such
//     documents exist rather than treating them as an error.
//
// Deliberately takes only what the key derivation needs — the document, whose
// site and identity class come from the frame's own security origin, never from
// an embedder. The embedder is not an input to the scope key
// (renderer/cosmetic/core/scope_key.h), and a seam that passed one would make
// the cross-origin leak structural rather than merely possible.
//
// Returns without doing anything when:
//   * the feature flag is off (the default — no call site is active and no
//     observer is installed, which is the off state asserted identical to
//     today's product);
//   * the frame is not a local main frame or has no committed origin;
//   * the key set for this scope is empty (no rules, no work);
//   * the blob for this scope is missing, invalid, or scope-mismatched.
//
// Every one of those is a degrade outcome from the single truth table in
// renderer/cosmetic/core/degrade.h, not an ad-hoc early return, so the debug
// page and the tests read the same table the seam does.
void OnDocumentStart(blink::Document* document);

// The DOM-mutation observer callback, installed only when
// ShouldInstallObserver() is true. Throttled under kDomMutationStorm rather
// than dropped, so a busy page still converges.
void OnDomMutated(blink::LocalFrame* frame);

// The guard state, for the debug page's seam row. Returns the compile-time
// guard, the runtime flag, and the last degrade condition — three different
// things, because a seam can be compiled in and switched off, and a reader who
// cannot tell those apart cannot diagnose anything.
const char* SeamGuardState();

}  // namespace blink::xr

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_XR_XR_COSMETIC_SEAM_H_
