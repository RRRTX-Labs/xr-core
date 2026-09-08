// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — palette ranking core (Plan P7-T2 "CI bench with synthetic
// 2000-command corpus"). Subsequence-fuzzy scoring (fzf-class) with a STABLE,
// deterministic tie-break so the corpus bench and the golden-ranking tests are
// reproducible run-to-run (×100 identical runs must produce identical order).
//
// Scoring model (integer, no floats, no RNG, no clock):
//   * a command matches iff the lowercased query is a subsequence of its
//     lowercased title, or (weaker) of "id keywords...";
//   * per matched char: +1 base, +kConsecutive if the previous query char
//     matched the immediately-preceding target char, +kBoundary at the start
//     of the target or after a separator/camelCase boundary;
//   * a length penalty ((target-len - query-len) / 2) rewards tight matches;
//   * a match-kind tier (title-substring > title-subsequence > keyword/id) is
//     folded in as the high bits so kind always dominates within-kind score.
//   * rank: score desc, then registration order asc (the stable tie-break).
//
// Complexity target (stated in docs, not hidden): 2000 commands x 32-char
// query in <=5 ms p99 (core sub-budget supporting the plan's 50 ms interactive
// warm budget — both numbers are recorded in the bench JSON + evidence).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "commands/core/registry.h"

namespace xr::commands {

struct RankedMatch {
  std::string id;
  long long score = 0;
  size_t order = 0;   // registration order (stable tie-break)
  std::string title;
  int kind = 0;       // 2=title-substring, 1=title-subseq, 0=keyword/id
};

// Rank `query` against the corpus (in the order given). Non-matches are
// omitted. Deterministic: same inputs => same output, every run.
std::vector<RankedMatch> MatchQuery(const std::string& query,
                                    const std::vector<const Command*>& corpus);

// Lowercase helper (ASCII; the corpus bench and goldens are ASCII-locked).
std::string LowerAscii(std::string s);

}  // namespace xr::commands
