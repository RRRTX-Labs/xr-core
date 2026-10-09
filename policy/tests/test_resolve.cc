// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — resolver unit matrix beyond the golden vectors: the P6
// layers (bindings, exceptions with expiry-at-input semantics, enterprise
// precedence, extension hints) and their fail-closed edges. The frozen core
// is covered by test_vectors.cc (byte-parity).
#include <string>

#include "policy/core/resolve.h"
#include "harness.h"

using namespace xr::policy;

namespace {

ResolveOutput R(const std::string& json) {
  auto p = ParseJson(json);
  XR_EXPECT_MSG(p.ok, "test bug: bad request JSON");
  RequestParse rp = ParseResolveRequest(p.value);
  return Resolve(rp.request);
}

const char* kStd = "xr:00000000-0000-4000-8000-000000000001";
const char* kFort = "xr:00000000-0000-4000-8000-000000000002";

std::string Req(const std::string& layers) {
  return std::string("{\"identity\":{\"value\":\"") + kStd +
         "\"},\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"example.com\"},"
         "\"request_class\":\"kNavigation\"" +
         (layers.empty() ? std::string() : std::string(",") + layers) + "}";
}

}  // namespace

int main() {
  // ---- layer inertness: vectors pass with empty layers (parity bedrock) ----
  {
    auto out = R(Req(""));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == false);  // Standard tier
    XR_EXPECT(out.policy.route == RouteClass::kDirect);
  }

  // ---- trust bindings: site default, per-identity ⌥ override ----
  {
    auto out = R(Req("\"bindings\":[{\"domain\":\"example.com\",\"identity\":\"\","
                     "\"trust\":\"kShield\",\"created_at\":1}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.route == RouteClass::kProxy);  // Shield via binding
  }
  {
    // Identity-specific binding wins over site default.
    auto out = R(Req("\"bindings\":["
                     "{\"domain\":\"example.com\",\"identity\":\"\",\"trust\":\"kShield\",\"created_at\":1},"
                     "{\"domain\":\"example.com\",\"identity\":\"" + std::string(kStd) +
                     "\",\"trust\":\"kFortress\",\"created_at\":2}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == true);  // Fortress via ⌥ override
  }
  {
    // Other-identity binding does not apply.
    auto out = R(Req("\"bindings\":[{\"domain\":\"example.com\",\"identity\":\"" +
                     std::string(kFort) + "\",\"trust\":\"kFortress\",\"created_at\":1}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == false);
  }
  {
    // Explicit request trust beats bindings.
    auto out = R(Req("\"trust_context\":\"kStandard\",\"bindings\":[{\"domain\":\"example.com\","
                     "\"identity\":\"\",\"trust\":\"kFortress\",\"created_at\":1}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == false);
    XR_EXPECT(out.policy.route == RouteClass::kDirect);
  }

  // ---- exceptions: scope semantics (expiry evaluated at INPUT) ----
  {
    // 7d active (expires in the future relative to caller-supplied now).
    auto out = R(Req("\"now_ms\":1000,\"exceptions\":[{\"id\":\"e1\",\"domain\":\"example.com\","
                     "\"identity\":\"\",\"scope\":\"7d\",\"trust\":\"kFortress\",\"created_at\":1,"
                     "\"expires_at\":2000}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == true);
  }
  {
    // 7d EXPIRED: expires_at == now_ms => inactive (fail-closed at ==).
    auto out = R(Req("\"now_ms\":2000,\"exceptions\":[{\"id\":\"e1\",\"domain\":\"example.com\","
                     "\"identity\":\"\",\"scope\":\"7d\",\"trust\":\"kFortress\",\"created_at\":1,"
                     "\"expires_at\":2000}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == false);
  }
  {
    // session: matching session_id => active.
    auto out = R(Req("\"session_id\":\"s1\",\"exceptions\":[{\"id\":\"e1\",\"domain\":\"example.com\","
                     "\"identity\":\"\",\"scope\":\"session\",\"trust\":\"kShield\",\"created_at\":1,"
                     "\"session_id\":\"s1\"}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.route == RouteClass::kProxy);
  }
  {
    // session: different session => inactive.
    auto out = R(Req("\"session_id\":\"s2\",\"exceptions\":[{\"id\":\"e1\",\"domain\":\"example.com\","
                     "\"identity\":\"\",\"scope\":\"session\",\"trust\":\"kShield\",\"created_at\":1,"
                     "\"session_id\":\"s1\"}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.route == RouteClass::kDirect);
  }
  {
    // once: remaining_uses > 0 => active.
    auto out = R(Req("\"exceptions\":[{\"id\":\"e1\",\"domain\":\"example.com\",\"identity\":\"\","
                     "\"scope\":\"once\",\"trust\":\"kShield\",\"created_at\":1,\"remaining_uses\":1}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.route == RouteClass::kProxy);
  }
  {
    // once: consumed (0 uses) => inactive.
    auto out = R(Req("\"exceptions\":[{\"id\":\"e1\",\"domain\":\"example.com\",\"identity\":\"\","
                     "\"scope\":\"once\",\"trust\":\"kShield\",\"created_at\":1,\"remaining_uses\":0}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.route == RouteClass::kDirect);
  }
  {
    // permanent honored ONLY from settings (Plan: permanent lives solely in
    // Settings; in-flow grants are never permanent).
    auto out = R(Req("\"exceptions\":[{\"id\":\"e1\",\"domain\":\"example.com\",\"identity\":\"\","
                     "\"scope\":\"permanent\",\"trust\":\"kFortress\",\"created_at\":1,"
                     "\"granted_by\":\"settings\"}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == true);
  }
  {
    auto out = R(Req("\"exceptions\":[{\"id\":\"e1\",\"domain\":\"example.com\",\"identity\":\"\","
                     "\"scope\":\"permanent\",\"trust\":\"kFortress\",\"created_at\":1,"
                     "\"granted_by\":\"in_flow\"}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == false);  // ignored, never widened
  }
  {
    // Exception beats binding (in-flow grant is more recent intent).
    auto out = R(Req("\"bindings\":[{\"domain\":\"example.com\",\"identity\":\"\","
                     "\"trust\":\"kFortress\",\"created_at\":1}],"
                     "\"exceptions\":[{\"id\":\"e1\",\"domain\":\"example.com\",\"identity\":\"\","
                     "\"scope\":\"7d\",\"trust\":\"kStandard\",\"created_at\":5,"
                     "\"expires_at\":99999}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == false);  // exception downgraded to Standard
    XR_EXPECT(out.policy.route == RouteClass::kDirect);
  }
  {
    // Deterministic winner: higher created_at wins; tie => greater id.
    auto out = R(Req("\"exceptions\":["
                     "{\"id\":\"a\",\"domain\":\"example.com\",\"identity\":\"\",\"scope\":\"7d\","
                     "\"trust\":\"kFortress\",\"created_at\":5,\"expires_at\":99999},"
                     "{\"id\":\"b\",\"domain\":\"example.com\",\"identity\":\"\",\"scope\":\"7d\","
                     "\"trust\":\"kShield\",\"created_at\":9,\"expires_at\":99999}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.route == RouteClass::kProxy);  // b (created_at 9) wins
  }

  // ---- enterprise: floor clamps UP only; forces applied; precedence ----
  {
    // Floor above user tier => clamped up.
    auto out = R(Req("\"trust_context\":\"kStandard\",\"enterprise\":{\"present\":true,"
                     "\"trust_floor\":\"kFortress\"}"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == true);  // Fortress floor beat Standard
  }
  {
    // Floor below user tier => no effect (clamp is upward-only).
    auto out = R(Req("\"trust_context\":\"kFortress\",\"enterprise\":{\"present\":true,"
                     "\"trust_floor\":\"kStandard\"}"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == true);  // still Fortress
  }
  {
    // Forced fields apply after tiering.
    auto out = R(Req("\"trust_context\":\"kStandard\",\"enterprise\":{\"present\":true,"
                     "\"force_fingerprint\":\"kStrict\",\"force_letterbox\":true,"
                     "\"force_block_third_party\":true,\"force_route\":\"kTor\"}"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.fingerprint_mode == FingerprintMode::kStrict);
    XR_EXPECT(out.policy.letterbox == true);
    XR_EXPECT(out.policy.block_third_party == true);
    XR_EXPECT(out.policy.route == RouteClass::kTor);
    XR_EXPECT(out.policy.camera == PermissionState::kAsk);  // tier perms intact
  }
  {
    // Enterprise cannot force export_allowed true (schema const_false is a
    // law above enterprise): field absent from the enterprise layer — the
    // resolver has no code path that sets export_allowed true, ever.
    auto out = R(Req("\"trust_context\":\"kFortress\",\"enterprise\":{\"present\":true}"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.export_allowed == false);
  }

  // ---- extension hints: never widen ----
  {
    auto out = R(Req("\"extension\":{\"id\":\"ext-1\",\"capabilities\":[\"tabs\"]}"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.autofill_allowed == false);  // navigation would allow; ext context forces off
  }
  {
    // Malformed extension hint (no id) => dropped, not denied.
    auto out = R(Req("\"extension\":{\"capabilities\":[\"tabs\"]}"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.autofill_allowed == true);  // hint dropped => tier default
  }

  // ---- invalid entries are dropped, core stays total ----
  {
    auto out = R(Req("\"exceptions\":[{\"id\":\"\",\"domain\":\"example.com\",\"scope\":\"7d\","
                     "\"trust\":\"kShield\",\"expires_at\":99999}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.route == RouteClass::kDirect);  // dropped => default tier
  }
  {
    // Corrupt enterprise layer (bad enum) => whole layer ignored.
    auto out = R(Req("\"trust_context\":\"kStandard\",\"enterprise\":{\"present\":true,"
                     "\"trust_floor\":\"kUltra\"}"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.letterbox == false);
  }

  // ---- identity layer: custom identity list replaces fixture set ----
  {
    auto out = R(std::string("{\"identity\":{\"value\":\"xr:custom\"},\"origin\":{\"scheme\":"
                             "\"https\",\"registrable_domain\":\"a.com\"},\"request_class\":"
                             "\"kNavigation\",\"identities\":[{\"value\":\"xr:custom\"}]}"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.storage_scope == StorageScope::kIdentityScoped);  // recognized
  }
  {
    // Identity NOT in the custom list => deny (total).
    auto out = R(std::string("{\"identity\":{\"value\":\"") + kStd +
                 "\"},\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"a.com\"},"
                 "\"request_class\":\"kNavigation\",\"identities\":[{\"value\":\"xr:custom\"}]}");
    XR_EXPECT(out.ok);
    EffectivePolicy deny;  // default == deny
    XR_EXPECT(out.policy == deny);
  }

  // ---- P15 matrix kill: a trust binding with NO identity field is a site default.
  // ParseBinding's optional-identity guard. The frozen vectors always carry the
  // field, so only this request reaches the "absent identity" branch.
  {
    auto out = R(Req("\"bindings\":[{\"domain\":\"example.com\",\"trust\":\"kShield\",\"created_at\":1}]"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.route == RouteClass::kProxy);  // site default applies to every identity
  }

  // ---- P15 matrix kill: an overlay grant never widens another identity. Identity A
  // (kStd) must stay at the tier answer; identity B (kFort), the grant's owner, widens.
  {
    const std::string overlay =
        "\"now_ms\":1000,\"permission_overlay\":{\"grants\":[{\"identity\":\"" + std::string(kFort) +
        "\",\"domain\":\"example.com\",\"capability\":\"geolocation\",\"scope\":\"7d\","
        "\"expires_at\":10000}]}";
    auto a = R(Req(overlay));
    XR_EXPECT(a.ok);
    XR_EXPECT(a.policy.geolocation == PermissionState::kAsk);  // no cross-identity widening
    auto b = R(std::string("{\"identity\":{\"value\":\"") + kFort +
               "\"},\"origin\":{\"scheme\":\"https\",\"registrable_domain\":\"example.com\"},"
               "\"request_class\":\"kNavigation\"," + overlay + "}");
    XR_EXPECT(b.ok);
    XR_EXPECT(b.policy.geolocation == PermissionState::kAllow);  // the owner's grant does apply
  }

  // ---- P15 policy-suite kills. policy/core is judged by THIS suite in the
  // mutation map, so each planted overlay defect must die here, not only in the
  // permissions cross-core suite. The frozen vectors are untouched.

  // (1) The 7d boundary is fail-closed: expires_at == now_ms is INACTIVE.
  {
    const std::string tail =
        "\",\"domain\":\"example.com\",\"capability\":\"geolocation\",\"scope\":\"7d\",\"expires_at\":";
    auto at = R(Req(std::string("\"now_ms\":1000,\"permission_overlay\":{\"grants\":[{\"identity\":\"") +
                    kStd + tail + "1000}]}"));
    XR_EXPECT(at.ok);
    XR_EXPECT(at.policy.geolocation == PermissionState::kAsk);  // equality: tier answer
    auto live = R(Req(std::string("\"now_ms\":1000,\"permission_overlay\":{\"grants\":[{\"identity\":\"") +
                      kStd + tail + "1001}]}"));
    XR_EXPECT(live.ok);
    XR_EXPECT(live.policy.geolocation == PermissionState::kAllow);  // one ms before expiry: live
  }

  // (2) The Fortress deny-list is FINAL: identity A is denied AND holds an active grant.
  {
    const std::string overlay =
        "\"now_ms\":1000,\"permission_overlay\":{\"denied_identities\":[\"" + std::string(kStd) +
        "\"],\"grants\":[{\"identity\":\"" + std::string(kStd) +
        "\",\"domain\":\"example.com\",\"capability\":\"geolocation\",\"scope\":\"7d\","
        "\"expires_at\":10000}]}";
    auto out = R(Req(overlay));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.geolocation == PermissionState::kDeny);  // deny beats the grant
    XR_EXPECT(out.policy.camera == PermissionState::kDeny);
  }

  // (3) A corrupt overlay (unknown key) denies all four, and is never read as absent.
  {
    auto out = R(Req("\"permission_overlay\":{\"grants\":[],\"zzz\":1}"));
    XR_EXPECT(out.ok);
    XR_EXPECT(out.policy.geolocation == PermissionState::kDeny);
    XR_EXPECT(out.policy.camera == PermissionState::kDeny);
    XR_EXPECT(out.policy.microphone == PermissionState::kDeny);
    XR_EXPECT(out.policy.notifications == PermissionState::kDeny);
  }

  // P15 hardening (mutation survivors, research-log-P15 section 10): the
  // overlay parser refuses a non-object, contract version 2, a once-grant with
  // an extra key and a default with an empty identity; it accepts contract
  // version 1, a valid default and the corrupt marker.
  {
    auto parse_overlay = [](const std::string& json, PermissionOverlayView* out) {
      auto p = ParseJson(json);
      XR_EXPECT_MSG(p.ok, "test bug: bad overlay JSON");
      return ParsePermissionOverlay(p.value, out);
    };
    PermissionOverlayView out;
    XR_EXPECT_MSG(!ParsePermissionOverlay(JsonValue(std::string("x")), &out),
                  "non-object overlay refused");
    XR_EXPECT_MSG(parse_overlay("{\"contract_version\":1}", &out),
                  "contract version 1 accepted");
    XR_EXPECT_MSG(!parse_overlay("{\"contract_version\":2}", &out),
                  "contract version 2 refused");
    XR_EXPECT_MSG(parse_overlay("{\"corrupt\":true}", &out) && out.corrupt,
                  "corrupt marker parsed as corrupt");
    XR_EXPECT_MSG(parse_overlay("{\"defaults\":[{\"identity\":\"" + std::string(kStd) +
                  "\",\"capability\":\"camera\",\"state\":\"kAsk\"}]}", &out),
                  "valid default accepted");
    XR_EXPECT_MSG(!parse_overlay("{\"defaults\":[{\"identity\":\"\",\"capability\":\"camera\","
                  "\"state\":\"kAsk\"}]}", &out),
                  "default with an empty identity refused");
    XR_EXPECT_MSG(!parse_overlay("{\"grants\":[{\"identity\":\"" + std::string(kStd) +
                  "\",\"domain\":\"example.com\",\"capability\":\"camera\",\"scope\":\"once\","
                  "\"remaining_uses\":1,\"extra\":1}]}", &out),
                  "once-grant with an extra key refused");
  }
  {
    // Request layers that cannot be parsed are dropped with a ledger reason;
    // a request without them drops nothing.
    auto drops = [](const std::string& json) {
      auto p = ParseJson(json);
      XR_EXPECT_MSG(p.ok, "test bug: bad request JSON");
      return ParseResolveRequest(p.value).dropped.size();
    };
    XR_EXPECT_MSG(drops(Req("")) == 0, "a plain request drops nothing");
    XR_EXPECT_MSG(drops(Req("\"exceptions\":[\"not-an-object\"]")) >= 1,
                  "non-object exception entry dropped");
    XR_EXPECT_MSG(drops(Req("\"enterprise\":{\"force_fingerprint\":5}")) >= 1,
                  "non-string force_fingerprint dropped");
    XR_EXPECT_MSG(drops(Req("\"enterprise\":{\"force_fingerprint\":\"bogus\"}")) >= 1,
                  "unknown fingerprint mode dropped");
  }
  {
    // The kind "fortress" sets the fortress class of a listed identity.
    auto p = ParseJson(Req("\"identities\":[{\"value\":\"" + std::string(kFort) +
                           "\",\"kind\":\"fortress\"}]"));
    XR_EXPECT_MSG(p.ok, "test bug: bad identity request JSON");
    const RequestParse rp = ParseResolveRequest(p.value);
    XR_EXPECT_MSG(rp.request.identities.size() == 1 && rp.request.identities[0].fortress,
                  "fortress kind sets the fortress class");
  }

  return xrtest::Report("test_resolve");
}
