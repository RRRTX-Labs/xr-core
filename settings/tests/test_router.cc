// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — the deep-link router (P8-T1): every section and every
// setting is addressable by a stable anchor; anchors are derived from the
// schema data (same derivation the T7 help-anchor contract checks), an
// unresolvable anchor is a TYPED error with near-miss suggestions, and
// section <-> anchor round-trips are exact.
#include <cstdio>
#include <string>
#include <vector>

#include "settings/core/router.h"
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
  Router router(schema);

  // 1. Home.
  {
    auto r = router.Resolve("");
    XR_EXPECT(r.ok);
    XR_EXPECT(r.kind == ResolveKind::kHome);
    auto r2 = router.Resolve("xr://settings");
    XR_EXPECT(r2.ok && r2.kind == ResolveKind::kHome);
  }

  // 2. Every section anchor resolves to its section.
  {
    auto anchors = router.SectionAnchors();
    XR_EXPECT_EQ(anchors.size(), 3);
    for (const std::string& a : anchors) {
      auto r = router.Resolve(a);
      XR_EXPECT_MSG(r.ok && r.kind == ResolveKind::kSection, a);
      if (r.ok) {
        XR_EXPECT_STREQ(r.canonical, a);
        XR_EXPECT(r.section == a.substr(std::string("xr://settings/").size()));
      }
    }
    // every section from the schema data is addressable
    for (const SectionDef* s : schema.Sections()) {
      std::string a = router.SectionAnchor(s->id);
      XR_EXPECT(a == "xr://settings/" + s->id);
    }
  }

  // 3. Every setting anchor resolves (canonical = section + anchor suffix,
  //    where the suffix drops the "<section>." prefix).
  {
    for (const SettingDef* st : schema.Settings()) {
      const std::string a = router.SettingAnchor(st->key);
      XR_EXPECT_MSG(!a.empty(), st->key);
      auto r = router.Resolve(a);
      XR_EXPECT_MSG(r.ok && r.kind == ResolveKind::kSetting, st->key);
      if (r.ok) {
        XR_EXPECT_STREQ(r.setting, st->key);
        XR_EXPECT_STREQ(r.section, st->section);
      }
    }
    // suffix derivation drops the owning-section prefix
    XR_EXPECT_STREQ(Router::AnchorSuffix("network", "network.adblock"), "adblock");
    XR_EXPECT_STREQ(router.SettingAnchor("network.adblock"),
                    "xr://settings/network/adblock");
  }

  // 4. Round-trip: section -> anchor -> section; setting -> anchor ->
  //    section + setting.
  {
    auto r1 = router.Resolve("xr://settings/privacy");
    XR_EXPECT(r1.ok && r1.kind == ResolveKind::kSection);
    XR_EXPECT_STREQ(r1.section, "privacy");
    auto r2 = router.Resolve("xr://settings/identity/storage");
    XR_EXPECT(r2.ok && r2.kind == ResolveKind::kSetting);
    XR_EXPECT_STREQ(r2.section, "identity");
    XR_EXPECT_STREQ(r2.setting, "identity.storage");
    XR_EXPECT_STREQ(r2.canonical, "xr://settings/identity/storage");
  }

  // 5. Dotted-key form resolves to the same canonical (host accepts both).
  {
    auto r = router.Resolve("xr://settings/network/network.adblock");
    XR_EXPECT(r.ok && r.kind == ResolveKind::kSetting);
    if (r.ok) XR_EXPECT_STREQ(r.setting, "network.adblock");
    auto s = router.Resolve("xr://settings/network/adblock");
    XR_EXPECT(s.ok && s.kind == ResolveKind::kSetting);
    XR_EXPECT_STREQ(r.canonical, s.canonical);
  }

  // 6. Unknown anchor: typed error + near-miss suggestions (never a silent
  //    redirect to home).
  {
    auto r = router.Resolve("xr://settings/network/not-a-setting");
    XR_EXPECT(!r.ok);
    XR_EXPECT(r.kind == ResolveKind::kUnknown);
    XR_EXPECT(!r.error.empty());
    // near-miss: partial section id suggests the real one (prefix-derived)
    auto s = router.Resolve("xr://settings/net");
    XR_EXPECT(!s.ok);
    XR_EXPECT(!s.suggestions.empty());
  }

  // 7. Unknown setting is never derived from arbitrary strings.
  {
    auto r = router.Resolve("xr://settings/network/../identity");
    XR_EXPECT(!r.ok || r.kind == ResolveKind::kUnknown || r.kind == ResolveKind::kHome);
  }

  std::printf("suite: %d checks, %d failures\n", xrtest::g_checks,
              xrtest::g_failures);
  return xrtest::Report("test_router");
}
