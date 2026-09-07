// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — THE policy resolver (Plan L3: "one policy resolver; unknown/
// absent policy => deny, never guess"). Pure and total by construction:
// reads ONLY its inputs (no clock, no RNG, no environment, no I/O — the
// caller supplies `now_ms` / `session_id` as plain input fields so expiry
// evaluation stays inside the pure function). The frozen core semantics are
// byte-parity with xr-core/fakes/policy_resolver.py (the behavioral
// reference frozen in P5; golden vectors are the arbiter). P6 layers —
// per-site trust bindings, expiring exceptions, enterprise overrides,
// extension hints — are INERT when their inputs are empty, which is what
// preserves vector parity. Invalid layer entries are DROPPED, never widened
// (fail-closed per entry; the service layer logs the drop).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "policy/core/effective_policy.h"
#include "policy/core/json.h"

namespace xr::policy {

inline constexpr int kContractVersion = 1;

// Trust ladder ordering: kStandard=0 < kShield=1 < kFortress=2; -1 unknown.
// (Contract semantics: "higher == strictly more restrictive".)
int TrustTierIndex(const std::string& t);

// Identity attributes needed by resolution (from identity-list-v1; the
// façade default equals the frozen fake's fixture set).
struct IdentityInfo {
  std::string value;
  bool ephemeral = false;  // in-memory identity (ADR-0042 (d))
  bool fortress = false;   // fortress-class identity
};

// Per-site trust binding (Settings-managed; permanent). `identity` empty =
// site default; a non-empty identity is the per-identity ⌥ override.
struct TrustBinding {
  std::string domain;
  std::string identity;
  std::string trust;  // kStandard | kShield | kFortress
  int64_t created_at = 0;
};

// Expiring trust override ("Shields down" etc.). Scopes follow Plan §1.5:
// in-flow prompts offer once/session/7d ONLY; `permanent` entries are
// honored ONLY when granted_by == "settings" (permanent exceptions live
// solely in Settings — never "forever" in-flow).
struct ExceptionEntry {
  std::string id;
  std::string domain;
  std::string identity;  // empty = all identities
  std::string scope;     // once | session | 7d | permanent
  std::string trust;
  int64_t created_at = 0;
  int64_t expires_at = 0;    // 7d scope: active iff expires_at > now_ms
  std::string session_id;    // session scope: active iff == request session
  int64_t remaining_uses = 0;  // once scope: active iff > 0 (caller decrements)
  std::string granted_by;    // in_flow | settings (exceptions-v2 field)
};

// Enterprise managed layer (T7): a *source with higher priority feeding the
// same pure resolver* — never a second decision-maker (L3). Unsigned or
// invalid managed docs are ignored upstream (signed-or-ignored, L6).
struct EnterprisePolicy {
  bool present = false;
  std::string trust_floor;  // "" = none; clamps the tier UPWARD only
  std::string force_fingerprint;  // "" = unset; else a legal FingerprintMode
  std::string force_route;        // "" = unset; else a legal RouteClass
  bool force_letterbox = false;
  bool force_block_third_party = false;
};

// Extension capability hint (input field only; Guard dispatch is P21).
struct ExtensionHint {
  bool present = false;
  std::string id;
  std::vector<std::string> capabilities;
};

struct ResolveRequest {
  // Frozen core (mojom PolicyResolver.Resolve args, parsed):
  bool has_request = false;      // input was not even a JSON object
  bool version_mismatch = false; // contract_version present and != 1
  bool core_valid = false;       // identity/origin/class/trust all valid
  std::string identity;          // valid identity value when core_valid
  std::string registrable_domain;
  std::string request_class;
  bool has_trust = false;
  std::string trust;

  // P6 layers:
  std::vector<IdentityInfo> identities;
  std::vector<TrustBinding> bindings;
  std::vector<ExceptionEntry> exceptions;
  EnterprisePolicy enterprise;
  ExtensionHint extension;
  int64_t now_ms = 0;
  std::string session_id;
};

struct ResolveOutput {
  bool ok = true;
  std::string error;  // ErrorCode name; only kVersionMismatch in v1
  EffectivePolicy policy;  // deny-safe default; the deny policy when core invalid
};

// The pure, total resolution. Deterministic; identical inputs => identical
// bytes, always.
ResolveOutput Resolve(const ResolveRequest& req);

// Parses a JSON request (the stdio protocol payload) into a ResolveRequest.
// Implements the frozen fake's validation order exactly (version gate
// first, then identity/origin/class/trust). Unknown top-level fields are
// ignored (fake parity); invalid layer entries are dropped and reported in
// `dropped` for the caller's ledger rows. Malformed core => core_valid=false
// (deny downstream). Never throws.
struct RequestParse {
  ResolveRequest request;
  std::vector<std::string> dropped;  // ledger reasons for dropped entries
};
RequestParse ParseResolveRequest(const JsonValue& v);

// Serializes a ResolveOutput to the protocol envelope ({"ok":...} /
// {"error":"..."}), canonical JSON.
JsonValue ResolveOutputToJson(const ResolveOutput& out);
std::string ResolveOutputToCanonicalJson(const ResolveOutput& out);

// Strict enterprise-field validation shared by the request layer and the
// managed (enterprise) source — ONE implementation, no drift. `v` is the
// enterprise object itself ({"trust_floor": ..., ...}).
bool ParseEnterpriseFields(const JsonValue& v, EnterprisePolicy* out);

}  // namespace xr::policy
