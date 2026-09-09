// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — the section registry is GENERATED from the schema data
// (never a hand-maintained list): ordering, anchors, per-section settings,
// availability predicates against the pinned P6 snapshot (deny unknown), and
// the identity.active gate turning on with an identity in the context.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "settings/core/sections.h"
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

std::string StateDoc(bool identity_active) {
  std::string ctx = identity_active ? "{\"identity_id\":\"xr:0000-0001\",\"origin\":null}"
                                    : "{\"identity_id\":null,\"origin\":null}";
  return "{\"schema\":\"xr-settings-state\",\"schema_version\":1,\"context\":" +
         ctx +
         ",\"values\":{\"network.adblock\":{\"value\":false,\"source\":"
         "\"enterprise\",\"managed\":true}}}";
}

}  // namespace

int main() {
  SettingsSchema schema = LoadSchema();
  XR_EXPECT_EQ(schema.Count(), 11);

  // 1. No identity: exactly the three declared sections; identity is gated.
  {
    PolicyState st;
    auto r = st.Load(StateDoc(false));
    XR_EXPECT_MSG(r.ok, r.error);
    auto reg = BuildRegistry(schema, st);
    XR_EXPECT_EQ(reg.size(), 3);
    // registry order == schema order
    XR_EXPECT_STREQ(reg[0].id, "network");
    XR_EXPECT_STREQ(reg[1].id, "privacy");
    XR_EXPECT_STREQ(reg[2].id, "identity");
    for (const auto& s : reg) {
      XR_EXPECT_STREQ(s.anchor, std::string("xr://settings/") + s.id);
      if (s.id == "identity") {
        XR_EXPECT(!s.available);
        XR_EXPECT(!s.unavailable_reason.empty());
      } else {
        XR_EXPECT_MSG(s.available, s.id);
        XR_EXPECT(s.unavailable_reason.empty());
      }
      XR_EXPECT_MSG(!s.setting_keys.empty(), s.id);
      XR_EXPECT_EQ(s.availability == "identity.active", s.id == "identity");
    }
    // network owns exactly its four rows, in schema order
    XR_EXPECT_STREQ(reg[0].setting_keys[0], "network.adblock");
    XR_EXPECT_EQ(reg[0].setting_keys.size(), 4);
    XR_EXPECT_EQ(reg[1].setting_keys.size(), 4);
    XR_EXPECT_EQ(reg[2].setting_keys.size(), 3);
  }

  // 2. Active identity: every section available.
  {
    PolicyState st;
    XR_EXPECT(st.Load(StateDoc(true)).ok);
    auto reg = BuildRegistry(schema, st);
    for (const auto& s : reg) XR_EXPECT_MSG(s.available, s.id);
    // identity rows are per-identity scoped
    for (const SettingDef* def : schema.Settings()) {
      if (def->section == "identity")
        XR_EXPECT_STREQ(def->scope, "per-identity");
      else
        XR_EXPECT_STREQ(def->scope, "global");
    }
  }

  // 3. Pinned snapshot is read only through the state object (no live
  //    resolver): enterprise rows surface as preempted values.
  {
    PolicyState st;
    XR_EXPECT(st.Load(StateDoc(true)).ok);
    XR_EXPECT(st.HasValue("network.adblock"));
    auto v = st.GetValue("network.adblock");
    XR_EXPECT(v.ok);
    XR_EXPECT(v.value.is_bool() && !v.value.as_bool());
    XR_EXPECT_STREQ(v.source, "enterprise");
    XR_EXPECT(!st.HasValue("network.route"));
    auto m = st.GetValue("network.route");
    XR_EXPECT(!m.ok);
  }

  // 4. Unknown availability predicate denies (L3 — never a guess) and the
  //    unknown-context/values fields are rejected (strict snapshot).
  {
    PolicyState st;
    auto bad = st.Load("{\"schema\":\"xr-settings-state\",\"schema_version\":1,"
                       "\"context\":{},\"values\":{}}");
    XR_EXPECT(bad.ok);  // empty context is valid; identity inactive
    auto bad2 = st.Load("{\"schema\":\"xr-settings-state\",\"schema_version\":1,"
                        "\"context\":{\"rogue\":1},\"values\":{}}");
    XR_EXPECT_MSG(!bad2.ok, "unknown context field must reject");
    auto bad3 = st.Load("{\"schema\":\"xr-settings-state\",\"schema_version\":1,"
                        "\"context\":{},\"values\":{\"network.adblock\":"
                        "{\"value\":true,\"source\":\"hacker\"}}}");
    XR_EXPECT_MSG(!bad3.ok, "unknown source must reject");
    auto bad4 = st.Load("{\"schema\":\"nope\",\"schema_version\":1,"
                        "\"context\":{},\"values\":{}}");
    XR_EXPECT(!bad4.ok);
  }

  // 5. SectionAvailable unit semantics over a synthetic def.
  {
    SectionDef always;
    always.id = "x";
    always.availability = "always";
    PolicyState st;
    XR_EXPECT(st.Load(StateDoc(false)).ok);
    std::string reason;
    XR_EXPECT(SectionAvailable(always, st, &reason));
    SectionDef unknown_pred;
    unknown_pred.id = "y";
    unknown_pred.availability = "mystery";
    XR_EXPECT(!SectionAvailable(unknown_pred, st, &reason));
    XR_EXPECT(reason.find("unknown") != std::string::npos);
  }

  std::printf("suite: %d checks, %d failures\n", xrtest::g_checks,
              xrtest::g_failures);
  return xrtest::Report("test_sections");
}
