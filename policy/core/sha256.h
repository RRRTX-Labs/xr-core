// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — SHA-256 (FIPS 180-4) for snapshot integrity fields. This is a
// standard, public algorithm implemented verbatim from the specification —
// NOT an invented primitive — and is verified in tests against the official
// FIPS test vectors ("abc", empty string, two-block message). Purpose:
// corruption DETECTION on versioned policy snapshot blobs (defense in depth
// next to schema-strict decode). It is not an authenticity mechanism; the
// authenticity boundary for managed policy is the P2 minisign scaffold
// (see policy/enterprise/managed_source.cc).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace xr::policy {

// 32-byte digest, hex-encoded lowercase (64 chars).
std::string Sha256Hex(std::string_view data);

// Raw 32-byte digest.
std::array<uint8_t, 32> Sha256(std::string_view data);

}  // namespace xr::policy
