// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: SHA-256 (FIPS 180-4) for the update core. This is the same
// standard, public algorithm already shipped in-tree (policy/settings/
// themes cores, byte-identical body, verified in tests against the official
// FIPS test vectors) — NOT an invented primitive and NOT a signature
// mechanism. In this core it serves non-authenticity identification only:
// the replay manifest_id (sha256 of canonical response bytes) and cohort
// bucketing. Authenticity lives exclusively behind the injected
// SignatureVerifier (update/core/verifier.h).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace xr::update {

// 32-byte digest, hex-encoded lowercase (64 chars).
std::string Sha256Hex(std::string_view data);

// Raw 32-byte digest.
std::array<uint8_t, 32> Sha256(std::string_view data);

}  // namespace xr::update
