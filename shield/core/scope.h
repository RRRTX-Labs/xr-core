// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/scope — resolver-coupled exception scopes (P11-T2;
// toggle mechanics land on this in T4). A scope says: this rule/list does
// NOT apply for this identity / site / workspace, until this monotonic
// expiry, for this recorded reason. The coupling law the vectors pin:
// rule X applied to identity A ≠ identity B — a scope bound to an identity
// never leaks to another one; same for site and workspace. All three
// dimensions are exact-match-when-set, any-when-empty; there is no
// inheritance, no wildcard, no guessing (the resolver's own domain
// hierarchy stays the resolver's business — a scope names a registrable
// domain and matches that domain exactly).
//
// Expiry is deterministic: caller-supplied monotonic integers only (no
// wall clock — the P9/P10 comparator law). expiry_mono < 0 = never
// expires; expiry_mono >= 0 expires AT the boundary (now >= expiry).
#pragma once

#include <string>
#include <vector>

#include "common/core/json.h"
#include "shield/core/context.h"
#include "shield/core/engine.h"

namespace xr::shield {

struct ExceptionScope {
  std::string scope_id;             // unique handle (remove/ledger)
  std::string identity;             // "" = any identity
  std::string site;                 // registrable domain, exact; "" = any
  std::string workspace;            // "" = any workspace
  std::string rule_id;              // "" = every rule
  std::string list_id;              // "" = every list
  long long expiry_mono = -1;       // caller monotonic ms; <0 = never
  std::string reason;               // REQUIRED non-empty (T4 ledger law)
};

struct ScopeSet {
  std::vector<ExceptionScope> scopes;
};

enum class ScopeResult {
  kOk = 0,
  kMalformed,
  kUnknownField,
  kMissingReason,     // a scope without a reason is refused, not stored
  kDuplicateScopeId,
};

// Strict parse of {"scopes":[...]} (the host's exception-* arg shape).
ScopeResult ParseScopeSet(const common::JsonValue& args, ScopeSet* out,
                          std::string* detail);

// True when `scope` covers this request+hit: every SET dimension must
// match exactly; the expiry is judged against `now_mono` (expired scopes
// cover nothing — they wait for the sweep).
bool Covers(const ExceptionScope& scope, const RequestContext& ctx,
            const EngineHit& hit, long long now_mono);

// Deterministic expiry sweep (T4's --as-of job at the core level).
struct SweepResult {
  std::vector<std::string> active_ids;   // kept, in input order
  std::vector<std::string> expired_ids;  // removed, in input order
};
SweepResult SweepAsOf(const ScopeSet& scopes, long long now_mono);

common::JsonValue ScopeToJson(const ExceptionScope& s);
common::JsonValue ScopeSetToJson(const ScopeSet& set);

}  // namespace xr::shield
