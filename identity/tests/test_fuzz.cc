// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// identity fuzz suite (P14-T1, the fleet's never-crash oracle): a seeded,
// deterministic op-loop over the whole core — mint, lifecycle, scheduler,
// binding, templates, attribution — asserting the INVARIANTS after every
// step, not just absence of crashes:
//   * every store key stays an opaque domain (no name-derived key can
//     appear, whatever the fuzz throws at the mint);
//   * the audit trail never contains a suggestion-caused change (the
//     never-auto-switch law, checked continuously);
//   * attribution stays within the total (Σ attributed ≤ measured);
//   * wake/destroy never resurrect: a purged domain is gone for good.
// The fleet contract (themes/tests/test_fuzz.cc shape): XR_FUZZ_SECONDS
// bounds the wall-clock budget (default 30 s; the gate runs >=60 s, the
// evidence campaign 600 s), XR_FUZZ_SEED fixes the op sequence — same seed,
// same ops, so a violation is reproducible by re-running with the seed.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "harness.h"
#include "core/attribution.h"
#include "core/binding.h"
#include "core/hibernate.h"
#include "core/identity.h"
#include "core/mint.h"
#include "core/templates.h"

using xr::identity::AttributionModel;
using xr::identity::AttributionRow;
using xr::identity::BindingModel;
using xr::identity::ChangeCause;
using xr::identity::CreateRequest;
using xr::identity::IdentityRecord;
using xr::identity::IdentityStore;
using xr::identity::Manager;
using xr::identity::ProcessSample;
using xr::identity::Scheduler;
using xr::identity::State;

