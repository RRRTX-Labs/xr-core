// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/scope (P11-T2, mechanics extended by T4) — strict
// parse, the resolver-coupling law (every SET dimension must match
// exactly: identity A ≠ identity B, site A ≠ site B, workspace A ≠ B),
// and the deterministic expiry sweep (boundary-inclusive, input order).
#include <string>

#include "common/core/json.h"
#include "shield/core/context.h"
#include "shield/core/engine.h"
#include "shield/core/scope.h"

#include "harness.h"

using namespace xr::shield;
using xr::common::ParseJson;

namespace {

ScopeResult Parse(const std::string& json, ScopeSet* set, std::string* d) {
  auto pr = ParseJson(json);
  if (!pr.ok) {
    *d = "bad-json";
    return ScopeResult::kMalformed;
  }
  return ParseScopeSet(pr.value, set, d);
}

RequestContext Ctx(const std::string& identity, const std::string& domain,
                   const std::string& workspace) {
  RequestContext c;
  c.identity.value = identity;
  c.origin.scheme = "https";
  c.origin.registrable_domain = domain;
  c.workspace = workspace;
  return c;
}

EngineHit Hit(const std::string& rule, const std::string& list) {
  EngineHit h;
  h.action = Action::kBlock;
  h.rule_id = rule;
  h.list_id = list;
  return h;
}

ExceptionScope Scope(const std::string& id, const std::string& identity,
                     const std::string& site, const std::string& workspace,
                     const std::string& rule, const std::string& list,
                     long long expiry) {
  ExceptionScope s;
  s.scope_id = id;
  s.identity = identity;
  s.site = site;
  s.workspace = workspace;
  s.rule_id = rule;
  s.list_id = list;
  s.expiry_mono = expiry;
  s.reason = "test";
  return s;
}

}  // namespace

