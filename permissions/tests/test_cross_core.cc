// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P15 cross-core conformance: the permissions core feeds the policy resolver
// through ONE seam, the `permission_overlay` JSON object. This suite proves
// two things.
//  (A) docs/contracts/vectors/permission-overlay-v1.json (33 additive vectors,
//      literal resolver requests) resolves to the expected four permission
//      fields through the REAL ParseResolveRequest + Resolve.
//  (B) store -> ProjectViewJson -> resolver agrees with the store's meaning
//      across the same lifecycle the ops perform (set, grant, expire, deny,
//      corrupt, persist-corrupt, revoke, spend).
// The frozen 66-vector suite (policy/tests) is NOT edited and still runs.
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "permissions/core/json.h"
#include "permissions/core/ops.h"
#include "permissions/core/store.h"
#include "policy/core/resolve.h"
#include "policy/tests/harness.h"

using namespace xr::permissions;

namespace {

const char* kA = "xr:00000000-0000-4000-8000-0000000000a1";
const char* kB = "xr:00000000-0000-4000-8000-0000000000b2";
constexpr int64_t kNow = 1000000;
constexpr int64_t kDay7 = 604800000;

std::string VectorsPath() {
  const char* env = std::getenv("XR_BROWSER_ROOT");
  std::string root = (env != nullptr && *env) ? env : "../../../xr-browser";
  return root + "/docs/contracts/vectors/permission-overlay-v1.json";
}

// Four frozen permission fields from a resolver output, as name -> state name.
std::string PermissionsOf(const xr::policy::ResolveOutput& out) {
  using xr::policy::PermissionState;
  auto n = [](PermissionState s) {
    return std::string(s == PermissionState::kDeny ? "kDeny" : s == PermissionState::kAsk ? "kAsk" : "kAllow");
  };
  return "geolocation=" + n(out.policy.geolocation) + " camera=" + n(out.policy.camera) +
         " microphone=" + n(out.policy.microphone) + " notifications=" + n(out.policy.notifications);
}

// Builds a resolver request for `identity` with `view` injected as the overlay.
xr::policy::ResolveRequest RequestWithView(const std::string& identity, const std::string& domain,
                                           int64_t now, const std::string& session,
                                           const std::string& view_json) {
  JsonValue::Object o;
  o.emplace("identity", JsonValue::Object{{"value", JsonValue(identity)}});
  o.emplace("origin", JsonValue::Object{{"registrable_domain", JsonValue(domain)}, {"scheme", JsonValue("https")}});
  o.emplace("request_class", JsonValue("kNavigation"));
  JsonValue::Array ids;
  for (const char* id : {kA, kB}) {
    ids.push_back(JsonValue::Object{{"value", JsonValue(id)}, {"ephemeral", JsonValue(false)}, {"fortress", JsonValue(false)}});
  }
  o.emplace("identities", JsonValue(std::move(ids)));
  o.emplace("now_ms", JsonValue(static_cast<int64_t>(now)));
  o.emplace("session_id", JsonValue(session));
  JsonParseResult view = ParseJson(view_json);
  if (!view.ok) {
    XR_EXPECT_MSG(false, "fixture: view JSON parses");
  }
  o.emplace("permission_overlay", view.value);
  auto rp = xr::policy::ParseResolveRequest(JsonValue(std::move(o)));
  return rp.request;
}

std::string ResolvedPerms(const std::string& identity, const std::string& domain, int64_t now,
                    const std::string& session, const std::string& view_json) {
  xr::policy::ResolveOutput out = xr::policy::Resolve(RequestWithView(identity, domain, now, session, view_json));
  XR_EXPECT_MSG(out.ok, "resolver accepts the request");
  return PermissionsOf(out);
}

constexpr const char* kAllAsk = "geolocation=kAsk camera=kAsk microphone=kAsk notifications=kAsk";
constexpr const char* kAllDeny = "geolocation=kDeny camera=kDeny microphone=kDeny notifications=kDeny";

GrantRequest G(const char* identity, const char* cap, const char* scope, const char* session = "") {
  GrantRequest r;
  r.identity = identity;
  r.domain = "example.com";
  r.capability = cap;
  r.scope = scope;
  r.session_id = session;
  return r;
}

}  // namespace

