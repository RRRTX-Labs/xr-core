// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — availability predicates (Plan §1.10 "availability predicate";
// P7 "Contracts out: availability-predicate contract (reads resolver)").
//
// Law (the pinned-snapshot / TOCTOU rule inherited from P6): predicates are
// PURE reads of the P6 snapshot PINNED at a version. The UI never calls
// Resolve() live. A predicate is a REGISTERED DATA-ID — never arbitrary code
// in a descriptor. Evaluating takes the snapshot BY VALUE (a pinned copy):
// invalidating the live cache mid-flight cannot change a pinned verdict
// (covered by test: pin, evaluate, mutate source, re-evaluate pinned => stable).
//
// The predicate set is a fixed, registered list (below). An unknown data-id
// is DENY-default (unavailable, "unknown predicate") — never a guess (L3).
// The snapshot shape is availability-predicate-contract v1 (registered
// post-freeze); the resolver/enterprise layers feed it upstream (P11/P16).
#pragma once

#include <string>
#include <vector>

#include "commands/core/json.h"

namespace xr::commands {

struct AvailabilityVerdict {
  bool available = false;
  std::string reason;  // human-readable, "disabled with reason" (never absent)
};

class Availability {
 public:
  // The registered predicate data-ids (single source; tests cross-check the
  // doc). New predicates are a contract change, not a code-only addition.
  static const std::vector<std::string>& Registered();

  bool KnownPredicate(const std::string& predicate_id) const;

  // Evaluate a predicate data-id against a PINNED snapshot (by value). Pure.
  AvailabilityVerdict Evaluate(const std::string& predicate_id,
                               const JsonValue& snapshot) const;
};

}  // namespace xr::commands
