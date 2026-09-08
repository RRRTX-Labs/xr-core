// Copyright 2026 RRRTX Labs
// test_matcher.cc — palette ranking: golden rankings for 8 queries (checked-in
// golden) + tie-break determinism (x100 identical runs) + invariants.
//
// Run with --emit-golden to (re)generate commands/tests/golden-rankings.json
// from the fixed 12-command corpus. The golden is the arbiter (frozen).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "commands/core/matcher.h"
#include "commands/core/registry.h"
#include "commands/core/json.h"
#include "commands/tests/harness.h"

using namespace xr::commands;

namespace {

std::vector<Command> BuildCorpus() {
  std::vector<Command> c;
  auto add = [&](const char* id, const char* title, const std::vector<std::string>& kw) {
    Command x;
    x.id = id;
    x.title = title;
    x.keywords = kw;
    x.group = "G";
    x.attention_tier = "tier2";
    x.danger_class = "safe";
    x.surface = "command-palette";
    x.handler = std::string("action.") + id;
    x.order = c.size();
    x.predicate_id = "always";
    c.push_back(x);
  };
  add("tab.new", "New Tab", {"new", "tab"});
  add("window.new", "New Window", {"new", "window"});
  add("identity.new", "New Identity", {"new", "identity"});
  add("panel.open", "Open XR Panel", {"open", "panel"});
  add("help.index", "Open Help Index", {"help", "index"});
  add("help.cheatsheet", "Open Printable Cheatsheet", {"help", "cheatsheet"});
  add("settings.network", "Settings: Network", {"settings", "network"});
  add("trust.fortress", "Trust: Fortress", {"trust", "fortress"});
  add("trust.shield", "Trust: Shield", {"trust", "shield"});
  add("tor.open", "Tor Session", {"tor"});
  add("history.clear", "Clear Identity History", {"clear", "history"});
  add("download.all", "Download All Files", {"download"});
  return c;
}

const std::vector<std::string> kQueries = {
    "new", "trust", "open", "help", "cheatsheet", "x", "", "zzz"};

std::string RankKey(const std::vector<RankedMatch>& v) {
  std::string s;
  for (const auto& m : v) {
    s += m.id + ":" + std::to_string(m.score) + ":" + std::to_string(m.kind) + ";";
  }
  return s;
}

JsonValue QueryToJson(const std::vector<RankedMatch>& v) {
  JsonValue::Array arr;
  for (const auto& m : v) {
    JsonValue::Object o = {
        {"id", JsonValue(m.id)},
        {"kind", JsonValue(static_cast<int64_t>(m.kind))},
        {"score", JsonValue(static_cast<int64_t>(m.score))},
    };
    arr.push_back(JsonValue(o));
  }
  return JsonValue(arr);
}

int EmitGolden(const std::vector<Command>& corpus) {
  std::vector<const Command*> ptrs;
  for (auto& c : corpus) ptrs.push_back(&c);
  JsonValue::Object queries;
  for (const auto& q : kQueries)
    queries.emplace(q.empty() ? "EMPTY" : q, QueryToJson(MatchQuery(q, ptrs)));
  std::string path = "golden-rankings.json";
  std::ofstream f(path);
  f << JsonValue(queries).Canonical() << "\n";
  std::printf("emitted %s\n", path.c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<Command> corpus = BuildCorpus();
  std::vector<const Command*> ptrs;
  for (auto& c : corpus) ptrs.push_back(&c);

  if (argc > 1 && std::string(argv[1]) == "--emit-golden") return EmitGolden(corpus);

  // Load the checked-in golden.
  std::ifstream f("golden-rankings.json");
  if (!f.good()) {
    std::fprintf(stderr, "missing golden-rankings.json (run --emit-golden)\n");
    return 1;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  auto p = ParseJson(ss.str());
  XR_EXPECT_MSG(p.ok, "golden parsed");
  const JsonValue& golden = p.value;

  for (const auto& q : kQueries) {
    const std::string key = q.empty() ? "EMPTY" : q;
    const JsonValue* gq = golden.find(key);
    XR_EXPECT_MSG(gq != nullptr, ("golden has query " + key).c_str());
    std::string got = RankKey(MatchQuery(q, ptrs));
    std::string want;
    if (gq && gq->is_array()) {
      for (const auto& e : gq->as_array()) {
        const JsonValue* id = e.find("id");
        const JsonValue* sc = e.find("score");
        const JsonValue* kd = e.find("kind");
        want += (id ? id->as_string() : "") + ":" + std::to_string(sc ? sc->as_int() : 0) +
                ":" + std::to_string(kd ? kd->as_int() : 0) + ";";
      }
    }
    XR_EXPECT_MSG(got == want, ("ranking for '" + key + "' matches golden (got " + got +
                                ", want " + want + ")").c_str());
  }

  // Determinism: 100 identical runs => identical output (stable tie-break).
  for (const auto& q : kQueries) {
    std::string first = RankKey(MatchQuery(q, ptrs));
    bool stable = true;
    for (int i = 0; i < 100; ++i) stable = stable && (RankKey(MatchQuery(q, ptrs)) == first);
    XR_EXPECT_MSG(stable, ("determinism x100 for '" + q + "'").c_str());
  }

  // Invariants.
  {
    auto r = MatchQuery("zzz", ptrs);
    XR_EXPECT_EQ(r.size(), 0);  // no subsequence match anywhere
  }
  {
    auto r = MatchQuery("", ptrs);
    XR_EXPECT_EQ(r.size(), corpus.size());  // empty query => all, in order
    bool in_order = true;
    for (size_t i = 0; i + 1 < r.size(); ++i) in_order = in_order && r[i].order < r[i + 1].order;
    XR_EXPECT_MSG(in_order, "empty query preserves registration order");
  }
  {
    // Tie-break: two commands with identical scoring => registration order.
    std::vector<Command> two = BuildCorpus();
    two[0].id = "alpha";
    two[0].title = "Zebra Zebra Zebra";
    two[0].keywords = {};
    two[1].id = "beta";
    two[1].title = "Zebra Zebra Zebra";
    two[1].keywords = {};
    std::vector<const Command*> tp;
    for (auto& c : two) tp.push_back(&c);
    auto r = MatchQuery("zebra", tp);
    XR_EXPECT_EQ(r.size(), 2);
    XR_EXPECT_STREQ(r[0].id.c_str(), "alpha");  // earlier registration wins the tie
    XR_EXPECT_STREQ(r[1].id.c_str(), "beta");
  }
  return xrtest::Report("test_matcher");
}
