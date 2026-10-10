// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// identity/core/ledger_tag — the identity overlay for ledger, history and
// bookmark rows (P14-T5; plan §4 P14 T5 "history/bookmarks identity tagging
// via ledger overlay (upstream history untouched; omnibox results
// filterable)", contracts-out "ledger identity_id everywhere").
//
// The contract this core implements is `ledger-identity-overlay-v1`
// (xr-browser docs/contracts/ledger-identity-overlay-v1.{md,schema.json},
// registered as an amendment record in registry-post-freeze.md). It does NOT
// edit any event class's own bytes: block-event-v1, policy-change-event-v1 and
// permission-audit-event-v1 keep their schemas and vectors byte-identical. The
// overlay record WRAPS one event of any class and REQUIRES an identity_id.
// That is the plan's own wording ("via ledger overlay"), and it is why the
// frozen ActivityRow (which already carries `IdentityId identity`) needs no
// amendment either.
//
// Laws (each is a test in identity/tests/test_ledger_tag.cc and a vector in
// docs/contracts/vectors/ledger-identity-overlay-v1.json):
//   L1  identity_id is REQUIRED and must have the minted domain shape. An
//       absent, empty or malformed id is a refusal, never a default.
//   L2  event_class is a CLOSED set: the eight frozen ActivityKind values plus
//       the two overlay classes kHistory and kBookmark. Unknown means refused.
//   L3  the wrapped event must be a JSON object, carried byte-for-byte in
//       canonical form (the overlay never rewrites an event).
//   L4  omnibox filtering: a query in identity A returns only rows tagged A.
//       A row tagged B is dropped without being counted (a count would say
//       that B exists). A row with no valid identity_id is UNKNOWN. It goes to
//       a separate `unknown` list with provenance "unknown" and is never put in
//       the current identity's list.
//   L5  upstream history untouched: the overlay lives in its own store, keyed
//       by (url_id, visit_id). The upstream rows' canonical bytes are the same
//       whether or not the overlay is written (the zero-delta oracle).
//
// Pure: no clock, no I/O, no RNG. Std-only C++20 plus the shared common/core
// JSON value (itself std-only).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "common/core/json.h"

namespace xr::identity {

inline constexpr std::string_view kLedgerOverlaySchema =
    "ledger-identity-overlay-v1";

// The closed event-class set (L2), in canonical order.
const std::vector<std::string>& LedgerEventClasses();
bool IsLedgerEventClass(std::string_view cls);

// L1+L2+L3. On success `*out` is the canonical overlay record and the return
// is true. On refusal `*refusal` is one of:
//   "identity-id-required", "identity-id-malformed",
//   "unknown-event-class:<cls>", "event-not-object".
bool TagEvent(const xr::common::JsonValue& request, xr::common::JsonValue* out,
              std::string* refusal);

// L4. `rows` is an array of overlay-shaped rows ({identity_id?, ...}).
// Returns {"current": [...], "identity_id": <current>, "unknown": [...]}.
// Refuses ("identity-id-required"/"identity-id-malformed") when the CURRENT
// identity is not a valid id: a query with no identity has nobody to filter
// for, so it sees nothing rather than everything.
bool FilterOmnibox(const xr::common::JsonValue& request,
                   xr::common::JsonValue* out, std::string* refusal);

// L5. The differential oracle over the history read/write path for one user.
// request: {identity_id, upstream_rows:[{url_id,visit_id,url,title,ts}...]}.
// Returns {"overlay_rows": n, "upstream_after": <canonical sha-free bytes>,
// "upstream_before": <...>, "verdict": "diff-clean"|"DELTA"}. The overlay
// write goes to a separate vector, and the comparison is over canonical bytes.
bool HistoryOracle(const xr::common::JsonValue& request,
                   xr::common::JsonValue* out, std::string* refusal);

}  // namespace xr::identity
