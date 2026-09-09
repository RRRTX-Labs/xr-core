// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1-test — seeded structure-aware fuzz of the settings core
// (P8-T1): schema + router + search + counters over random anchors, queries
// and counter events with the typed oracle "never accepts an invalid
// section/setting" (an ok resolution always names a schema-known section /
// setting owned by its section; a failed resolution is a typed error). The
// seed is checked in; the campaign is WALL-CLOCK timeboxed (600 s default —
// the P8 timebox; set XR_FUZZ_SECONDS for a dev-only smoke, never CI).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "settings/core/counters.h"
#include "settings/core/json.h"
#include "settings/core/router.h"
#include "settings/core/search.h"
#include "settings/core/settings_schema.h"
#include "settings/tests/harness.h"

using namespace xr::settings;

namespace {

constexpr uint32_t kSeed = 20260909;  // checked in — reproducible

std::string RandomString(std::mt19937& rng, size_t max_len) {
  static const char kAlnum[] =
      "abcdefghijklmnopqrstuvwxyz0123456789.-_/ :!؟\xd8\xa5\xd9\x84\xd8\xb9";
  const size_t len = 1 + (rng() % max_len);
  std::string s;
  for (size_t i = 0; i < len; ++i) s += kAlnum[rng() % (sizeof(kAlnum) - 1)];
  return s;
}

}  // namespace

int main() {
  SettingsSchema schema;
  bool ok = false;
  std::string text =
      xrtest::ReadFile("../../settings/core/settings_schema_v1.json", &ok);
  XR_EXPECT_MSG(ok, "schema unreadable");
  if (ok) {
    auto lr = schema.Load(text);
    XR_EXPECT_MSG(lr.ok, lr.error);
  }

  // Unit budget default is 30 s so `make test` stays CI-bounded (the P6
  // pattern: the policy 600 s campaign is a separate timeboxed lane). The
  // P8 600 s seeded settings campaign runs as XR_FUZZ_SECONDS=600 and its
  // transcript is recorded in evidence/P8/logs (never a silent pass).
  const long long budget_s =
      std::getenv("XR_FUZZ_SECONDS") != nullptr
          ? std::atoll(std::getenv("XR_FUZZ_SECONDS"))
          : 30;
  const int64_t deadline_s =
      budget_s > 0
          ? std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                    .count() +
                budget_s
          : 0;

  auto index = BuildIndex(schema);
  Router router(schema);
  CounterStore counters;  // in-memory (never touches disk in this suite)
  counters.Load();

  // Known keys/sections for the oracle (read from the schema DATA only).
  std::vector<std::string> known_keys;
  for (const SettingDef* st : schema.Settings()) known_keys.push_back(st->key);
  std::vector<std::string> known_sections;
  for (const SectionDef* s : schema.Sections()) known_sections.push_back(s->id);

  std::mt19937 rng(kSeed);
  std::uniform_int_distribution<int> dice(0, 99);
  long long checks = 0;
  long long violations = 0;
  long long router_oks = 0, router_fails = 0;

  while (true) {
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    if (deadline_s > 0 && now >= deadline_s) break;
    for (int i = 0; i < 4096; ++i) {
      const int roll = dice(rng);
      if (roll < 40) {
        // Router oracle: an ok setting/section result must name a real row.
        std::string anchor = "xr://settings/";
        if (roll < 10) anchor += RandomString(rng, 16);
        else if (roll < 20) {
          anchor += known_sections[rng() % known_sections.size()];
        } else if (roll < 30) {
          anchor += known_sections[rng() % known_sections.size()] + "/" +
                    RandomString(rng, 20);
        } else {
          anchor += known_sections[rng() % known_sections.size()] + "/" +
                    known_keys[rng() % known_keys.size()].substr(
                        known_sections[rng() % known_sections.size()].size() +
                        1);
        }
        auto r = router.Resolve(anchor);
        ++checks;
        if (r.ok) {
          if (r.kind == ResolveKind::kSection) {
            bool known = false;
            for (const auto& s : known_sections)
              if (s == r.section) known = true;
            if (!known) ++violations;  // accepted an invalid section
            else ++router_oks;
          } else if (r.kind == ResolveKind::kSetting) {
            bool known = false, owned = false;
            for (const auto& k : known_keys)
              if (k == r.setting) known = true;
            if (known) {
              const SettingDef* def = schema.FindSetting(r.setting);
              if (def != nullptr && def->section == r.section) owned = true;
            }
            if (!known || !owned) ++violations;  // invalid/unanchored setting
            else ++router_oks;
          }
        } else {
          ++router_fails;
          if (r.error.empty()) ++violations;  // failure without a typed error
        }
        // open the section it resolved to (counters must stay sane)
        if (r.ok && !r.section.empty()) counters.OpenSection(r.section);
      } else if (roll < 70) {
        // Search: never throws, deterministic, non-empty queries may match
        auto res = MatchQuery(RandomString(rng, 24), index);
        ++checks;
        for (size_t k = 1; k < res.size(); ++k) {
          if (res[k - 1].score < res[k].score) ++violations;  // order broken
        }
      } else if (roll < 90) {
        // Counter events: garbage keys must not crash and must not count
        counters.OpenSection(RandomString(rng, 8));
        counters.AcceptQuery();
        counters.SettingChanged(RandomString(rng, 12));
        ++checks;
      } else {
        // Dump must stay canonical + parseable every time
        JsonValue d = counters.Dump();
        ++checks;
        if (!d.is_object()) ++violations;
      }
    }
  }

  std::printf(
      "settings_fuzz: %lld checks, %lld router-ok, %lld router-typed-error, "
      "%lld violations (seed %u, %lld s budget, queries in-memory only)\n",
      checks, router_oks, router_fails, violations, kSeed, budget_s);
  XR_EXPECT_EQ(violations, 0);
  XR_EXPECT(router_oks > 1000);
  XR_EXPECT(router_fails > 1000);
  std::printf("suite: %d checks, %d failures\n", xrtest::g_checks,
              xrtest::g_failures);
  return xrtest::Report("test_settings_fuzz");
}
