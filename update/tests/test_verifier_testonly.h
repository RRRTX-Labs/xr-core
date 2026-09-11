// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// TEST-ONLY SignatureVerifier fixture (update/tests ONLY — see verifier.h).
// Deterministic: a signature is valid iff it equals "sig:" + first 16 hex
// of sha256(public_key + "|" + message). This is a FIXTURE for policy testing (any message
// change invalidates the signature), NOT a signature scheme, NOT a MAC, and
// NOT linked into anything but tests. The production binding is the
// documented farm row (docs/release/updater-integration.md). The host
// façade (update_host.cc) carries its own copy of the same stub, labeled
// TEST-ONLY in every verify result.
#pragma once

#include <string>

#include "update/core/sha256.h"
#include "update/core/verifier.h"

namespace xr::update {

class TestVerifierTestonly final : public SignatureVerifier {
 public:
  bool Verify(const std::string& public_key, const std::string& message,
              const std::string& signature) const override {
    // Key material participates: signing under a different key invalidates
    // the fixture signature (so key substitution is observable in tests).
    return signature ==
           "sig:" + Sha256Hex(public_key + "|" + message).substr(0, 16);
  }
  bool AllowedForChannel(const std::string& channel) const override {
    return channel == "dev" || channel == "nightly-test";
  }
};

// A verifier that refuses EVERY channel — the negative fixture for
// "TEST-ONLY cannot leak into production" (proves the refusal is policy,
// not accidental).
class RefusingVerifierTestonly final : public SignatureVerifier {
 public:
  bool Verify(const std::string&, const std::string&,
              const std::string&) const override {
    return false;
  }
  bool AllowedForChannel(const std::string&) const override { return false; }
};

}  // namespace xr::update