int main() {
  // (A) the vector file, through the real parser and resolver ---------------
  {
    std::ifstream in(VectorsPath());
    XR_EXPECT_MSG(in.good(), "overlay vectors readable (XR_BROWSER_ROOT set?)");
    std::stringstream ss;
    ss << in.rdbuf();
    JsonParseResult doc = ParseJson(ss.str());
    XR_EXPECT_MSG(doc.ok, "overlay vectors parse as strict JSON");
    const JsonValue* vectors = doc.value.find("vectors");
    const JsonValue* count = doc.value.find("count");
    if (vectors == nullptr || !vectors->is_array()) {
      XR_EXPECT_MSG(false, "vectors array present");
      return xrtest::Report("test_cross_core");
    }
    XR_EXPECT_MSG(count != nullptr && count->is_int() &&
                      count->as_int() == static_cast<int64_t>(vectors->as_array().size()),
                  "count field equals the vector array length");
    int passed = 0;
    for (const auto& v : vectors->as_array()) {
      const JsonValue* name = v.find("name");
      const JsonValue* request = v.find("request");
      const JsonValue* expected = v.find("expected");
      const std::string label = (name != nullptr && name->is_string()) ? name->as_string() : "?";
      const JsonValue* perms = expected != nullptr ? expected->find("permissions") : nullptr;
      if (request == nullptr || perms == nullptr) {
        XR_EXPECT_MSG(false, "vector has request and permissions: " + label);
        continue;
      }
      // The expected line, in the same field order PermissionsOf prints.
      std::string wantLine;
      bool complete = true;
      for (const char* k : {"geolocation", "camera", "microphone", "notifications"}) {
        const JsonValue* f = perms->find(k);
        if (f == nullptr || !f->is_string()) {
          complete = false;
          break;
        }
        wantLine += std::string(wantLine.empty() ? "" : " ") + k + "=" + f->as_string();
      }
      auto rp = xr::policy::ParseResolveRequest(*request);
      xr::policy::ResolveOutput out = xr::policy::Resolve(rp.request);
      const std::string got = PermissionsOf(out);
      if (complete && out.ok && got == wantLine) {
        ++passed;
      } else {
        XR_EXPECT_MSG(false, "vector " + label + ": want [" + wantLine + "] got [" + got + "]");
      }
    }
    XR_EXPECT_MSG(passed == static_cast<int>(vectors->as_array().size()), "every overlay vector resolves as expected");
    XR_EXPECT_MSG(passed == 33, "33 overlay vectors (update the count with the file)");
  }

  // (B) store -> view -> resolver, across the lifecycle --------------------
  {
    Store s;  // inert
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", kNow, "s1", ProjectViewJson(s)) == kAllAsk,
                  "empty store: the frozen tier table decides (no change)");
    s = SetDefault(s, kA, "camera", "kAllow", kNow).store;
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", kNow, "s1", ProjectViewJson(s)) ==
                      "geolocation=kAsk camera=kAllow microphone=kAsk notifications=kAsk",
                  "identity A's camera default is visible to A");
    XR_EXPECT_MSG(ResolvedPerms(kB, "example.com", kNow, "s1", ProjectViewJson(s)) == kAllAsk,
                  "identity A's camera default never reaches identity B");

    s = GrantTemp(s, G(kA, "geolocation", "7d"), kNow).store;
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", kNow + 1, "s1", ProjectViewJson(s)).find("geolocation=kAllow") !=
                      std::string::npos,
                  "a live 7d grant allows for A on the granted site");
    XR_EXPECT_MSG(ResolvedPerms(kA, "other.example", kNow + 1, "s1", ProjectViewJson(s)).find("geolocation=kAllow") ==
                      std::string::npos,
                  "a 7d grant does not leak to another site");
    XR_EXPECT_MSG(ResolvedPerms(kB, "example.com", kNow + 1, "s1", ProjectViewJson(s)).find("geolocation=kAllow") ==
                      std::string::npos,
                  "a 7d grant does not leak to another identity");
    const int64_t expires = kNow + kDay7;
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", expires, "s1", ProjectViewJson(s)).find("geolocation=kAllow") ==
                      std::string::npos,
                  "at the exact expiry instant the grant is inactive (fail-closed at ==)");
    OpOutcome swept = SweepExpired(s, expires, "s1");
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", expires, "s1", ProjectViewJson(swept.store)) ==
                      "geolocation=kAsk camera=kAllow microphone=kAsk notifications=kAsk",
                  "after the sweep the grant is gone and A's camera default (not a grant) remains");

    s = SetDenied(s, kA, true, kNow).store;
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", kNow + 1, "s1", ProjectViewJson(s)) == kAllDeny,
                  "a Fortress-denied identity resolves to deny on all four");
    XR_EXPECT_MSG(ResolvedPerms(kB, "example.com", kNow + 1, "s1", ProjectViewJson(s)) == kAllAsk,
                  "the deny-list is identity-scoped");

    Store spent = GrantTemp(Store{}, G(kA, "camera", "once"), kNow).store;
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", kNow, "s1", ProjectViewJson(spent)).find("camera=kAllow") != std::string::npos,
                  "an unspent once grant allows");
    OpOutcome used = ConsumeOnce(spent, "g1", kNow + 1);
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", kNow + 2, "s1", ProjectViewJson(used.store)).find("camera=kAllow") ==
                      std::string::npos,
                  "a spent once grant no longer allows");

    Store rev = GrantTemp(SetDefault(Store{}, kA, "camera", "kAllow", kNow).store, G(kA, "geolocation", "7d"), kNow).store;
    rev = RevokeAll(rev, kA, kNow + 3).store;
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", kNow + 4, "s1", ProjectViewJson(rev)) == kAllAsk,
                  "revoke-all returns A to the tier table");
  }

  // (C) corruption is fail-closed end to end ------------------------------
  {
    Store corrupt = LoadStore("not a store");
    XR_EXPECT_MSG(corrupt.corrupt, "fixture: corrupt store");
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", kNow, "s1", ProjectViewJson(corrupt)) == kAllDeny,
                  "a corrupt store denies identity A");
    XR_EXPECT_MSG(ResolvedPerms(kB, "example.com", kNow, "s1", ProjectViewJson(corrupt)) == kAllDeny,
                  "a corrupt store denies every identity (fully-denying, not partial)");
    Store reloaded = LoadStore(SerializeStore(corrupt));
    XR_EXPECT_MSG(ResolvedPerms(kA, "example.com", kNow, "s1", ProjectViewJson(reloaded)) == kAllDeny,
                  "corruption survives a save and reload");
  }

  return xrtest::Report("test_cross_core");
}
