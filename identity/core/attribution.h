// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — per-identity RSS/memory ATTRIBUTION model (P14-T10: "what is
// using my RAM" honesty). Feeds P34 perf and the manager page's per-identity
// stats. The model takes measured samples (per-process RSS + which
// identities a process serves) and attributes memory to identities under
// the plan's honesty law:
//   * Σ(attributed) ≤ total — attributed memory can never exceed the
//     measured total (a double-count is the classic attribution lie; the
//     planted double-count reddens in test_attribution.cc);
//   * SHARED processes are split EVENLY across the identities served
//     (documented approximation — the alternative, attributing all shared
//     memory to every identity, is exactly the double-count the law bans);
//   * UNATTRIBUTED is reported as its own row ("browser + shared + we
//     don't know"), never silently dropped — the sum the user sees closes
//     against the task manager's total.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace xr::identity {

struct ProcessSample {
  uint64_t pid = 0;
  size_t rss_kb = 0;
  std::vector<std::string> serves;  // opaque domains this process serves
};

struct AttributionRow {
  std::string domain;    // opaque domain, or "(unattributed)"
  size_t kb = 0;
  size_t processes = 0;  // processes that contributed to this row
};

class AttributionModel {
 public:
  // Attribute `total_kb` (the task manager's measured total) across
  // `samples`. Fail-closed: an impossible input (total < sum of exclusive
  // RSS, i.e. the samples contradict the total) returns false and leaves
  // `rows` empty — the manager page then shows "attribution unavailable",
  // never a plausible-looking wrong table.
  bool Attribute(std::vector<ProcessSample> samples, size_t total_kb,
                 std::vector<AttributionRow>* rows) const;

  // The audit invariant the manager page asserts before rendering: the
  // rows' kb sum must be <= total (and, with the unattributed row,
  // exactly total). Exposed for the test's planted double-count probe.
  static bool SumWithinTotal(const std::vector<AttributionRow>& rows,
                             size_t total_kb);
};

}  // namespace xr::identity
