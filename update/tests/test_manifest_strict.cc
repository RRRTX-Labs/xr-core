// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: strict envelope/manifest parse matrix (P10-T1). Deny-on-unknown
// at EVERY level, canonical version law, digest/url laws, oversize, the
// )]}' safe prefix, and the recomputed manifest_id.
#include "update/core/manifest.h"

#include "harness.h"

#include "update/tests/env_helper.h"

using namespace xr::update;
using xrtest_update::BaseResponse;
using xrtest_update::Envelope;

int main() {
  UpdateEnvelope env;
  // accept path + recomputed manifest_id
  const std::string good = Envelope(BaseResponse("1.2.0.0"));
  XR_EXPECT_MSG(ParseEnvelope(good, &env) == ParseResult::kOk, "good envelope parses");
  XR_EXPECT_MSG(env.manifest_id ==
            Sha256Hex(xr::update::JsonValue(
                          xr::update::ParseJson(good).value.find("response")->as_object()
                      ).Canonical()),
            "manifest_id = sha256(canonical response)");
  XR_EXPECT_MSG(env.version.ToString() == "1.2.0.0", "version round-trips");
  XR_EXPECT_MSG(env.package_hash_sha256 == std::string(64, 'a'), "digest carried");
  XR_EXPECT_MSG(env.codebase == "https://updates.example/xr/", "codebase carried");

  // safe prefix
  XR_EXPECT_MSG(ParseEnvelope(")]}'\n" + good, &env) == ParseResult::kOk,
            ")]}' prefix stripped");
  // wrong prefix-ish leading junk is malformed
  XR_EXPECT_MSG(ParseEnvelope(" ])}'\n" + good, &env) == ParseResult::kMalformed,
            "junk prefix malformed");

  // unknown field at every level
  {
    auto doc = xr::update::ParseJson(good).value;
    auto obj = doc.as_object();
    obj["xrsig"] = JsonValue("z");
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kUnknownField, "unknown envelope field denied");
  }
  {
    auto doc = xr::update::ParseJson(good).value;
    auto resp = doc.find("response")->as_object();
    resp["extra"] = JsonValue(static_cast<int64_t>(1));
    auto obj = doc.as_object();
    obj["response"] = JsonValue(resp);
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kUnknownField, "unknown response field denied");
  }
  {
    auto doc = xr::update::ParseJson(good).value;
    auto resp = doc.find("response")->as_object();
    auto app0 = resp.at("app").as_array()[0].as_object();
    app0["tag"] = JsonValue("x");
    resp["app"] = JsonValue(JsonValue::Array{JsonValue(app0)});
    auto obj = doc.as_object();
    obj["response"] = JsonValue(resp);
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kUnknownField, "unknown app field denied");
  }
  {
    auto doc = xr::update::ParseJson(good).value;
    auto resp = doc.find("response")->as_object();
    auto app0 = resp.at("app").as_array()[0].as_object();
    auto uc = app0.at("updatecheck").as_object();
    uc["ttl"] = JsonValue(static_cast<int64_t>(5));
    app0["updatecheck"] = JsonValue(uc);
    resp["app"] = JsonValue(JsonValue::Array{JsonValue(app0)});
    auto obj = doc.as_object();
    obj["response"] = JsonValue(resp);
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kUnknownField, "unknown updatecheck field denied");
  }
  {
    auto doc = xr::update::ParseJson(good).value;
    auto resp = doc.find("response")->as_object();
    auto app0 = resp.at("app").as_array()[0].as_object();
    auto uc = app0.at("updatecheck").as_object();
    auto man = uc.at("manifest").as_object();
    man["restrict"] = JsonValue("x");
    uc["manifest"] = JsonValue(man);
    app0["updatecheck"] = JsonValue(uc);
    resp["app"] = JsonValue(JsonValue::Array{JsonValue(app0)});
    auto obj = doc.as_object();
    obj["response"] = JsonValue(resp);
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kUnknownField, "unknown manifest field denied");
  }
  {
    auto doc = xr::update::ParseJson(good).value;
    auto ep = doc.find("epoch")->as_object();
    ep["note"] = JsonValue("x");
    auto obj = doc.as_object();
    obj["epoch"] = JsonValue(ep);
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kUnknownField, "unknown epoch field denied");
  }

  // protocol law
  {
    auto doc = xr::update::ParseJson(good).value;
    auto resp = doc.find("response")->as_object();
    resp["protocol"] = JsonValue("3.0");
    auto obj = doc.as_object();
    obj["response"] = JsonValue(resp);
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kWrongProtocol, "wrong protocol denied");
  }

  // version law
  const char* bad_versions[] = {"1.2.0", "01.2.0.0", "1.2.0.0.0", "1..2.0",
                                "one.two", "4294967296.0.0.0", ""};
  for (const char* bv : bad_versions) {
    auto doc = xr::update::ParseJson(good).value;
    auto resp = doc.find("response")->as_object();
    auto app0 = resp.at("app").as_array()[0].as_object();
    auto uc = app0.at("updatecheck").as_object();
    auto man = uc.at("manifest").as_object();
    man["version"] = JsonValue(std::string(bv));
    uc["manifest"] = JsonValue(man);
    app0["updatecheck"] = JsonValue(uc);
    resp["app"] = JsonValue(JsonValue::Array{JsonValue(app0)});
    auto obj = doc.as_object();
    obj["response"] = JsonValue(resp);
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kBadVersion, std::string("bad version: ") + bv);
  }

  // digest / url laws
  {
    auto doc = xr::update::ParseJson(good).value;
    auto resp = doc.find("response")->as_object();
    auto app0 = resp.at("app").as_array()[0].as_object();
    auto uc = app0.at("updatecheck").as_object();
    auto man = uc.at("manifest").as_object();
    auto pkg0 = man.at("packages").as_object()
                    .at("package").as_array()[0].as_object();
    pkg0["hash_sha256"] = JsonValue("ab");
    man["packages"] = JsonValue(JsonValue::Object{
        {"package", JsonValue(JsonValue::Array{pkg0})}});
    uc["manifest"] = JsonValue(man);
    app0["updatecheck"] = JsonValue(uc);
    resp["app"] = JsonValue(JsonValue::Array{JsonValue(app0)});
    auto obj = doc.as_object();
    obj["response"] = JsonValue(resp);
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kBadDigest, "short digest denied");
  }
  {
    auto doc = xr::update::ParseJson(good).value;
    auto resp = doc.find("response")->as_object();
    auto app0 = resp.at("app").as_array()[0].as_object();
    auto uc = app0.at("updatecheck").as_object();
    auto urls = uc.at("urls").as_object();
    urls["url"] = JsonValue(JsonValue::Array{JsonValue(JsonValue::Object{
        {"codebase", JsonValue(std::string("http://updates.example/xr/"))}})});
    uc["urls"] = JsonValue(urls);
    app0["updatecheck"] = JsonValue(uc);
    resp["app"] = JsonValue(JsonValue::Array{JsonValue(app0)});
    auto obj = doc.as_object();
    obj["response"] = JsonValue(resp);
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kInsecureUrl, "http codebase denied");
  }

  // oversize + malformed + two-app profile
  XR_EXPECT_MSG(ParseEnvelope(std::string(65 * 1024, 'x'), &env) ==
            ParseResult::kOversize, "oversize denied");
  XR_EXPECT_MSG(ParseEnvelope("{not json", &env) == ParseResult::kMalformed,
            "malformed denied");
  {
    auto doc = xr::update::ParseJson(good).value;
    auto resp = doc.find("response")->as_object();
    auto app0 = resp.at("app").as_array()[0];
    resp["app"] = JsonValue(JsonValue::Array{app0, app0});
    auto obj = doc.as_object();
    obj["response"] = JsonValue(resp);
    XR_EXPECT_MSG(ParseEnvelope(JsonValue(obj).Canonical(), &env) ==
              ParseResult::kMalformed, "two-app profile denied");
  }

  // Version ordering laws
  Version a, b;
  XR_EXPECT_MSG(Version::Parse("1.0.0.0", &a) && Version::Parse("1.0.0.1", &b) && a < b,
            "1.0.0.0 < 1.0.0.1");
  XR_EXPECT_MSG(Version::Parse("2.0.0.0", &a) && Version::Parse("1.9.9.9", &b) && b < a,
            "1.9.9.9 < 2.0.0.0");
  XR_EXPECT_MSG(!(Version::Parse("1.0.0.0", &a) && Version::Parse("1.0.0", &b)),
            "three-part version refused");

  return xrtest::Report("test_manifest_strict");
}
