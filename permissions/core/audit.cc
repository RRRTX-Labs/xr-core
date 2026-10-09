// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — audit row generator (see audit.h). The generator is the golden
// source: tests/golden-permission-audit-event.json is compared byte-for-byte
// against AuditRowJson output (permissions/tests/test_audit.cc).
#include "permissions/core/audit.h"

#include <string>

#include "permissions/core/json.h"

namespace xr::permissions {
namespace {

constexpr const char* kDeterminismNote =
    "ts_millis is caller-supplied; the core reads no clock; identical inputs give identical bytes";

bool LabelOk(std::string_view label) {
  if (label.empty() || label.size() > 63) return false;
  if (label.front() == '-' || label.back() == '-') return false;
  for (char c : label) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

const char* AuditEventName(AuditEvent e) {
  switch (e) {
    case AuditEvent::kGrant: return "grant";
    case AuditEvent::kRevoke: return "revoke";
    case AuditEvent::kExpiry: return "expiry";
  }
  return "revoke";  // unreachable; the narrower event is the safe label
}

const char* DecidingLayerName(DecidingLayer d) {
  return d == DecidingLayer::kIdentityOverlay ? "identity_overlay" : "global_fallback";
}

bool RedactOrigin(std::string_view input, std::string* out) {
  if (input.empty() || input.size() > 253) return false;
  // Split on '.', validate each label; this refuses "https://", "/", "?", "#",
  // ":", "@", spaces and uppercase, because none of them is a label character.
  size_t start = 0;
  while (true) {
    size_t dot = input.find('.', start);
    std::string_view label = input.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
    if (!LabelOk(label)) return false;
    if (dot == std::string_view::npos) break;
    start = dot + 1;
  }
  *out = std::string(input);
  return true;
}

std::string AuditRowJson(const AuditRow& r) {
  JsonValue::Object o;
  o.emplace("contract", JsonValue("permission-audit-event"));
  o.emplace("contract_version", JsonValue(static_cast<int64_t>(1)));
  o.emplace("event", JsonValue(AuditEventName(r.event)));
  o.emplace("identity", JsonValue(r.identity));
  o.emplace("capability", JsonValue(CapabilityName(r.capability)));
  o.emplace("origin", JsonValue(r.origin));
  o.emplace("scope", JsonValue(r.scope));
  o.emplace("ts_millis", JsonValue(static_cast<int64_t>(r.ts_millis)));
  o.emplace("ttl_millis", JsonValue(static_cast<int64_t>(r.ttl_millis)));
  o.emplace("reason", JsonValue(r.reason));
  o.emplace("deciding_layer", JsonValue(DecidingLayerName(r.deciding_layer)));
  o.emplace("determinism_note", JsonValue(kDeterminismNote));
  return JsonValue(std::move(o)).Canonical();
}

std::string ActivitySummary(const AuditRow& r) {
  const std::string cap = CapabilityName(r.capability);
  const std::string where = r.origin.empty() ? std::string("all sites") : r.origin;
  switch (r.event) {
    case AuditEvent::kGrant:
      return "Allowed " + cap + " for " + where + " (" + r.scope + ")";
    case AuditEvent::kRevoke:
      return "Revoked " + cap + " for " + where + " (" + r.reason + ")";
    case AuditEvent::kExpiry:
      return "Temporary " + cap + " access for " + where + " ended (" + r.reason + ")";
  }
  return "Permission changed";
}

std::string ActivityRowJson(const AuditRow& r) {
  JsonValue::Object o;
  o.emplace("identity", JsonValue(r.identity));
  o.emplace("kind", JsonValue("kPermission"));  // frozen ActivityKind::kPermission
  o.emplace("summary", JsonValue(ActivitySummary(r)));
  o.emplace("ts_millis", JsonValue(static_cast<int64_t>(r.ts_millis)));
  return JsonValue(std::move(o)).Canonical();
}

}  // namespace xr::permissions
