// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/fake_engine — the deterministic stand-in for the
// vendored adblock-rust engine (P11-T2). It implements the v1 filter
// grammar of bundle.h EXACTLY, and the Python fake (fakes/shield.py)
// mirrors this algorithm byte-for-byte — the golden vectors are generated
// against one and replayed against both, which is what makes the eventual
// Rust swap (T8 parity) measurable instead of hopeful.
//
// v1 match semantics (closed, documented, deterministic):
//   * match surface: scheme://host/path, lowercased, port stripped,
//     query+fragment stripped (the same redaction events use);
//   * "||d[rest]": host == d or host ends with ".d" (dot boundary); rest
//     then matches left-anchored against path; "||d|" requires path "/";
//   * "|p": p left-anchored at position 0 of the match surface;
//   * "p|": the last segment must END the match surface; a trailing "*"
//     voids the right anchor;
//   * "*": ordered segment containment, earliest-start per segment (the
//     earliest start maximizes the remainder, so leftmost-greedy is
//     complete for ordered containment — no backtracking needed);
//   * "^" (kSep): one char of "/:" or end-of-string (zero-width, only at
//     the end of a segment);
//   * cosmetic rules never match in the network engine;
//   * rule options: domains/exclude_domains are EXACT set membership
//     against the request's registrable_domain or host;
//   * precedence: list order, then rule order; an allow hit returns
//     immediately (ABP exception semantics — allow overrides block);
//     otherwise the FIRST non-allow hit wins.
//
// Engine death is total and observable: Kill()/Poison() flip alive();
// a dead engine's Match is never called by the decision core, and returns
// false (no opinion) if called anyway — defense in depth, not a contract.
#pragma once

#include <string>

#include "shield/core/bundle.h"
#include "shield/core/context.h"
#include "shield/core/engine.h"

namespace xr::shield {

// The segment matcher, exported so tests (and the Python mirror's
// equivalence vectors) can pin it directly. `segs` are parsed-filter
// segments; `target` is the string to match against; anchoring as
// documented above.
bool MatchSegmentsV1(const std::vector<std::string>& segs,
                     const std::string& target, bool left_anchor,
                     bool right_anchor);

// Full parsed-filter match against a split URL.
bool FilterMatchV1(const ParsedFilter& f, const UrlParts& parts);

class TableEngine : public BlockingEngine {
 public:
  TableEngine(const NormalizedBundle* bundle, bool alive = true,
              bool poisoned = false)
      : bundle_(bundle), alive_(alive), poisoned_(poisoned) {}

  bool alive() const override { return alive_ && !poisoned_; }
  bool Match(const RequestContext& ctx, EngineHit* hit) override;

  void Kill() { alive_ = false; }     // simulated engine death
  void Poison() { poisoned_ = true; } // simulated poisoned-mutex state

 private:
  const NormalizedBundle* bundle_;  // not owned; outlives the engine
  bool alive_;
  bool poisoned_;
};

}  // namespace xr::shield
