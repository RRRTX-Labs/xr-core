// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — policy-change event generator (Plan P6-T5): the dial strip's
// data contract. Emits CONCRETE deltas ("camera, microphone now blocked ·
// stricter isolation · route now proxy") with an `undo` affordance field.
// Schema-enforced absences (absence-lint tested): NO score/risk/grade
// fields (vocab law), NO auto_reload field (auto-reload is not a contract
// behavior — absence IS the contract). The generator is pure: old policy +
// new policy + context in, canonical JSON out.
#pragma once

#include <string>
#include <vector>

#include "policy/core/effective_policy.h"
#include "policy/core/json.h"

namespace xr::policy {

struct PolicyDelta {
  std::string path;     // e.g. "permissions.camera"
  std::string from;     // e.g. "kAsk"
  std::string to;       // e.g. "kDeny"
  std::string change;   // classification: blocked|unblocked|ask|on|off|...
  std::string human;    // concrete copy: "camera now blocked"
};

// Flattens a policy into (path, value) pairs, in deterministic order.
std::vector<std::pair<std::string, std::string>> FlattenPolicy(const EffectivePolicy& p);

// Computes the concrete delta list between two policies (deterministic
// order; empty when identical).
std::vector<PolicyDelta> DiffPolicies(const EffectivePolicy& old_p, const EffectivePolicy& new_p);

// Builds the summary line: "camera, microphone now blocked · stricter
// isolation". Empty string when there are no deltas.
std::string BuildSummary(const std::vector<PolicyDelta>& deltas);

// The full event payload per policy-change-event-v1 (schema lives in
// xr-browser/docs/contracts/). `undo_allowed` is the caller's policy
// decision (in-flow changes: true; enterprise-enforced: false).
JsonValue BuildPolicyChangeEvent(const std::string& identity, const std::string& site,
                                 const std::string& previous_trust, const std::string& new_trust,
                                 const EffectivePolicy& old_p, const EffectivePolicy& new_p,
                                 bool undo_allowed);

}  // namespace xr::policy
