// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — ALIAS SHIM to the single shared strict-JSON model/parser/
// canonical serializer (common/core/json.h; P11-T0-b — before this, five
// byte-identical per-core copies differed only by the namespace word). This
// file carries NO implementation; it re-exports the xr::common symbols into
// xr::commands so this core's call sites stay stable. The parser edge corpus
// (common/tests/test_json.cc) runs in the common lane; every suite lane
// additionally compiles the shared SHA-256 KAT.
#pragma once

#include "common/core/json.h"

namespace xr::commands {
using common::CanonicalJsonString;
using common::JsonParseResult;
using common::JsonValue;
using common::ParseJson;
}  // namespace xr::commands
