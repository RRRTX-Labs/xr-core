// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — permission audit rows (P15 T4, post-freeze contract
// `permission-audit-event-v1`, docs/contracts/permission-audit-event-v1.md).
// Two outputs from one row: the structured permission-audit-event-v1 JSON,
// and the FROZEN ActivityLog row (xr-core/mojom/activity_log.mojom, kind =
// kPermission) so the P13 Activity tab works without a schema change.
// An audit row is a leak surface: the origin is reduced to a bare registrable
// domain and anything else is REFUSED, never truncated.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "permissions/core/types.h"

namespace xr::permissions {

// grant = the identity's effective state for the capability WIDENED to allow
// (or a temporary grant was issued); revoke = narrowed or cleared; expiry =
// a temporary grant ended by time, use, or session end.
enum class AuditEvent { kGrant = 0, kRevoke = 1, kExpiry = 2 };

// What decides for the identity AFTER the row's change: the overlay (grant,
// revoke) or the global fallback (expiry: the tier table decides again).
enum class DecidingLayer { kIdentityOverlay = 0, kGlobalFallback = 1 };

struct AuditRow {
  AuditEvent event = AuditEvent::kGrant;
  std::string identity;
  Capability capability = Capability::kGeolocation;
  std::string origin;       // registrable domain, or "" for identity-wide rows
  std::string scope;        // once | session | 7d | identity_default | identity_deny_list
  int64_t ts_millis = 0;    // caller-supplied; the core reads no clock
  int64_t ttl_millis = 0;   // 7d => kSevenDaysMillis; otherwise 0 (no time bound)
  std::string reason;       // user-grant | user-default | ttl | used | session_end |
                            // revoke_site | revoke_all | fortress-deny-list
  DecidingLayer deciding_layer = DecidingLayer::kIdentityOverlay;
};

const char* AuditEventName(AuditEvent e);          // "grant" | "revoke" | "expiry"
const char* DecidingLayerName(DecidingLayer d);    // "identity_overlay" | "global_fallback"

// Registrable-domain-only redaction. Accepts [a-z0-9.-], 1..253 bytes, no
// leading/trailing or doubled dots, no leading/trailing hyphen per label.
// Anything with a scheme, path, query, fragment, port, userinfo or uppercase
// is refused (returns false, *out untouched).
bool RedactOrigin(std::string_view input, std::string* out);

// permission-audit-event-v1: canonical JSON (sorted keys, no whitespace).
std::string AuditRowJson(const AuditRow& r);

// Frozen ActivityRow {ts_millis, kind, identity, summary}, canonical JSON.
std::string ActivityRowJson(const AuditRow& r);

// Human-readable ActivityRow.summary. English default; localization goes
// through tools/l10n_extract.py when the UI half lands (deferred, see report).
std::string ActivitySummary(const AuditRow& r);

}  // namespace xr::permissions