int main() {
  long long budget_s = 30;
  if (const char* e = std::getenv("XR_FUZZ_SECONDS")) {
    budget_s = std::atoll(e);
  }
  uint64_t seed = 20260910;
  if (const char* e = std::getenv("XR_FUZZ_SEED")) seed = std::atoll(e);
  std::mt19937 rng(static_cast<unsigned>(seed));  // the seed IS the repro key
  const auto start = std::chrono::steady_clock::now();
  const auto deadline = start + std::chrono::seconds(budget_s);
  long long iter = 0;
  IdentityStore store;
  Manager m(&store);
  Scheduler s(&m, 1 + (rng() % 6));
  BindingModel b;
  std::vector<std::string> live;   // domains believed alive
  int resurrect_attempts = 0;

  while (std::chrono::steady_clock::now() < deadline) {
    ++iter;
    const int op = static_cast<int>(rng() % 10);
    if (op == 0) {  // provision (sometimes with hostile entropy)
      CreateRequest req;
      static const char* kHostile[] = {
          "", "work", "https://mail.example", "xr:00000000-0000-4000-8000-"
          "000000000001", "partition:work", "Identity", "\xff\xfe"};
      req.entropy = kHostile[rng() % 7];
      if (req.entropy.empty()) req.entropy = "e" + std::to_string(rng());
      req.display_name = "n" + std::to_string(rng() % 50);
      req.in_memory = (rng() % 4) == 0;
      IdentityRecord rec;
      if (m.Create(req, &rec).ok) {
        // INVARIANT: the minted key is opaque (never embeds the entropy).
        XR_EXPECT(xr::identity::LooksOpaque(rec.domain, req.entropy));
        live.push_back(rec.domain);
      }
      if (live.size() > 32) {  // bound the campaign's memory footprint
        bool v = false;
        const auto r = m.Destroy(live.front(), &v);
        XR_EXPECT_MSG(!r.ok || v,
                      "bound-destroy verifies (no residuals planted here)");
        live.erase(live.begin());
      }
    } else if (op == 1 && !live.empty()) {  // activate via scheduler
      s.Activate(live[rng() % live.size()], rng());
    } else if (op == 2 && !live.empty()) {  // hibernate
      s.Hibernate(live[rng() % live.size()], rng());
    } else if (op == 3 && !live.empty()) {  // wake (may be purged -> refuse)
      const std::string d = live[rng() % live.size()];
      if (!s.Wake(d, rng()).ok) ++resurrect_attempts;
    } else if (op == 4 && !live.empty()) {  // destroy (+ re-destroy: unknown)
      const size_t idx = rng() % live.size();
      const std::string d = live[idx];
      bool verified = false;
      const auto res = m.Destroy(d, &verified);
      // No residual is ever planted in this campaign, so a SUCCESSFUL
      // destroy must verify (§1.4), and a re-destroy of the now-purged
      // domain is kUnknownIdentity (never a resurrect, never a crash).
      XR_EXPECT_MSG(!res.ok || verified,
                    "a successful destroy always verifies (no residuals "
                    "planted in this campaign)");
      if (res.ok) {
        live.erase(live.begin() + static_cast<long>(idx));
        XR_EXPECT_MSG(store.Find(d) == nullptr,
                      "a verified destroy leaves no record");
        XR_EXPECT_MSG(!s.Wake(d, rng()).ok,
                      "wake never resurrects a purged domain");
      }
    } else if (op == 5) {  // binding: open/move/suggest/autoswitch
      const uint64_t tab = rng() % 64;
      if ((rng() % 3) == 0) {
        b.SetWindowDefault("w", "xr:00000000-0000-4000-8000-00000000000"
                                + std::to_string(1 + rng() % 2));
      }
      b.OpenTab("w", tab, "");
      if ((rng() % 2) == 0) {
        b.MoveTab("x", "y", tab, rng() % 2 == 0, {});
      }
      b.RecordSuggestion("site.example", "xr:00000000-0000-4000-8000-"
                                         "000000000001");
      b.TestPlantedAutoSwitch(tab, "xr:00000000-0000-4000-8000-000000000002");
      // INVARIANT (continuously): no suggestion-caused change exists in
      // the recent audit window (the deterministic suite checks the whole
      // trail; here the cost must stay bounded for long campaigns).
      const auto& trail = b.changes();
      for (size_t k = trail.size() > 50 ? trail.size() - 50 : 0;
           k < trail.size(); ++k) {
        XR_EXPECT(trail[k].cause != ChangeCause::kSuggestion);
      }
    } else if (op == 6) {  // templates apply + ceremony completeness
      static const char* kIds[] = {"personal", "work", "research", "banking",
                                   "shopping", "disposable", "tor", "ghost"};
      const std::string id = kIds[rng() % 8];
      std::map<std::string, std::string> prefs;
      std::string c, g;
      std::vector<xr::identity::TemplateRow> applied;
      if (xr::identity::ApplyTemplate(id, &prefs, &c, &g, &applied)) {
        auto ceremony = xr::identity::Ceremony(id);
        XR_EXPECT(ceremony.has_value());
        size_t listed = 0;
        for (const auto& line : *ceremony) {
          listed += line.rfind("applied ", 0) == 0 ? 1 : 0;
        }
        XR_EXPECT_EQ(listed, applied.size());  // inventory == application
      }
    } else if (op == 7) {  // attribution: random samples, invariants hold
      std::vector<ProcessSample> samples;
      for (int k = 0, n = static_cast<int>(rng() % 5); k < n; ++k) {
        ProcessSample ps;
        ps.pid = rng() % 1000;
        ps.rss_kb = rng() % 100000;
        for (int j = 0, q = static_cast<int>(rng() % 3); j < q; ++j) {
          ps.serves.push_back(live.empty()
                                  ? "xr:00000000-0000-4000-8000-000000000001"
                                  : live[rng() % live.size()]);
        }
        samples.push_back(std::move(ps));
      }
      std::vector<AttributionRow> rows;
      AttributionModel model;
      if (model.Attribute(samples, 500000, &rows)) {
        size_t sum = 0;
        for (const auto& r : rows) sum += r.kb;
        XR_EXPECT(sum <= 500000);  // Σ attributed ≤ total, always
      }
    } else if (op == 8) {  // mint directly with hostile entropy
      std::string dom;
      const std::string hostile =
          "name-title-url-" + std::to_string(rng()) + "-\xff\x01";
      if (xr::identity::MintDomain(hostile, &dom)) {
        XR_EXPECT(xr::identity::LooksOpaque(dom, hostile));
      }
    } else {  // lifecycle on random/opaque strings (unknown identities)
      const std::string junk = "xr:" + std::to_string(rng());
      m.Activate(junk);
      m.Hibernate(junk);
      bool v = false;
      m.Destroy(junk, &v);
    }
  }
  XR_EXPECT_MSG(resurrect_attempts >= 0, "wake refusals counted (sanity)");
  const int rc = xrtest::Report("identity/fuzz");
  std::printf("identity fuzz: %d violations (seed %llu, %lld s budget, "
              "%lld iters)\n", xrtest::g_failures,
              static_cast<unsigned long long>(seed), budget_s, iter);
  return rc;
}
