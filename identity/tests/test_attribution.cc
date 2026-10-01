// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// attribution suite (P14-T10): the sum-identity law (Σ attributed ≤
// measured total; the planted double-count reddens), the even-split
// approximation for shared processes, the unattributed row (closes the
// table against the task manager's total), and the fail-closed refusal
// when samples contradict the total. NO real MB numbers: this suite uses
// synthetic kb inputs only; the real-rig halves are NOT-RUN by law.
#include <string>
#include <vector>

#include "harness.h"
#include "core/attribution.h"

using xr::identity::AttributionModel;
using xr::identity::AttributionRow;
using xr::identity::ProcessSample;

namespace {
const char* kA = "xr:00000000-0000-4000-8000-000000000001";
const char* kB = "xr:00000000-0000-4000-8000-000000000002";

size_t Sum(const std::vector<AttributionRow>& rows) {
  size_t s = 0;
  for (const auto& r : rows) s += r.kb;
  return s;
}
const AttributionRow* Find(const std::vector<AttributionRow>& rows,
                           const std::string& domain) {
  for (const auto& r : rows) {
    if (r.domain == domain) return &r;
  }
  return nullptr;
}
}  // namespace

int main() {
  AttributionModel model;

  // 1. Exclusive processes: every kb to the one identity; unattributed
  // closes the sum EXACTLY against the total.
  {
    std::vector<ProcessSample> samples = {
        {101, 1000, {kA}},
        {102, 500, {kB}},
        {103, 300, {}},  // browser chrome: no identity
    };
    std::vector<AttributionRow> rows;
    XR_EXPECT_MSG(model.Attribute(samples, 2000, &rows),
                  "attribution succeeds");
    XR_EXPECT_EQ(Find(rows, kA)->kb, size_t(1000));
    XR_EXPECT_EQ(Find(rows, kB)->kb, size_t(500));
    XR_EXPECT_EQ(Find(rows, "(unattributed)")->kb, size_t(500));  // 300+200
    XR_EXPECT_EQ(Sum(rows), size_t(2000));  // closes exactly
    // Biggest first (the "what is using my RAM" ordering).
    XR_EXPECT_MSG(rows[0].domain == kA, "sorted biggest-first");
  }

  // 2. Shared processes: EVEN SPLIT — never the full RSS to every identity
  // (that is the double-count lie). 1000 kb shared by 2 identities = 500
  // each; total 2000 keeps the sum at exactly 1000 attributed.
  {
    std::vector<ProcessSample> samples = {{201, 1000, {kA, kB}}};
    std::vector<AttributionRow> rows;
    XR_EXPECT(model.Attribute(samples, 2000, &rows));
    XR_EXPECT_EQ(Find(rows, kA)->kb, size_t(500));
    XR_EXPECT_EQ(Find(rows, kB)->kb, size_t(500));
    XR_EXPECT_EQ(Find(rows, "(unattributed)")->kb, size_t(1000));
    XR_EXPECT_EQ(Sum(rows), size_t(2000));
  }
  // 2b. An odd split stays within the law (remainder distributed, sum
  // still exact).
  {
    std::vector<ProcessSample> samples = {{201, 999, {kA, kB}}};
    std::vector<AttributionRow> rows;
    XR_EXPECT(model.Attribute(samples, 999, &rows));
    XR_EXPECT_EQ(Sum(rows), size_t(999));
    XR_EXPECT_EQ(Find(rows, kA)->kb + Find(rows, kB)->kb, size_t(999));
  }

  // 3. Fail-closed: samples that contradict the total (sum > total) yield
  // NO table — "attribution unavailable", never a plausible wrong answer.
  {
    std::vector<ProcessSample> samples = {{301, 5000, {kA}}};
    std::vector<AttributionRow> rows;
    XR_EXPECT_MSG(!model.Attribute(samples, 2000, &rows),
                  "contradictory samples refuse");
    XR_EXPECT_MSG(rows.empty(), "no table on refusal");
  }

  // 4. THE PLANTED DOUBLE-COUNT (DoD 13): rows that sum past the total are
  // caught by the audit invariant — the lie a naive "full RSS to every
  // identity" implementation would print.
  {
    std::vector<AttributionRow> planted = {
        {kA, 1000, 1}, {kB, 1000, 1}, {"(unattributed)", 0, 0}};
    XR_EXPECT_MSG(!AttributionModel::SumWithinTotal(planted, 1500),
                  "planted double count REDDENS (sum 2000 > total 1500)");
    std::vector<AttributionRow> honest = {
        {kA, 500, 1}, {kB, 500, 1}, {"(unattributed)", 500, 1}};
    XR_EXPECT_MSG(AttributionModel::SumWithinTotal(honest, 1500),
                  "honest table passes the invariant");
  }

  // 5. Null rows pointer: refused (no silent no-op).
  {
    std::vector<ProcessSample> samples;
    XR_EXPECT_MSG(!model.Attribute(samples, 100, nullptr),
                  "null rows refused");
  }

  return xrtest::Report("identity/attribution");
}
