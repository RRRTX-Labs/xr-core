// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — SHA-256 (FIPS 180-4), the SINGLE in-tree copy (P11-T0-b;
// ADR-0043 "one copy of a public algorithm, never a new one"). A standard,
// public algorithm implemented verbatim from the specification — NOT an
// invented primitive. Purpose: corruption DETECTION on versioned blobs
// (policy snapshots, update manifests, bundle digests, counters) — defense
// in depth next to schema-strict decode.
//
// BOUNDARY (the law this header states for every consumer): this is NOT an
// authenticity mechanism. Authenticity lives behind the injected
// SignatureVerifier (update/core/verifier.h, the P10 pinned-key path) and
// the release signing scaffold — never behind a bare digest. A second copy
// of this file anywhere in the tree is a DEFECT (tools/no_new_crypto_check.py
// reddens; the shared FIPS KAT — common/tests/test_sha256_kat.cc — is
// compiled into every consumer suite so coverage cannot desynchronize).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace xr::common {

// 32-byte digest, hex-encoded lowercase (64 chars).
std::string Sha256Hex(std::string_view data);

// Raw 32-byte digest.
std::array<uint8_t, 32> Sha256(std::string_view data);

}  // namespace xr::common
