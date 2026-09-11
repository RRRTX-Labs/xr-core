// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/match + the TableEngine fake (P11-T2) — the
// decision ORDER is law: posture first (fail-closed short-circuits to
// block, fail-open to allow with the posture reason as why_code), then
// bundle presence, then the engine, then exception scopes. The v1 filter
// matcher semantics are pinned here branch by branch (the Python fake
// mirrors them; the golden vectors pin the pair).
#include <string>

#include "shield/core/bundle.h"
#include "shield/core/context.h"
#include "shield/core/fake_engine.h"
#include "shield/core/match.h"
#include "shield/core/posture.h"
#include "shield/core/scope.h"

#include "harness.h"

using namespace xr::shield;

namespace {

NormalizedBundle TestBundle() {
  NormalizedBundle b;
  b.name = "xr-default";
  b.bundle_version = 3;
  b.digest = std::string(64, 'a');
  BundleList l;
  l.name = "l1";
  auto rule = [&](const char* id, const char* filter, Action act,
                  const char* kind = "network") {
    Rule r;
    r.id = id;
    r.kind = kind;
    r.filter = filter;
    r.action = act;
    std::string d;
    XR_EXPECT(ParseFilter(filter, &r.parsed, &d));
    return r;
  };
  l.rules.push_back(rule("r-block", "||tracker.example^", Action::kBlock));
  l.rules.push_back(
      rule("r-allow", "||tracker.example^ok.js", Action::kAllow));
  l.rules.push_back(rule("r-redir", "||ads.example/banner", Action::kRedirect,
                         "redirect"));
  l.rules.back().resource = "1x1.gif";
  Rule scoped = rule("r-scoped", "||scoped.example^", Action::kBlock);
  scoped.domains.push_back("site.example");
  l.rules.push_back(scoped);
  Rule cosmetic = rule("r-cosmetic", "tracker.example", Action::kBlock,
                       "cosmetic");
  l.rules.push_back(cosmetic);
  b.lists.push_back(l);
  return b;
}

RequestContext Ctx(const std::string& url, const std::string& domain,
                   const std::string& identity = "xr:a") {
  RequestContext c;
  c.identity.value = identity;
  c.origin.scheme = url.rfind("https", 0) == 0 ? "https" : "http";
  c.origin.registrable_domain = domain;
  c.url = url;
  XR_EXPECT(SplitUrl(url, &c.parts));
  c.request_class = RequestClass::kSubresource;
  return c;
}

PostureInputs Healthy() { return PostureInputs{}; }

Verdict Decide(const RequestContext& c, const NormalizedBundle* b,
               BlockingEngine* e, const ScopeSet& s = ScopeSet{},
               long long now = 0, PostureInputs in = Healthy(),
               bool* fail_closed = nullptr) {
  MatchOutcome o = DecideMatch(c, b, e, s, now, in);
  if (fail_closed) *fail_closed = o.fail_closed;
  return o.verdict;
}

}  // namespace

