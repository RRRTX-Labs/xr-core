// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the SignatureVerifier INTERFACE for the update core (P10-T1).
//
// The division of labor is the whole point of this file:
//   * verification POLICY is ours — which key, which epoch, monotonicity,
//     replay, downgrade, TLS-independence — and lives in verify_policy.cc;
//   * the verification PRIMITIVE is not ours to invent (DR-11 "zero custom
//     crypto"; governance law: no invented crypto). This interface is the
//     only seam where a primitive enters: `verify(pubkey, message, sig)`.
//
// Bindings:
//   * PRODUCTION: the in-tree //chrome/updater crypto or minisign — a
//     documented farm/human row (docs/release/updater-integration.md), NOT
//     an in-house scheme. Nothing in xr-core/update implements one.
//   * TEST-ONLY: update/tests/test_verifier_testonly.h binds a stub. It is
//     named *_testonly, lives under update/tests/, and refuses release
//     selection (verify_policy refuses a test verifier for a release
//     channel; the refusal is tested).
#pragma once

#include <string>

namespace xr::update {

class SignatureVerifier {
 public:
  virtual ~SignatureVerifier() = default;

  // Verify `signature` over `message` with `public_key` (pinned-key ids map
  // to key material OUTSIDE this core — key selection is policy; key bytes
  // are injected). Implementations wrap a standard, reviewed primitive.
  // Returns false on any failure; no error taxonomy crosses this seam.
  virtual bool Verify(const std::string& public_key,
                      const std::string& message,
                      const std::string& signature) const = 0;

  // True when this verifier may sign/verify for `channel`. The TEST-ONLY
  // binding answers false for every release channel; that refusal is a
  // tested law (test_verify_policy), not a comment.
  virtual bool AllowedForChannel(const std::string& channel) const = 0;
};

}  // namespace xr::update
