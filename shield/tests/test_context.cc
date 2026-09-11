// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/context strictness (P11-T2) — required fields,
// documented defaults for the living trio (first_party/tab_type/workspace),
// deny-on-unknown everywhere, and the URL splitter's redaction law:
// query/fragment stripped, port stripped, userinfo REFUSED, lowercased.
#include <string>

#include "common/core/json.h"
#include "shield/core/context.h"

#include "harness.h"

using namespace xr::shield;
using xr::common::ParseJson;

namespace {

const char* kValid =
    R"({"identity":{"value":"xr:aaa"},"origin":{"scheme":"https",)"
    R"("registrable_domain":"example.com"},"url":"https://a.example.com/x.js",)"
    R"("request_class":"kScript"})";

ParseResult Parse(const std::string& json, RequestContext* ctx,
                  std::string* detail) {
  auto pr = ParseJson(json);
  if (!pr.ok) {
    *detail = "bad-json";
    return ParseResult::kMalformed;
  }
  return ParseRequestContext(pr.value, ctx, detail);
}

void ExpectRefusal(const std::string& json, ParseResult want,
                   const std::string& detail_frag, const char* what) {
  RequestContext ctx;
  std::string detail;
  ParseResult r = Parse(json, &ctx, &detail);
  XR_EXPECT_MSG(r == want, std::string(what) + " (result)");
  XR_EXPECT_MSG(detail.find(detail_frag) != std::string::npos,
                std::string(what) + " (detail '" + detail + "' contains '" +
                    detail_frag + "')");
}

}  // namespace

