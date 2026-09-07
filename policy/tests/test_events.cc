// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — policy-change events: golden strings (schema-conformant
// canonical JSON), concrete-delta correctness, undo affordance semantics,
// and the absence laws (NO score/risk/grade vocabulary; NO auto_reload
// field) — the vocab/absence lint counterpart at the generator level.
#include <string>

#include "policy/core/events.h"
#include "policy/core/resolve.h"
#include "harness.h"

using namespace xr::policy;

namespace {

EffectivePolicy ResolveOrDie(const char* req) {
  auto p = ParseJson(req);
  XR_EXPECT_MSG(p.ok, "fixture must parse");
  RequestParse rp = ParseResolveRequest(p.value);
  return Resolve(rp.request).policy;
}

}  // namespace

int main() {
  // Build old/new from the real resolver (Standard -> Fortress on same site).
  EffectivePolicy old_p = ResolveOrDie(
      "{\"identity\":{\"value\":\"xr:00000000-0000-4000-8000-000000000001\"},"
      "\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"example.com\"},"
      "\"request_class\":\"kNavigation\"}");
  EffectivePolicy new_p = ResolveOrDie(
      "{\"identity\":{\"value\":\"xr:00000000-0000-4000-8000-000000000001\"},"
      "\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"example.com\"},"
      "\"request_class\":\"kNavigation\",\"trust_context\":\"kFortress\"}");

  // ---- deltas are concrete and ordered ----
  auto deltas = DiffPolicies(old_p, new_p);
  XR_EXPECT(deltas.size() >= 8);  // perms(3)+cosmetic(2)+route+fp+letterbox+...
  bool saw_camera = false, saw_letterbox = false, saw_route = false;
  for (const auto& d : deltas) {
    if (d.path == "permissions.camera") {
      saw_camera = true;
      XR_EXPECT_STREQ(d.from.c_str(), "kAsk");
      XR_EXPECT_STREQ(d.to.c_str(), "kDeny");
      XR_EXPECT_STREQ(d.change.c_str(), "blocked");
      XR_EXPECT_STREQ(d.human.c_str(), "camera now blocked");
    }
    if (d.path == "letterbox") saw_letterbox = true;
    if (d.path == "egress.route") {
      saw_route = true;
      XR_EXPECT_STREQ(d.human.c_str(), "route now tor");
    }
  }
  XR_EXPECT(saw_camera && saw_letterbox && saw_route);

  // ---- summary is concrete copy ----
  std::string summary = BuildSummary(deltas);
  XR_EXPECT(summary.find("camera now blocked") != std::string::npos);
  XR_EXPECT(summary.find("microphone now blocked") != std::string::npos);
  XR_EXPECT(summary.find("geolocation now blocked") != std::string::npos);
  XR_EXPECT(summary.find(" · ") != std::string::npos);

  // ---- golden canonical string (schema policy-change-event-v1) ----
  std::string gold = BuildPolicyChangeEvent("xr:00000000-0000-4000-8000-000000000001",
                                            "example.com", "kStandard", "kFortress",
                                            old_p, new_p, true).Canonical();
  // Structure assertions (full goldens live in the committed fixture file,
  // diffed by the python side; here we pin the invariants).
  XR_EXPECT(gold.rfind("{\"contract_version\":1,\"deltas\":[", 0) == 0);
  XR_EXPECT(gold.find("\"event\":\"policy_changed\"") != std::string::npos);
  XR_EXPECT(gold.find("\"identity\":\"xr:00000000-0000-4000-8000-000000000001\"") != std::string::npos);
  XR_EXPECT(gold.find("\"site\":\"example.com\"") != std::string::npos);
  XR_EXPECT(gold.find("\"previous_trust\":\"kStandard\"") != std::string::npos);
  XR_EXPECT(gold.find("\"new_trust\":\"kFortress\"") != std::string::npos);
  XR_EXPECT(gold.find("\"undo\":true") != std::string::npos);
  XR_EXPECT(gold.find("\"summary\":\"hide_cosmetic on") != std::string::npos);  // deterministic delta order: cosmetic first

  // ---- absence laws (schema + generator level) ----
  for (const char* banned : {"score", "risk", "grade", "auto_reload", "autoreload"}) {
    XR_EXPECT_MSG(gold.find(banned) == std::string::npos,
                  std::string("banned token must be absent: ") + banned);
  }
  // Undo=false path (enterprise-enforced change).
  std::string no_undo = BuildPolicyChangeEvent("i", "s.com", "kStandard", "kStandard",
                                               old_p, old_p, false).Canonical();
  XR_EXPECT(no_undo.find("\"undo\":false") != std::string::npos);
  XR_EXPECT(no_undo.find("\"deltas\":[]") != std::string::npos);
  XR_EXPECT(no_undo.find("\"summary\":\"\"") != std::string::npos);

  // ---- identical policies => zero deltas ----
  XR_EXPECT(DiffPolicies(old_p, old_p).empty());

  // ---- determinism ----
  std::string again = BuildPolicyChangeEvent("xr:00000000-0000-4000-8000-000000000001",
                                             "example.com", "kStandard", "kFortress",
                                             old_p, new_p, true).Canonical();
  XR_EXPECT(gold == again);

  return xrtest::Report("test_events");
}
