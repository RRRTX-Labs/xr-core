// Copyright 2026 RRRTX Labs
// test_registry.cc — registry invariants: dupes, missing-field, danger-class,
// tier-1 <=9 (in the registry itself), grouping, serialize round-trip.
#include <string>
#include <vector>

#include "commands/core/registry.h"
#include "commands/tests/harness.h"

using namespace xr::commands;

namespace {

Command C(const std::string& id, const std::string& tier = "tier2",
          const std::string& danger = "safe", const std::string& group = "G") {
  Command c;
  c.id = id;
  c.title = "Title " + id;
  c.attention_tier = tier;
  c.danger_class = danger;
  c.surface = "command-palette";
  c.handler = "action." + id;
  c.group = group;
  c.keywords = {"a", "b"};
  c.scope = Scope::kGlobal;
  c.predicate_id = "always";
  return c;
}

}  // namespace

int main() {
  // Duplicate id.
  {
    Registry r;
    XR_EXPECT(r.Register(C("a")).ok);
    RegisterResult dup = r.Register(C("a"));
    XR_EXPECT_MSG(!dup.ok, "duplicate id rejected");
    XR_EXPECT(dup.error.find("duplicate") != std::string::npos);
    XR_EXPECT_EQ(r.Size(), 1);
  }
  // Missing frozen field.
  {
    Registry r;
    Command c = C("a");
    c.title.clear();
    XR_EXPECT_MSG(!r.Register(c).ok, "empty frozen field rejected");
  }
  // Danger-class validity.
  {
    Registry r;
    Command c = C("a", "tier2", "explosive");
    XR_EXPECT_MSG(!r.Register(c).ok, "invalid danger_class rejected");
    Command c2 = C("a", "tier0");
    XR_EXPECT(r.Register(c2).ok);  // tier0 is legal
  }
  // Tier-1 cap: 9 ok, the 10th rejected with the cited rule.
  {
    Registry r;
    for (int i = 0; i < 9; ++i) {
      RegisterResult rr = r.Register(C("t" + std::to_string(i), "tier1"));
      XR_EXPECT_MSG(rr.ok, "first 9 tier1 controls accepted");
    }
    XR_EXPECT_EQ(r.Tier1Count(), 9);
    RegisterResult tenth = r.Register(C("tenth", "tier1"));
    XR_EXPECT_MSG(!tenth.ok, "10th tier1 control rejected at registration");
    // The refusal states the ceiling truthfully (P14-CLOSE re-score).
    XR_EXPECT(tenth.error.find("would be the 10th tier1 control") != std::string::npos);
    XR_EXPECT(tenth.error.find("'Tier-1 always-visible <=9 controls'") != std::string::npos);
    XR_EXPECT(tenth.error.find("<=9") != std::string::npos ||
             tenth.error.find("Tier-1") != std::string::npos);
    XR_EXPECT_EQ(r.Tier1Count(), 9);  // unchanged
    // a tier2 control still registers after the cap (cap is tier1-only).
    XR_EXPECT(r.Register(C("tier2.after.cap", "tier2")).ok);
  }
  // Grouping.
  {
    Registry r;
    r.Register(C("a", "tier2", "safe", "Identity"));
    r.Register(C("b", "tier2", "safe", "Trust"));
    r.Register(C("c", "tier2", "safe", "Identity"));
    auto g = r.Group("Identity");
    XR_EXPECT_EQ(g.size(), 2);
    auto groups = r.Groups();
    XR_EXPECT_EQ(groups.size(), 2);
    XR_EXPECT_STREQ(groups[0].c_str(), "Identity");  // first-seen order
    XR_EXPECT_STREQ(groups[1].c_str(), "Trust");
  }
  // Serialize round-trip (commands-registry-v1).
  {
    Registry r;
    r.Register(C("a", "tier1", "destructive", "Trust"));
    r.Register(C("b", "tier2", "safe", "Tabs"));
    JsonValue j = r.ToJson();
    Registry r2;
    std::string err;
    XR_EXPECT_MSG(r2.FromJson(j, &err), "FromJson round-trip: " + err);
    XR_EXPECT_EQ(r2.Size(), 2);
    const Command* a = r2.Find("a");
    XR_EXPECT(a != nullptr);
    XR_EXPECT_STREQ(a->attention_tier.c_str(), "tier1");
    XR_EXPECT_STREQ(a->danger_class.c_str(), "destructive");
    XR_EXPECT_STREQ(a->group.c_str(), "Trust");
    XR_EXPECT_EQ(r2.Tier1Count(), 1);
    // canonical JSON is sorted-key + byte-stable.
    XR_EXPECT_STREQ(r.ToJson().Canonical().c_str(), r2.ToJson().Canonical().c_str());
  }
  // KnownId answers both ways.
  {
    Registry r;
    r.Register(C("known", "tier2"));
    XR_EXPECT(r.KnownId("known"));
    XR_EXPECT(!r.KnownId("unknown"));
    XR_EXPECT(!r.KnownId(""));
  }
  // FromJson refuses every malformed doc with false AND a reason (P14-CLOSE:
  // the honest mutation re-score found no test of a refusal).
  {
    Registry ok;
    ok.Register(C("a", "tier2"));
    const JsonValue good = ok.ToJson();
    const JsonValue entry = good.find("commands")->as_array()[0];
    auto doc = [](JsonValue::Array cmds, int64_t version, const char* schema) {
      JsonValue::Object d = {{"schema", JsonValue(schema)},
                             {"schema_version", JsonValue(version)},
                             {"commands", JsonValue(std::move(cmds))}};
      return JsonValue(d);
    };
    JsonValue::Object desc_only = {{"descriptor", *entry.find("descriptor")}};
    JsonValue::Object meta_only = {{"registry", *entry.find("registry")}};
    struct Case { JsonValue d; const char* want; };
    const Case cases[] = {
        {JsonValue(JsonValue::Array{}), "registry doc must be an object"},
        {doc({entry}, 1, "other"), "bad schema id 'other' (want xr-commands-registry)"},
        {doc({entry}, 2, "xr-commands-registry"), "unsupported schema_version (want 1)"},
        {doc({JsonValue(desc_only)}, 1, "xr-commands-registry"), "entry missing descriptor/registry"},
        {doc({JsonValue(meta_only)}, 1, "xr-commands-registry"), "entry missing descriptor/registry"},
    };
    for (const Case& c : cases) {
      Registry r2;
      std::string err;
      XR_EXPECT_MSG(!r2.FromJson(c.d, &err), std::string("refused: ") + c.want);
      XR_EXPECT_MSG(err == c.want, "reason: " + err);
    }
    Registry r3;
    std::string err;
    XR_EXPECT(r3.FromJson(doc({entry}, 1, "xr-commands-registry"), &err));
    XR_EXPECT_EQ(r3.Size(), 1);
  }
  return xrtest::Report("test_registry");
}
