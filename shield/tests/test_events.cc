// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/events (P11-T2; T5 wires emission) — the redaction
// law AT CREATION (target = scheme://host/path, query/fragment never
// reach the ledger), the frozen mojom event shape (enum spellings
// included), the FIFO ring cap, and the RecentEvents view (identity
// filter, newest-first, max_events AND the 64KB canonical-byte budget).
#include <string>

#include "common/core/json.h"
#include "shield/core/events.h"

#include "harness.h"

using namespace xr::shield;
using xr::common::ParseJson;

namespace {

RequestContext Ctx(const std::string& url) {
  RequestContext c;
  c.identity.value = "xr:a";
  c.origin.scheme = "https";
  c.origin.registrable_domain = "tracker.example";
  c.url = url;
  XR_EXPECT(SplitUrl(url, &c.parts));
  c.request_class = RequestClass::kScript;
  return c;
}

BlockEvent Ev(const std::string& identity, long long ts,
              const std::string& target_pad = "") {
  BlockEvent e;
  e.ts_millis = ts;
  e.identity = identity;
  e.tab_id = 7;
  e.origin.scheme = "https";
  e.origin.registrable_domain = "tracker.example";
  e.target = "https://tracker.example/a" + target_pad;
  e.rule = "||tracker.example^";
  e.list_provenance = "l1";
  e.action = BlockAction::kBlocked;
  e.request_class = RequestClass::kScript;
  return e;
}

}  // namespace

