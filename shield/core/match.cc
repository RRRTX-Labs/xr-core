// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/match — see match.h. Pure decision pipeline; no
// I/O, no clock, no globals (house core law).
#include "shield/core/match.h"

#include <string_view>

namespace xr::shield {

using common::JsonValue;

const char* ActionName(Action a) {
  switch (a) {
    case Action::kBlock: return "block";
    case Action::kAllow: return "allow";
    case Action::kRedirect: return "redirect";
    case Action::kReplace: return "replace";
  }
  return "block";  // unreachable; keeps -Werror switches honest
}

namespace {

// posture reason -> match why_code (both closed vocabularies; the mapping
// itself is law and pinned by the vectors)
const char* WhyFromPosture(const Posture& p) {
  std::string_view r(p.reason);
  if (r == "route-loss") return "route-loss-fail-closed";
  if (r == "engine-dead") return "engine-dead-fail-open";
  if (r == "engine-poisoned") return "engine-poisoned-fail-open";
  if (r == "kill-switch") return "kill-switch";
  return "no-match";  // unreachable from a fail-open/fail-closed posture
}

}  // namespace

MatchOutcome DecideMatch(const RequestContext& ctx,
                         const NormalizedBundle* bundle,
                         BlockingEngine* engine, const ScopeSet& scopes,
                         long long now_mono, const PostureInputs& posture_in) {
  MatchOutcome out;
  Posture pd = DecidePosture(posture_in);
  out.posture = pd;

  // 1. fail-closed beats everything: the request does not proceed
  if (pd.mode == PostureMode::kFailClosed) {
    out.fail_closed = true;
    out.verdict.action = Action::kBlock;
    out.verdict.why_code = "route-loss-fail-closed";
    if (bundle != nullptr) out.verdict.bundle_version = bundle->bundle_version;
    return out;
  }
  // 2. fail-open: allowed, posture reason IS the why_code
  if (pd.mode == PostureMode::kFailOpen) {
    out.verdict.action = Action::kAllow;
    out.verdict.why_code = WhyFromPosture(pd);
    if (bundle != nullptr) out.verdict.bundle_version = bundle->bundle_version;
    return out;
  }
  // 3. normal posture: the bundle+engine decide
  if (bundle == nullptr) {
    out.verdict.action = Action::kAllow;
    out.verdict.why_code = "no-bundle";
    return out;
  }
  out.verdict.bundle_version = bundle->bundle_version;
  if (engine == nullptr || !engine->alive()) {
    // defense in depth: posture said the engine was alive; if it died
    // mid-decision the fail-open law still applies
    out.verdict.action = Action::kAllow;
    out.verdict.why_code = "engine-dead-fail-open";
    return out;
  }
  EngineHit hit;
  if (!engine->Match(ctx, &hit)) {
    out.verdict.action = Action::kAllow;
    out.verdict.why_code = "no-match";
    return out;
  }
  out.verdict.engine_decision = true;
  out.verdict.rule_id = hit.rule_id;
  out.verdict.list_id = hit.list_id;
  switch (hit.action) {
    case Action::kAllow:
      out.verdict.action = Action::kAllow;
      out.verdict.why_code = "rule-allowed";
      return out;
    case Action::kRedirect:
    case Action::kReplace: {
      // redirect/replace hits are still suppressible by an exception scope
      for (const auto& s : scopes.scopes) {
        if (Covers(s, ctx, hit, now_mono)) {
          out.verdict.action = Action::kAllow;
          out.verdict.why_code = "exception-scope";
          return out;
        }
      }
      out.verdict.action = hit.action;
      out.verdict.why_code =
          hit.action == Action::kRedirect ? "rule-redirected" : "rule-replaced";
      return out;
    }
    case Action::kBlock: {
      for (const auto& s : scopes.scopes) {
        if (Covers(s, ctx, hit, now_mono)) {
          out.verdict.action = Action::kAllow;
          out.verdict.why_code = "exception-scope";
          return out;
        }
      }
      out.verdict.action = Action::kBlock;
      out.verdict.why_code = "rule-blocked";
      return out;
    }
  }
  out.verdict.action = Action::kAllow;  // unreachable; -Werror switch guard
  out.verdict.why_code = "no-match";
  return out;
}

JsonValue VerdictToJson(const Verdict& v) {
  JsonValue::Object o{
      {"action", JsonValue(ActionName(v.action))},
      {"bundle_version", JsonValue(static_cast<int>(v.bundle_version))},
      {"engine_decision", JsonValue(v.engine_decision)},
      {"list_id", JsonValue(v.list_id)},
      {"rule_id", JsonValue(v.rule_id)},
      {"why_code", JsonValue(v.why_code)},
  };
  return JsonValue(o);
}

}  // namespace xr::shield
