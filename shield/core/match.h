// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/match — the decision pipeline (P11-T2). Order is
// LAW, and the golden vectors pin it:
//   1. posture: fail-closed (route loss) short-circuits to a BLOCKED
//      verdict — fail-closed means the request does not proceed;
//      fail-open (engine dead/poisoned, kill switch) short-circuits to an
//      ALLOWED verdict with its posture reason as the why_code;
//   2. no bundle loaded → allowed, why_code "no-bundle";
//   3. the engine decides; allow-overrides-block among engine hits lives
//      inside the engine implementation (ABP exception semantics);
//   4. an exception scope covering a blocking hit downgrades it to
//      allowed, why_code "exception-scope" — the scope dimensions are
//      checked against THIS request's identity/site/workspace (the
//      resolver-coupling law).
//
// why_code is CLOSED vocabulary (living doc shield/host_protocol.md):
//   route-loss-fail-closed | engine-dead-fail-open |
//   engine-poisoned-fail-open | kill-switch | no-bundle | no-match |
//   rule-blocked | rule-allowed | rule-redirected | rule-replaced |
//   exception-scope
#pragma once

#include <string>

#include "common/core/json.h"
#include "shield/core/bundle.h"
#include "shield/core/context.h"
#include "shield/core/engine.h"
#include "shield/core/posture.h"
#include "shield/core/scope.h"

namespace xr::shield {

struct Verdict {
  Action action = Action::kAllow;
  std::string why_code;       // closed vocabulary above
  std::string rule_id;        // "" when no rule produced the decision
  std::string list_id;        // "" ditto
  long long bundle_version = 0;
  bool engine_decision = false;  // true only when the engine produced a hit
};

struct MatchOutcome {
  bool fail_closed = false;  // route-loss: the caller MUST block the request
  Posture posture;  // defaults to normal/green
  Verdict verdict;
};

MatchOutcome DecideMatch(const RequestContext& ctx,
                         const NormalizedBundle* bundle,
                         BlockingEngine* engine, const ScopeSet& scopes,
                         long long now_mono, const PostureInputs& posture_in);

common::JsonValue VerdictToJson(const Verdict& v);

}  // namespace xr::shield