int main() {
  // parse: valid + defaults
  {
    ScopeSet set;
    std::string d;
    ScopeResult r = Parse(R"({"scopes":[{"scope_id":"s1","identity":"xr:a",)"
                          R"("site":"example.com","rule_id":"r1",)"
                          R"("expiry_mono":500,"reason":"user"}]})", &set, &d);
    XR_EXPECT_MSG(r == ScopeResult::kOk, std::string("valid scope set: ") + d);
    XR_EXPECT(set.scopes.size() == 1);
    XR_EXPECT(set.scopes[0].workspace.empty());   // unset = any
    XR_EXPECT(set.scopes[0].list_id.empty());     // unset = any
    XR_EXPECT(set.scopes[0].expiry_mono == 500);
  }
  // parse: strictness tokens
  struct { const char* json; ScopeResult want; const char* frag; } cases[] = {
      {R"({"scopes":"x"})", ScopeResult::kMalformed, "scopes-not-array"},
      {R"({"scopes":[],"x":1})", ScopeResult::kUnknownField, "unknown-field:x"},
      {R"({"scopes":[1]})", ScopeResult::kMalformed, "scope-not-object"},
      {R"({"scopes":[{"identity":"a","reason":"r"}]})", ScopeResult::kMalformed,
       "bad-scope-id"},
      {R"({"scopes":[{"scope_id":"","reason":"r"}]})", ScopeResult::kMalformed,
       "bad-scope-id"},
      {R"({"scopes":[{"scope_id":"s","scope_id2":"x","reason":"r"}]})",
       ScopeResult::kUnknownField, "unknown-field:scope_id2"},
      {R"({"scopes":[{"scope_id":"s","identity":5,"reason":"r"}]})",
       ScopeResult::kMalformed, "field-not-string:identity"},
      {R"({"scopes":[{"scope_id":"s","expiry_mono":"500","reason":"r"}]})",
       ScopeResult::kMalformed, "bad-expiry"},
      {R"({"scopes":[{"scope_id":"s","expiry_mono":-2,"reason":"r"}]})",
       ScopeResult::kMalformed, "bad-expiry"},
      {R"({"scopes":[{"scope_id":"s"}]})", ScopeResult::kMissingReason,
       "missing-reason:s"},
      {R"({"scopes":[{"scope_id":"s","reason":""}]})", ScopeResult::kMissingReason,
       "missing-reason:s"},
      {R"({"scopes":[{"scope_id":"s","reason":"a"},{"scope_id":"s","reason":"b"}]})",
       ScopeResult::kDuplicateScopeId, "duplicate-scope-id:s"},
  };
  for (const auto& c : cases) {
    ScopeSet set;
    std::string d;
    ScopeResult r = Parse(c.json, &set, &d);
    XR_EXPECT_MSG(r == c.want && d.find(c.frag) != std::string::npos,
                  std::string("scope refusal ") + c.frag + " (got " + d + ")");
  }
  // expiry -1 = never expires; -1 parses (>= -1 law)
  {
    ScopeSet set;
    std::string d;
    XR_EXPECT(Parse(R"({"scopes":[{"scope_id":"s","expiry_mono":-1,"reason":"r"}]})",
                    &set, &d) == ScopeResult::kOk);
    XR_EXPECT(set.scopes[0].expiry_mono == -1);
  }

  // the coupling law, dimension by dimension
  {
    EngineHit h = Hit("r1", "l1");
    // identity binding
    ExceptionScope s = Scope("s", "xr:a", "", "", "", "", -1);
    XR_EXPECT(Covers(s, Ctx("xr:a", "e.com", ""), h, 0));
    XR_EXPECT(!Covers(s, Ctx("xr:b", "e.com", ""), h, 0));  // A ≠ B
    // site binding
    s = Scope("s", "", "e.com", "", "", "", -1);
    XR_EXPECT(Covers(s, Ctx("xr:a", "e.com", ""), h, 0));
    XR_EXPECT(!Covers(s, Ctx("xr:a", "other.com", ""), h, 0));
    XR_EXPECT(!Covers(s, Ctx("xr:a", "sub.e.com", ""), h, 0));  // exact, no inheritance
    // workspace binding
    s = Scope("s", "", "", "ws1", "", "", -1);
    XR_EXPECT(Covers(s, Ctx("xr:a", "e.com", "ws1"), h, 0));
    XR_EXPECT(!Covers(s, Ctx("xr:a", "e.com", "ws2"), h, 0));
    XR_EXPECT(!Covers(s, Ctx("xr:a", "e.com", ""), h, 0));  // default ≠ ws1
    // rule/list binding
    s = Scope("s", "", "", "", "r1", "", -1);
    XR_EXPECT(Covers(s, Ctx("xr:a", "e.com", ""), h, 0));
    XR_EXPECT(!Covers(s, Ctx("xr:a", "e.com", ""), Hit("r2", "l1"), 0));
    s = Scope("s", "", "", "", "", "l1", -1);
    XR_EXPECT(!Covers(s, Ctx("xr:a", "e.com", ""), Hit("r1", "l2"), 0));
    // all-empty scope covers everything (the "disable shield here" scope)
    s = Scope("s", "", "", "", "", "", -1);
    XR_EXPECT(Covers(s, Ctx("xr:zzz", "anywhere.test", "ws9"), Hit("rx", "lx"), 12345));
    // multi-dimension: ALL set dimensions must match
    s = Scope("s", "xr:a", "e.com", "ws1", "r1", "l1", -1);
    XR_EXPECT(Covers(s, Ctx("xr:a", "e.com", "ws1"), h, 0));
    XR_EXPECT(!Covers(s, Ctx("xr:a", "e.com", "ws2"), h, 0));
    XR_EXPECT(!Covers(s, Ctx("xr:b", "e.com", "ws1"), h, 0));
  }
  // expiry boundary: inclusive at now == expiry
  {
    EngineHit h = Hit("r1", "l1");
    ExceptionScope s = Scope("s", "", "", "", "", "", 500);
    XR_EXPECT(Covers(s, Ctx("a", "e.com", ""), h, 499));
    XR_EXPECT(!Covers(s, Ctx("a", "e.com", ""), h, 500));  // expires AT boundary
    XR_EXPECT(!Covers(s, Ctx("a", "e.com", ""), h, 501));
  }
  // sweep: deterministic, input order, boundary-inclusive
  {
    ScopeSet set;
    set.scopes = {Scope("keep1", "", "", "", "", "", -1),
                  Scope("gone", "", "", "", "", "", 100),
                  Scope("keep2", "", "", "", "", "", 300),
                  Scope("edge", "", "", "", "", "", 200)};
    SweepResult r = SweepAsOf(set, 200);
    XR_EXPECT(r.active_ids.size() == 2 && r.active_ids[0] == "keep1" &&
              r.active_ids[1] == "keep2");
    XR_EXPECT(r.expired_ids.size() == 2 && r.expired_ids[0] == "gone" &&
              r.expired_ids[1] == "edge");  // boundary-inclusive
    SweepResult r2 = SweepAsOf(set, 99);
    XR_EXPECT(r2.expired_ids.empty() && r2.active_ids.size() == 4);
  }
  // canonical round-trip
  {
    ScopeSet set;
    set.scopes = {Scope("s1", "xr:a", "e.com", "", "r1", "", 500)};
    std::string c = ScopeSetToJson(set).Canonical();
    ScopeSet back;
    std::string d;
    auto pr = ParseJson(c);
    XR_EXPECT(pr.ok);
    XR_EXPECT_MSG(ParseScopeSet(pr.value, &back, &d) == ScopeResult::kOk, d);
    XR_EXPECT(ScopeSetToJson(back).Canonical() == c);  // idempotent
    XR_EXPECT(c.find("\"reason\":\"test\"") != std::string::npos);
  }
  return xrtest::Report("test_scope");
}
