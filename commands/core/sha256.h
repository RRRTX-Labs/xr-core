// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — ALIAS SHIM to the single shared SHA-256 (common/core/sha256.h;
// P11-T0-b, ADR-0043: standard public algorithms live in-tree exactly once,
// in an S0-owned location; a second copy is a defect — the boundary law and
// the "not an authenticity mechanism" note live on the shared header). This
// file carries NO implementation; it re-exports the xr::common symbols into
// xr::commands so this core's call sites stay stable. The shared FIPS KAT
// (common/tests/test_sha256_kat.cc) runs inside THIS core's suite lane.
#pragma once

#include "common/core/sha256.h"

namespace xr::commands {
using common::Sha256;
using common::Sha256Hex;
}  // namespace xr::commands
