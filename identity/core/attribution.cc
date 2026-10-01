// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — attribution implementation (header has the laws).
#include "core/attribution.h"

#include <algorithm>
#include <set>
#include <utility>

namespace xr::identity {

bool AttributionModel::Attribute(std::vector<ProcessSample> samples,
                                 size_t total_kb,
                                 std::vector<AttributionRow>* rows) const {
  if (rows == nullptr) return false;
  rows->clear();
  // Exclusive (single-identity) and shared (multi-identity) processes.
  // A process serving NO identity still exists: its memory lands in the
  // unattributed row (browser chrome, GPU, network service, ...).
  std::map<std::string, size_t> per_domain;
  std::map<std::string, size_t> per_domain_procs;
  size_t unattributed = 0, unattributed_procs = 0;
  size_t sample_sum = 0;
  for (auto& s : samples) {
    sample_sum += s.rss_kb;
    std::set<std::string> unique(s.serves.begin(), s.serves.end());
    if (unique.empty()) {
      unattributed += s.rss_kb;
      ++unattributed_procs;
      continue;
    }
    if (unique.size() == 1) {
      // exclusive: every kb to the one identity
      per_domain[*unique.begin()] += s.rss_kb;
      ++per_domain_procs[*unique.begin()];
    } else {
      // shared: EVEN SPLIT (documented approximation; the double-count —
      // full RSS to every identity — is the lie this model must not tell)
      const size_t share = s.rss_kb / unique.size();
      size_t remainder = s.rss_kb % unique.size();
      for (const auto& d : unique) {
        size_t kb = share;
        if (remainder > 0) { kb += 1; --remainder; }
        per_domain[d] += kb;
        ++per_domain_procs[d];
      }
    }
  }
  // Fail-closed: samples that already exceed the measured total are a
  // contradiction — no table (the caller shows "unavailable").
  if (sample_sum > total_kb) return false;
  for (const auto& [domain, kb] : per_domain) {
    rows->push_back({domain, kb, per_domain_procs[domain]});
  }
  unattributed += (total_kb - sample_sum);  // memory no sample explains
  rows->push_back({"(unattributed)", unattributed, unattributed_procs});
  std::sort(rows->begin(), rows->end(),
            [](const AttributionRow& a, const AttributionRow& b) {
              return a.kb > b.kb;  // biggest first: "what is using my RAM"
            });
  return SumWithinTotal(*rows, total_kb);
}

bool AttributionModel::SumWithinTotal(
    const std::vector<AttributionRow>& rows, size_t total_kb) {
  size_t sum = 0;
  for (const auto& r : rows) {
    if (sum + r.kb < sum) return false;  // overflow paranoia
    sum += r.kb;
  }
  return sum <= total_kb;
}

}  // namespace xr::identity
