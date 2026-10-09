// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — ALIAS SHIM to the single shared strict-JSON model/parser/
// canonical serializer (common/core/json.h). Same precedent as
// policy/core/json.h (P11-T0-b, ADR-0043): NO implementation here, and no
// second JSON parser anywhere in P15 (tools/no_new_crypto_check.py law).
#pragma once

#include "common/core/json.h"

namespace xr::permissions {
using common::CanonicalJsonString;
using common::JsonParseResult;
using common::JsonValue;
using common::ParseJson;
}  // namespace xr::permissions