int main() {
  // the valid baseline + defaults
  {
    RequestContext ctx;
    std::string detail;
    ParseResult r = Parse(kValid, &ctx, &detail);
    XR_EXPECT_MSG(r == ParseResult::kOk, std::string("valid parses: ") + detail);
    XR_EXPECT(ctx.identity.value == "xr:aaa");
    XR_EXPECT(ctx.origin.scheme == "https");
    XR_EXPECT(ctx.origin.registrable_domain == "example.com");
    XR_EXPECT(ctx.request_class == RequestClass::kScript);
    XR_EXPECT(!ctx.first_party);                        // documented default
    XR_EXPECT(ctx.tab_type == TabType::kNormal);        // documented default
    XR_EXPECT(ctx.workspace.empty());                   // "" = default workspace
    XR_EXPECT(ctx.parts.scheme == "https");
    XR_EXPECT(ctx.parts.host == "a.example.com");
    XR_EXPECT(ctx.parts.path == "/x.js");
  }
  // optional living fields, explicit
  {
    std::string j = std::string(kValid);
    j.pop_back();  // drop }
    j += R"(,"first_party":true,"tab_type":"workspace","workspace":"ws-1"})";
    RequestContext ctx;
    std::string detail;
    ParseResult r = Parse(j, &ctx, &detail);
    XR_EXPECT_MSG(r == ParseResult::kOk, std::string("optionals parse: ") + detail);
    XR_EXPECT(ctx.first_party);
    XR_EXPECT(ctx.tab_type == TabType::kWorkspace);
    XR_EXPECT(ctx.workspace == "ws-1");
  }
  // every frozen request class name round-trips
  for (const char* n : {"kNavigation", "kSubresource", "kScript",
                        "kPermission", "kStorage", "kNetwork"}) {
    std::string j = std::string(kValid);
    j.replace(j.find("kScript"), 7, n);
    RequestContext ctx;
    std::string detail;
    XR_EXPECT_MSG(Parse(j, &ctx, &detail) == ParseResult::kOk,
                  std::string("request class ") + n);
  }
  // missing required fields
  ExpectRefusal(R"({"origin":{"scheme":"https","registrable_domain":"e.com"},)"
                R"("url":"https://e.com/","request_class":"kScript"})",
                ParseResult::kMalformed, "identity-not-object",
                "identity required");
  ExpectRefusal(R"({"identity":{"value":"x"},"url":"https://e.com/",)"
                R"("request_class":"kScript"})",
                ParseResult::kMalformed, "origin-not-object",
                "origin required");
  ExpectRefusal(R"({"identity":{"value":"x"},)"
                R"("origin":{"scheme":"https","registrable_domain":"e.com"},)"
                R"("request_class":"kScript"})",
                ParseResult::kMalformed, "missing-required-field:url",
                "url required");
  // unknown fields — top level and nested
  ExpectRefusal(
      std::string(kValid).substr(0, std::string(kValid).size() - 1) +
          R"(,"incognito":true})",
      ParseResult::kUnknownField, "unknown-field:incognito",
      "unknown top-level field refused");
  ExpectRefusal(R"({"identity":{"value":"x","grade":"kStandard"},)"
                R"("origin":{"scheme":"https","registrable_domain":"e.com"},)"
                R"("url":"https://e.com/","request_class":"kScript"})",
                ParseResult::kUnknownField, "unknown-field:identity.grade",
                "unknown identity field refused");
  // unknown enum values
  ExpectRefusal(
      std::string(kValid).substr(0, std::string(kValid).size() - 1) +
          R"(,"tab_type":"guest"})",
      ParseResult::kUnknownValue, "unknown-tab-type", "unknown tab_type");
  {
    std::string j = std::string(kValid);
    j.replace(j.find("kScript"), 7, "kFetch");
    ExpectRefusal(j, ParseResult::kUnknownValue, "unknown-request-class",
                  "unknown request_class");
  }
  // origin scheme law (http|https only — frozen fake law)
  ExpectRefusal(R"({"identity":{"value":"x"},"origin":{"scheme":"ftp",)"
                R"("registrable_domain":"e.com"},"url":"https://e.com/",)"
                R"("request_class":"kScript"})",
                ParseResult::kUnknownValue, "unknown-origin-scheme",
                "ftp origin scheme refused");
  ExpectRefusal(R"({"identity":{"value":"x"},"origin":{"scheme":"https",)"
                R"("registrable_domain":""},"url":"https://e.com/",)"
                R"("request_class":"kScript"})",
                ParseResult::kUnknownValue, "empty-registrable-domain",
                "empty registrable_domain refused");
  ExpectRefusal(R"({"identity":{"value":""},"origin":{"scheme":"https",)"
                R"("registrable_domain":"e.com"},"url":"https://e.com/",)"
                R"("request_class":"kScript"})",
                ParseResult::kUnknownValue, "empty-identity",
                "empty identity refused");
  // URL splitter refusals
  for (const char* u : {"ftp://e.com/x", "https://user:pw@e.com/x",
                        "not-a-url", "https://", "/relative/path"}) {
    std::string j = std::string(kValid);
    const std::string target = "https://a.example.com/x.js";
    j.replace(j.find(target), target.size(), u);
    RequestContext ctx;
    std::string detail;
    ParseResult r = Parse(j, &ctx, &detail);
    XR_EXPECT_MSG(r == ParseResult::kUnknownValue && detail == "unparsable-url",
                  std::string("url refused: ") + u + " (" + detail + ")");
  }
  // SplitUrl redaction + normalization law
  {
    UrlParts p;
    XR_EXPECT(SplitUrl("HTTPS://A.Example.COM:8080/x/Y.JS?uid=SECRET#frag", &p));
    XR_EXPECT(p.scheme == "https");
    XR_EXPECT(p.host == "a.example.com");     // lowercased, port stripped
    XR_EXPECT(p.path == "/x/Y.JS");           // case preserved, query+frag gone
    XR_EXPECT(SplitUrl("https://e.com", &p) && p.path == "/");   // canonical /
    XR_EXPECT(SplitUrl("https://e.com?a=1", &p) && p.path == "/" &&
              p.host == "e.com");  // query never leaks into the host
    XR_EXPECT(SplitUrl("https://e.com#f", &p) && p.path == "/" &&
              p.host == "e.com");
    XR_EXPECT(SplitUrl("https://e.com:8080?a=1", &p) && p.host == "e.com" &&
              p.path == "/");
    XR_EXPECT(!SplitUrl("https://e.com:@x", &p));  // userinfo refused
    XR_EXPECT(!SplitUrl("https://e.com:notaport/", &p));
  }
  // canonical echo round-trip (vectors pin these bytes)
  {
    RequestContext ctx;
    std::string detail;
    XR_EXPECT(Parse(kValid, &ctx, &detail) == ParseResult::kOk);
    std::string c = ContextToJson(ctx).Canonical();
    XR_EXPECT(c.find("\"tab_type\":\"normal\"") != std::string::npos);
    XR_EXPECT(c.find("\"workspace\":\"\"") != std::string::npos);
    XR_EXPECT(c.find("\"request_class\":\"kScript\"") != std::string::npos);
    auto pr = ParseJson(c);
    XR_EXPECT(pr.ok);
    RequestContext ctx2;
    XR_EXPECT(ParseRequestContext(pr.value, &ctx2, &detail) == ParseResult::kOk);
    XR_EXPECT(ContextToJson(ctx2).Canonical() == c);  // idempotent echo
  }
  return xrtest::Report("test_context");
}
