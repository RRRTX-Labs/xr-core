// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — settings search ranking (P8-T1): word-exact/prefix/
// subsequence semantics over the schema-derived index, deterministic order,
// stable tie-breaks, alias-only "container" vocabulary (search-only, never
// a label), and score monotonicity sanity. Byte-parity with the Python
// reference lives in the fakes parity gate (xr-browser).
#include <cstdio>
#include <string>

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

  // 1. Index: 3 sections + 11 settings in schema order; every setting
  //    carries key + section + aliases; sections carry their id.
  {
    XR_EXPECT_EQ(index.size(), 14);
    XR_EXPECT(index[0].kind == EntryKind::kSection);
    XR_EXPECT_STREQ(index[0].key, "network");
    XR_EXPECT_EQ(index[3].kind, EntryKind::kSetting);
    XR_EXPECT_STREQ(index[3].key, "network.adblock");
    XR_EXPECT(index[3].fields.size() >= 3);  // key + section + aliases
    for (size_t i = 1; i < index.size(); ++i)
      XR_EXPECT(index[i - 1].order < index[i].order);
  }

  auto top1 = [&](const std::string& q) -> std::string {
    auto r = MatchQuery(q, index);
    return r.empty() ? std::string() : r[0].key;
  };
  auto top3 = [&](const std::string& q) {
    auto r = MatchQuery(q, index);
    std::vector<std::string> keys;
    for (size_t i = 0; i < r.size() && i < 3; ++i) keys.push_back(r[i].key);
    return keys;
  };

  // 2. Direct and paraphrase hits land on the right setting.
  XR_EXPECT_STREQ(top1("adblock"), "network.adblock");
  XR_EXPECT_STREQ(top1("ads"), "network.adblock");
  XR_EXPECT_STREQ(top1("block ads"), "network.adblock");
  XR_EXPECT_STREQ(top1("advertisements stop"), "network.adblock");
  {
    auto t3 = top3("trackers");
    XR_EXPECT(t3[0] == "network.tracker-block");
  }
  XR_EXPECT_STREQ(top1("https upgrade"), "network.https-upgrade");
  XR_EXPECT_STREQ(top1("notifications"), "privacy.notifications");

  // 3. Search-only vocabulary: "container(s)" is an alias for the identity
  //    vocabulary and resolves there — and never appears as a label field
  //    (aliases are the only copy-adjacent words in the index; the UI
  //    renders ids, enforced by l10n_extract).
  {
    XR_EXPECT_STREQ(top1("container"), "identity.kind");
    XR_EXPECT_STREQ(top1("containers"), "identity.kind");
    auto r = MatchQuery("container", index);
    XR_EXPECT(!r.empty());
    XR_EXPECT(r[0].kind == EntryKind::kSetting);
  }

  // 4. Typo tolerance (subsequence), prefix, and word-order flip.
  {
    auto t3 = top3("adbloc settings");   // deletion typo on adblock
    XR_EXPECT(t3[0] == "network.adblock");
    auto r = top3("blocker ad");          // order flip
    XR_EXPECT(r[0] == "network.adblock");
  }
  XR_EXPECT_STREQ(top1("notif"), "privacy.notifications");  // prefix

  // 5. Determinism + stable tie-breaks (score desc, then schema order).
  {
    auto a = MatchQuery("ad", index);
    auto b = MatchQuery("ad", index);
    XR_EXPECT_EQ(a.size(), b.size());
    if (a.size() == b.size()) {
      for (size_t i = 0; i < a.size(); ++i) {
        XR_EXPECT_STREQ(a[i].key, b[i].key);
        XR_EXPECT_EQ(a[i].score, b[i].score);
      }
    }
    // scores are monotonically non-increasing
    for (size_t i = 1; i < a.size(); ++i)
      XR_EXPECT(a[i - 1].score >= a[i].score);
  }

  // 6. Empty/whitespace query: no results (search-first UI shows the
  //    section list, not a phantom ranking).
  XR_EXPECT_EQ(MatchQuery("", index).size(), 0);
  XR_EXPECT_EQ(MatchQuery("   ", index).size(), 0);

  // 7. Section-level match: "network" ranks the section before the settings.
  {
    auto r = MatchQuery("network", index);
    XR_EXPECT(!r.empty());
    XR_EXPECT(r[0].kind == EntryKind::kSection);
    XR_EXPECT_STREQ(r[0].key, "network");
    XR_EXPECT(r[0].score >= r[1].score);
  }

  // 8. RTL aliases resolve byte-exactly (no lowercasing of Arabic).
  {
    std::string arabic = "\xd8\xa5\xd8\xb9\xd9\x84\xd8\xa7\xd9\x86\xd8\xa7\xd8\xaa";
    auto r = MatchQuery(arabic, index);  // "إعلانات"
    XR_EXPECT(!r.empty());
    XR_EXPECT(r[0].score > 0);
  }

  std::printf("suite: %d checks, %d failures\n", xrtest::g_checks,
              xrtest::g_failures);
  return xrtest::Report("test_settings_search");
}
