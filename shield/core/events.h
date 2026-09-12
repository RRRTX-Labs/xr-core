// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/events — the block-event ledger + ring feeding the
// FROZEN mojom surface (mojom/shield.mojom BlockEvent, kContractVersion=1)
// and the living block-event-v1 registry row (P11-T5: the activity-
// ledger emitter — MakeLedgerRow below). Field names and the action
// enum order are FROZEN vocabulary: ts_millis, identity{value}, tab_id,
// origin{scheme,registrable_domain}, target, rule, list_provenance,
// action ∈ blocked|allowed|redirected|upgraded, request_class.
//
// The redaction law lives HERE, at event CREATION: `target` is always
// scheme://host/path — query strings and fragments are stripped before an
// event is written, never at display time. `rule` keeps the filter text
// (provenance for the user's "why was this blocked"), which is list
// content, not browsing data. kUpgraded has no v1 list-rule producer
// (documented deviation; reserved for the future HTTPS-upgrade seam).
//
// The ring is a FIFO with a capacity cap; the RecentEvents VIEW applies a
// second cap: at most kChunkBudgetBytes of canonical event bytes per call
// (the house chunk-budget idiom), newest first.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common/core/json.h"
#include "shield/core/context.h"

namespace xr::shield {

// FROZEN order (mojom/shield.mojom BlockAction).
enum class BlockAction {
  kBlocked = 0,
  kAllowed = 1,
  kRedirected = 2,
  kUpgraded = 3,
};

const char* BlockActionName(BlockAction a);

struct BlockEvent {
  long long ts_millis = 0;      // caller-supplied (determinism law)
  std::string identity;         // IdentityId.value
  long long tab_id = 0;
  OriginKey origin;             // OriginKey (frozen shape)
  std::string target;           // REDACTED at creation: scheme://host/path
  std::string rule;             // filter text (list provenance)
  std::string list_provenance;  // "list_id" (bundle binding rides in ledger)
  BlockAction action = BlockAction::kBlocked;
  RequestClass request_class = RequestClass::kSubresource;
};

// The redaction function itself — exported so the vectors pin it directly.
std::string RedactTarget(const UrlParts& parts);

// Build an event from a decision context (target redaction applied).
BlockEvent MakeEvent(const RequestContext& ctx, long long tab_id,
                     long long ts_millis, const std::string& rule,
                     const std::string& list_id, BlockAction action);

struct EventRing {
  static constexpr size_t kCapacity = 256;  // FIFO cap (living; documented)
  std::vector<BlockEvent> events;           // chronological
};

void RingAppend(EventRing* ring, BlockEvent event);

constexpr size_t kChunkBudgetBytes = 65536;  // 64KB view budget

// RecentEvents view: identity-filtered ("" = all identities — the host
// method always passes one), newest first, capped by max_events AND the
// canonical-byte budget.
std::vector<common::JsonValue> RingView(const EventRing& ring,
                                        const std::string& identity,
                                        int max_events);

common::JsonValue EventToJson(const BlockEvent& e);
common::JsonValue RingToJson(const EventRing& ring);
bool ParseRing(const common::JsonValue& v, EventRing* out,
               std::string* detail);

// ---------------------------------------------------------------------------
// P11-T5: living `block-event-v1` ledger row
// (docs/contracts/block-event-v1.md, xr-browser registry-post-freeze.md)
// ---------------------------------------------------------------------------
// A row is a living SUPERSET document around the FROZEN BlockEvent
// vocabulary — the mojom surface is never widened (brief §architecture
// invariant 7: a block is an event, not a policy change). Provenance
// (rule id / filter text / list / bundle version / site-class via origin)
// and the caller's `why_code` (the closed verdict vocabulary in
// host_protocol.md) ride on the row; redaction happens at row CREATION,
// the same law as MakeEvent: `target` is always scheme://host/path —
// query strings and fragments never reach the ledger.
//
// Determinism law (precedent: the ledger rows in commands/core/
// dispatch.cc): `seq` and `ts_millis` are caller-supplied — no clock
// reads here. `seq` is the caller's monotonic per-identity counter.
struct LedgerRowParams {
  long long seq = 0;             // caller's monotonic counter
  long long ts_millis = 0;       // caller-supplied
  long long tab_id = 0;
  long long bundle_version = 0;  // 0 = no bundle context (no-bundle rows)
  std::string rule_id;           // engine rule id ("" = not rule-derived)
  std::string rule;              // filter text — "why blocked" provenance
  std::string list_id;           // "" = not list-derived
  std::string why_code;          // closed verdict vocabulary (host_protocol)
  BlockAction action = BlockAction::kBlocked;
};

common::JsonValue MakeLedgerRow(const RequestContext& ctx,
                                const LedgerRowParams& p);

}  // namespace xr::shield
