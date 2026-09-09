// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — the P8 settings-search recall gate: >=95% of the
// committed 200-phrase corpus (settings/tests/recall_corpus.json, generated
// from the schema aliases by gen_recall_corpus.py — never hand-edited)
// resolves to the RIGHT setting in the top 3. Prints the measured %.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "settings/core/json.h"
#include "settings/core/search.h"
#include "settings/core/settings_schema.h"
#include "settings/tests/harness.h"

using namespace xr::settings;

namespace {

SettingsSchema LoadSchema() {
  SettingsSchema s;
  bool ok = false;
  std::string t = xrtest::ReadFile("../../settings/core/settings_schema_v1.json", &ok);
  if (ok) s.Load(t);
  return s;
}

}  // namespace

int main() {
  SettingsSchema schema = LoadSchema();
  XR_EXPECT_EQ(schema.Count(), 11);
  auto index = BuildIndex(schema);

  bool ok = false;
  std::string text = xrtest::ReadFile("../../settings/tests/recall_corpus.json", &ok);
  XR_EXPECT_MSG(ok, "recall corpus missing — run gen_recall_corpus.py --check");
  if (!ok) return xrtest::Report("test_recall_corpus");

  auto pr = ParseJson(text);
  XR_EXPECT_MSG(pr.ok, pr.error);
  const JsonValue* qp = pr.ok ? pr.value.find("queries") : nullptr;
  XR_EXPECT(qp != nullptr && qp->is_array());

  long long total = 0, hit = 0;
  if (qp != nullptr && qp->is_array()) {
    for (const JsonValue& qe : qp->as_array()) {
      const JsonValue* q = qe.find("query");
      const JsonValue* expect = qe.find("expect");
      if (q == nullptr || expect == nullptr || !q->is_string() ||
          !expect->is_string()) {
        XR_EXPECT_MSG(false, "corpus row malformed");
        continue;
      }
      ++total;
      auto res = MatchQuery(q->as_string(), index);
      bool top3 = false;
      for (size_t i = 0; i < res.size() && i < 3; ++i) {
        if (res[i].key == expect->as_string()) top3 = true;
      }
      if (top3) ++hit;
    }
  }
  const double pct = total > 0 ? (100.0 * static_cast<double>(hit)) /
                                     static_cast<double>(total)
                               : 0.0;
  std::printf("recall %lld/%lld = %.1f%% (bar: >=95%% top-3)\n", hit, total,
              pct);
  XR_EXPECT_MSG(total == 200, "corpus must contain exactly 200 phrases");
  XR_EXPECT_MSG(pct >= 95.0, "recall below the 95% bar — fix the index, not the bar");
  std::printf("suite: %d checks, %d failures\n", xrtest::g_checks,
              xrtest::g_failures);
  return xrtest::Report("test_recall_corpus");
}