int main() {
  NormalizedBundle b = TestBundle();

  // ---- the v1 matcher, branch by branch ----
  {
    TableEngine e(&b);
    // domain anchor + separator: host, subdomain, boundary law
    XR_EXPECT(Decide(Ctx("https://tracker.example/a.js", "tracker.example"), &b, &e)
                  .why_code == "rule-blocked");
    XR_EXPECT(Decide(Ctx("https://sub.tracker.example/a", "tracker.example"), &b, &e)
                  .why_code == "rule-blocked");  // subdomain covered
    XR_EXPECT(Decide(Ctx("https://tracker.example.com/a", "example.com"), &b, &e)
                  .why_code == "no-match");  // NOT a dot-boundary suffix
    XR_EXPECT(Decide(Ctx("https://nottracker.example/a", "example"), &b, &e)
                  .why_code == "no-match");
    // allow overrides block (both rules match ok.js)
    Verdict v = Decide(Ctx("https://tracker.example/ok.js", "tracker.example"), &b, &e);
    XR_EXPECT(v.why_code == "rule-allowed" && v.rule_id == "r-allow");
    // domain-anchored redirect rule
    XR_EXPECT(Decide(Ctx("https://ads.example/banner", "ads.example"), &b, &e)
                  .why_code == "rule-redirected");
    XR_EXPECT(Decide(Ctx("https://x.com/ads.example/banner", "x.com"), &b, &e)
                  .why_code == "no-match");  // || anchors at the HOST, not substring
    // domains option: include set is exact against origin domain or host
    XR_EXPECT(Decide(Ctx("https://scoped.example/x", "site.example"), &b, &e)
                  .why_code == "rule-blocked");
    XR_EXPECT(Decide(Ctx("https://scoped.example/x", "other.example"), &b, &e)
                  .why_code == "no-match");
    // cosmetic rules never match in the network engine
    XR_EXPECT(Decide(Ctx("https://tracker.example/cosmetic", "tracker.example"),
                     &b, &e)
                  .rule_id != "r-cosmetic");
    // query/fragment redaction: same verdict as the clean URL
    XR_EXPECT(Decide(Ctx("https://tracker.example/a.js?uid=SECRET#f",
                         "tracker.example"), &b, &e)
                  .why_code == "rule-blocked");
    // port stripped from the match surface
    XR_EXPECT(Decide(Ctx("https://tracker.example:8443/a.js", "tracker.example"),
                     &b, &e)
                  .why_code == "rule-blocked");
  }
  // matcher units: wildcard, right anchor, separator class
  {
    ParsedFilter pf;
    std::string d;
    UrlParts p;
    XR_EXPECT(ParseFilter("||cdn.example*/ad_*.js", &pf, &d));
    XR_EXPECT(SplitUrl("https://cdn.example/x/ad_min.js", &p));
    XR_EXPECT(FilterMatchV1(pf, p));
    XR_EXPECT(SplitUrl("https://cdn.example/x/ad_min.css", &p));
    XR_EXPECT(!FilterMatchV1(pf, p));
    XR_EXPECT(ParseFilter("||a.example/exact|", &pf, &d));
    XR_EXPECT(SplitUrl("https://a.example/exact", &p) && FilterMatchV1(pf, p));
    XR_EXPECT(SplitUrl("https://a.example/exact/more", &p) &&
              !FilterMatchV1(pf, p));  // right anchor
    XR_EXPECT(ParseFilter("|https://b.example/", &pf, &d));
    XR_EXPECT(SplitUrl("https://b.example/x", &p) && FilterMatchV1(pf, p));
    XR_EXPECT(SplitUrl("http://b.example/x", &p) && !FilterMatchV1(pf, p));
    // plain literal: unanchored substring of the full match surface
    XR_EXPECT(ParseFilter("ads.js", &pf, &d));
    XR_EXPECT(SplitUrl("https://any.example/x/ads.js", &p) && FilterMatchV1(pf, p));
    XR_EXPECT(SplitUrl("https://any.example/x/ads.css", &p) && !FilterMatchV1(pf, p));
    XR_EXPECT(ParseFilter("||d.example^deep", &pf, &d));
    XR_EXPECT(SplitUrl("https://d.example/deep", &p) && FilterMatchV1(pf, p));
    XR_EXPECT(SplitUrl("https://d.example/xdeep", &p) && !FilterMatchV1(pf, p));
    // "||d|" — the bare-domain filter
    XR_EXPECT(ParseFilter("||d.example|", &pf, &d));
    XR_EXPECT(SplitUrl("https://d.example", &p) && FilterMatchV1(pf, p));
    XR_EXPECT(SplitUrl("https://d.example/x", &p) && !FilterMatchV1(pf, p));
  }
  // ---- decision ORDER: posture beats everything ----
  {
    TableEngine alive(&b);
    bool fc = false;
    // route loss: fail-closed even with kill switch + dead engine + scopes
    PostureInputs lost{true, false, false, true};
    Verdict v = Decide(Ctx("https://clean.example/", "clean.example"), &b,
                       &alive, ScopeSet{}, 0, lost, &fc);
    XR_EXPECT(fc && v.action == Action::kBlock &&
              v.why_code == "route-loss-fail-closed" && !v.engine_decision);
    // engine death: fail-open ALLOW even though the URL would be blocked
    PostureInputs dead{false, false, true, false};
    v = Decide(Ctx("https://tracker.example/a.js", "tracker.example"), &b,
               &alive, ScopeSet{}, 0, dead, &fc);
    XR_EXPECT(!fc && v.action == Action::kAllow &&
              v.why_code == "engine-dead-fail-open");
    // poison outranks kill switch
    PostureInputs poison{true, true, true, true};
    v = Decide(Ctx("https://tracker.example/a.js", "tracker.example"), &b,
               &alive, ScopeSet{}, 0, poison);
    XR_EXPECT(v.why_code == "engine-poisoned-fail-open");
    // kill switch: allowed, why says so
    PostureInputs killed{true, false, true, true};
    v = Decide(Ctx("https://tracker.example/a.js", "tracker.example"), &b,
               &alive, ScopeSet{}, 0, killed);
    XR_EXPECT(v.action == Action::kAllow && v.why_code == "kill-switch");
    // no bundle: allowed, engine never consulted
    v = Decide(Ctx("https://tracker.example/a.js", "tracker.example"), nullptr,
               &alive);
    XR_EXPECT(v.why_code == "no-bundle" && v.bundle_version == 0);
    // defense in depth: posture inputs claim alive, engine actually dead
    TableEngine corpse(&b);
    corpse.Kill();
    v = Decide(Ctx("https://tracker.example/a.js", "tracker.example"), &b,
               &corpse);
    XR_EXPECT(v.why_code == "engine-dead-fail-open");
    TableEngine poisoned(&b);
    poisoned.Poison();
    XR_EXPECT(!poisoned.alive());
  }
  // ---- exception scopes over real hits ----
  {
    TableEngine e(&b);
    RequestContext c = Ctx("https://tracker.example/a.js", "tracker.example");
    ScopeSet s;
    ExceptionScope sc;
    sc.scope_id = "s1";
    sc.reason = "user";
    sc.site = "tracker.example";
    sc.rule_id = "r-block";
    s.scopes.push_back(sc);
    Verdict v = Decide(c, &b, &e, s);
    XR_EXPECT(v.why_code == "exception-scope" && v.action == Action::kAllow &&
              v.rule_id == "r-block" && v.engine_decision);
    // coupling: another identity is NOT covered
    sc.identity = "xr:other";
    s.scopes[0] = sc;
    XR_EXPECT(Decide(c, &b, &e, s).why_code == "rule-blocked");
    // expiry: boundary-inclusive at now_mono
    s.scopes[0] = sc;
    s.scopes[0].identity = "";
    s.scopes[0].expiry_mono = 100;
    XR_EXPECT(Decide(c, &b, &e, s, 99).why_code == "exception-scope");
    XR_EXPECT(Decide(c, &b, &e, s, 100).why_code == "rule-blocked");
    // scopes cannot manufacture a hit where the engine saw none
    s.scopes[0].expiry_mono = -1;
    XR_EXPECT(Decide(Ctx("https://clean.example/", "clean.example"), &b, &e, s)
                  .why_code == "no-match");
    // redirect hits are suppressible too
    s.scopes[0].site = "ads.example";
    s.scopes[0].rule_id = "r-redir";
    XR_EXPECT(Decide(Ctx("https://ads.example/banner", "ads.example"), &b, &e, s)
                  .why_code == "exception-scope");
  }
  // verdict echo carries the bundle version
  {
    TableEngine e(&b);
    Verdict v = Decide(Ctx("https://tracker.example/a.js", "tracker.example"),
                       &b, &e);
    XR_EXPECT(v.bundle_version == 3);
    std::string c = VerdictToJson(v).Canonical();
    XR_EXPECT(c.find("\"action\":\"block\"") != std::string::npos);
    XR_EXPECT(c.find("\"why_code\":\"rule-blocked\"") != std::string::npos);
    XR_EXPECT(c.find("\"engine_decision\":true") != std::string::npos);
  }
  return xrtest::Report("test_match");
}
