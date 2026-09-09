// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — client-side fuzzy search over settings entries (Plan P8-T1:
// "search-first settings"). Ranking is CORE C++ (never TS): the views call
// the host and render; byte-parity with fakes/settings.py keeps the verdict
// identical wherever it is computed (P13/P21 reuse the same core).
//
// Semantics (v2, tuned against the committed recall corpus — see the P8
// research log "recall tuning" entry): the query is split into words; each
// word scores per field as fzf-class token match — exact (3), prefix (2,
// word length >= 3), contiguous subsequence (1, word length >= 3); the
// per-entry score is the sum of per-word best^2 (exact words dominate,
// fractional words do not outvote them). A query that IS a contiguous
// subsequence of a field dominates the entry (+150; +300 when equal). A
// word shorter than 2 chars is ignored (never decides). Scoring is
// deterministic integer math; ties break by schema order (stable).
//
// This is the P7 matcher CLASS adapted per-field (recorded decision in the
// research log); it reuses the P7 subsequence rule as its sub-word signal
// (share-vs-copy decision: the ~80-line scorer is COPY-ADAPTED into the
// core because the settings core is std-only and cannot #include the P7
// matcher header without pulling Chromium-free commands-core deps across
// the settings boundary; the decision + diff is recorded in research-log).
//
// "container" is a search-only ALIAS for the identity vocabulary here and
// nowhere else (DR-06: search-only, never a label).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "settings/core/settings_schema.h"

namespace xr::settings {

enum class EntryKind { kSection, kSetting };

struct IndexEntry {
  std::string key;
  EntryKind kind = EntryKind::kSetting;
  size_t order = 0;  // schema order (stable tie-break)
  std::vector<std::string> fields;  // aliases + key (+ section id)
  // Precomputed per-field lowercase token/joined forms (built once by
  // BuildIndex so per-query scoring does zero re-lowercasing/allocation).
  struct ParsedField {
    std::vector<std::string> tokens;  // split on ' ' (lowercased)
    std::string joined;               // spaces removed (lowercased)
  };
  std::vector<ParsedField> parsed;
};

struct RankedResult {
  std::string key;
  EntryKind kind = EntryKind::kSetting;
  long long score = 0;
  size_t order = 0;
};

// Build the search index from a loaded schema (sections + settings in
// schema order). Fields = section id (sections) / key + section + aliases
// (settings). Aliases are the ONLY copy-adjacent words in the index;
// labels themselves are message ids (grdp), not indexed text.
std::vector<IndexEntry> BuildIndex(const SettingsSchema& schema);

// Rank `query` against the corpus; non-matches are omitted. Deterministic
// (integer math, stable order); the score model is documented above.
std::vector<RankedResult> MatchQuery(const std::string& query,
                                     const std::vector<IndexEntry>& corpus);

// One word vs one token: 3 exact, 2 prefix (len(word)>=3), 1 subsequence
// (len(word)>=3), else 0. Both inputs lowercased.
long long WordTokenScore(const std::string& word,
                         const std::string& token);

// Word vs one FIELD (tokenized on ' ' plus the joined form). Lowercased.
long long WordFieldBest(const std::string& word,
                        const std::string& field);

// ASCII lowercase helper (the index and queries are ASCII + a small fixed
// RTL alias set — byte-preserved, never lowercased away).
std::string LowerAscii(const std::string& s);

}  // namespace xr::settings