int main() {
  // the redaction law: query + fragment stripped at CREATION
  {
    UrlParts p;
    XR_EXPECT(SplitUrl("https://a.example/x.js?uid=SECRET&tok=2#frag", &p));
    XR_EXPECT_STREQ(RedactTarget(p), "https://a.example/x.js");
    RequestContext c = Ctx("https://tracker.example/a.js?uid=SECRET#f");
    BlockEvent e = MakeEvent(c, 7, 1000, "||tracker.example^", "l1",
                             BlockAction::kBlocked);
    XR_EXPECT(e.target == "https://tracker.example/a.js");  // no query, no frag
    XR_EXPECT(e.target.find("SECRET") == std::string::npos);
    XR_EXPECT(e.identity == "xr:a" && e.tab_id == 7 && e.ts_millis == 1000);
    XR_EXPECT(e.origin.registrable_domain == "tracker.example");
    XR_EXPECT(e.request_class == RequestClass::kScript);
  }
  // frozen mojom bytes: k-spellings, exact field set, sorted canonical
  {
    BlockEvent e = Ev("xr:a", 1000);
    std::string c = EventToJson(e).Canonical();
    XR_EXPECT(c ==
              R"({"action":"kBlocked","identity":{"value":"xr:a"},)"
              R"("list_provenance":"l1","origin":{"registrable_domain":)"
              R"("tracker.example","scheme":"https"},"request_class":)"
              R"("kScript","rule":"||tracker.example^","tab_id":7,)"
              R"("target":"https://tracker.example/a","ts_millis":1000})");
  }
  // action name table (frozen order/spelling)
  XR_EXPECT_STREQ(BlockActionName(BlockAction::kBlocked), "kBlocked");
  XR_EXPECT_STREQ(BlockActionName(BlockAction::kAllowed), "kAllowed");
  XR_EXPECT_STREQ(BlockActionName(BlockAction::kRedirected), "kRedirected");
  XR_EXPECT_STREQ(BlockActionName(BlockAction::kUpgraded), "kUpgraded");

  // FIFO ring cap: oldest evicted first
  {
    EventRing ring;
    for (int i = 0; i < 300; ++i) RingAppend(&ring, Ev("xr:a", i));
    XR_EXPECT(ring.events.size() == EventRing::kCapacity);
    XR_EXPECT(ring.events.front().ts_millis == 44);  // 300-256
    XR_EXPECT(ring.events.back().ts_millis == 299);
  }
  // view: identity filter + newest-first + max cap
  {
    EventRing ring;
    for (int i = 0; i < 5; ++i) RingAppend(&ring, Ev("xr:a", i));
    for (int i = 5; i < 10; ++i) RingAppend(&ring, Ev("xr:b", i));
    auto v = RingView(ring, "xr:a", 3);
    XR_EXPECT(v.size() == 3);
    XR_EXPECT(v[0].Canonical().find("\"ts_millis\":4") != std::string::npos);
    XR_EXPECT(v[2].Canonical().find("\"ts_millis\":2") != std::string::npos);
    XR_EXPECT(RingView(ring, "xr:a", 99).size() == 5);
    XR_EXPECT(RingView(ring, "xr:a", 0).empty());
    XR_EXPECT(RingView(ring, "xr:a", -1).empty());   // negative cap = none
    XR_EXPECT(RingView(ring, "xr:zzz", 9).empty());  // unknown identity
    XR_EXPECT(RingView(ring, "", 99).size() == 10);  // "" = all identities
  }
  // view: the 64KB chunk budget (at least one event always emitted)
  {
    EventRing ring;
    const std::string pad(4096, 'p');  // ~4KB targets
    for (int i = 0; i < 40; ++i) RingAppend(&ring, Ev("xr:a", i, pad));
    auto v = RingView(ring, "xr:a", 40);
    size_t bytes = 0;
    for (const auto& j : v) bytes += j.Canonical().size();
    XR_EXPECT(!v.empty());
    XR_EXPECT(v.size() < 40);  // budget must have cut in
    // budget law: total <= 64KB + the first event (always emitted whole)
    XR_EXPECT(bytes <= kChunkBudgetBytes + v[0].Canonical().size());
    // the cut was budget-driven: one more event would have exceeded it
    XR_EXPECT(bytes + v[0].Canonical().size() > kChunkBudgetBytes);
    // a single oversize event still comes back (never an empty view when
    // the newest matching event exists)
    EventRing big;
    RingAppend(&big, Ev("xr:a", 1, std::string(100000, 'x')));
    XR_EXPECT(RingView(big, "xr:a", 5).size() == 1);
  }
  // ring parse round-trip + strictness
  {
    EventRing ring;
    for (int i = 0; i < 3; ++i) RingAppend(&ring, Ev("xr:a", i));
    std::string c = RingToJson(ring).Canonical();
    auto pr = ParseJson(c);
    XR_EXPECT(pr.ok);
    EventRing back;
    std::string d;
    XR_EXPECT_MSG(ParseRing(pr.value, &back, &d), d);
    XR_EXPECT(RingToJson(back).Canonical() == c);  // idempotent
  }
  {
    EventRing ring;
    std::string d;
    auto pr = ParseJson(R"([{"ts_millis":1,"identity":{"value":"x"},)"
                        R"("tab_id":1,"origin":{"scheme":"https",)"
                        R"("registrable_domain":"e.com"},"target":"t",)"
                        R"("rule":"r","list_provenance":"l","action":"kBlocked",)"
                        R"("request_class":"kScript","extra":1}])");
    XR_EXPECT(pr.ok);
    XR_EXPECT(!ParseRing(pr.value, &ring, &d) && d == "unknown-field:extra");
    pr = ParseJson(R"([{"ts_millis":1,"identity":{"value":"x"},"tab_id":1,)"
                   R"("origin":{"scheme":"https","registrable_domain":"e.com"},)"
                   R"("target":"t","rule":"r","list_provenance":"l",)"
                   R"("action":"blocked","request_class":"kScript"}])");
    XR_EXPECT(pr.ok);
    XR_EXPECT(!ParseRing(pr.value, &ring, &d) &&
              d == "unknown-block-action:blocked");  // k-spelling only
    pr = ParseJson(R"({"x":1})");
    XR_EXPECT(pr.ok);
    XR_EXPECT(!ParseRing(pr.value, &ring, &d) && d == "ring-not-array");
  }
  {  // P11-T5: living block-event-v1 ledger row — shape, redaction,
     // determinism (MakeLedgerRow is the ONLY row constructor).
    RequestContext c = Ctx("https://tracker.example/ad.js?q=secret#frag");
    LedgerRowParams p;
    p.seq = 7;
    p.ts_millis = 1234;
    p.tab_id = 3;
    p.bundle_version = 2;
    p.rule_id = "r-1";
    p.rule = "||tracker.example^";
    p.list_id = "l-1";
    p.why_code = "rule-blocked";
    p.action = BlockAction::kBlocked;
    const std::string canon = MakeLedgerRow(c, p).Canonical();
    XR_EXPECT(canon == MakeLedgerRow(c, p).Canonical());  // deterministic
    XR_EXPECT(canon.find("\"target\":\"https://tracker.example/ad.js\"") !=
              std::string::npos);
    XR_EXPECT(canon.find("secret") == std::string::npos);  // query stripped
    XR_EXPECT(canon.find("frag") == std::string::npos);    // fragment too
    XR_EXPECT(canon.find("\"action\":\"kBlocked\"") != std::string::npos);
    XR_EXPECT(canon.find("\"contract_version\":1") != std::string::npos);
    XR_EXPECT(canon.find("\"event\":\"block_event\"") != std::string::npos);
    XR_EXPECT(canon.find("\"request_class\":\"kScript\"") !=
              std::string::npos);
    XR_EXPECT(canon.find("\"why_code\":\"rule-blocked\"") !=
              std::string::npos);
    XR_EXPECT(canon.find("\"identity\":\"xr:a\"") != std::string::npos);
    XR_EXPECT(canon.find("\"rule_id\":\"r-1\"") != std::string::npos);
    XR_EXPECT(canon.find("\"bundle_version\":2") != std::string::npos);
    p.action = BlockAction::kUpgraded;  // no v1 producer; still a legal row
    XR_EXPECT(MakeLedgerRow(c, p).Canonical().find(
                  "\"action\":\"kUpgraded\"") != std::string::npos);
    p.why_code = "no-bundle";           // the vocab is the caller's choice
    XR_EXPECT(MakeLedgerRow(c, p).Canonical().find(
                  "\"why_code\":\"no-bundle\"") != std::string::npos);
  }
  return xrtest::Report("test_events");
}
