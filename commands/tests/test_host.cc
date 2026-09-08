// Copyright 2026 RRRTX Labs
// test_host.cc — command-host-protocol v1: flag both states (on: seeded 20
// commands / off: empty + invoke rejected), the full method surface, the
// Tier-1 cap via register, and the menu-model tier rules.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "commands/host/protocol.h"
#include "commands/core/registry.h"
#include "commands/tests/harness.h"

using namespace xr::commands;
using xr::commands::host::Context;
using xr::commands::host::HandleMethod;
using xr::commands::host::LoadContext;

namespace {

const std::string kRoster = "../../commands/core/roster_v1.json";
const std::string kDir = "host-test-store";

bool Load(Context* ctx, std::string* err) {
  std::string setup = "rm -rf " + kDir + " && mkdir -p " + kDir;
  int setup_rc = std::system(setup.c_str());
  (void)setup_rc;
  return LoadContext(ctx, kDir, kRoster, err);
}

JsonValue Req(const char* json) {
  auto p = ParseJson(json);
  XR_EXPECT_MSG(p.ok, "request JSON parsed");
  return p.value;
}

std::string OkField(const std::string& result, const char* field) {
  // result is {"ok":{...}}; return the requested string field.
  auto p = ParseJson(result);
  XR_EXPECT(p.ok);
  const JsonValue* okv = p.value.find("ok");
  if (!okv) return "";
  const JsonValue* f = okv->find(field);
  return (f && f->is_string()) ? f->as_string() : std::string();
}

int64_t OkCount(const std::string& result) {
  auto p = ParseJson(result);
  const JsonValue* okv = p.value.find("ok");
  const JsonValue* c = okv ? okv->find("count") : nullptr;
  return (c && c->is_int()) ? c->as_int() : -1;
}

}  // namespace

int main() {
  Context ctx;
  std::string err;
  XR_EXPECT_MSG(Load(&ctx, &err), "LoadContext: " + err);

  // Seeded roster: 20 commands, 7 tier1, tor.open disabled-by-predicate.
  XR_EXPECT_EQ(ctx.registry.Size(), 20);
  XR_EXPECT_EQ(ctx.registry.Tier1Count(), 7);

  // flag-status.
  {
    std::string r = HandleMethod("flag-status", Req("{}"), ctx);
    XR_EXPECT(r.find("\"xr_command_registry_v1\":\"on\"") != std::string::npos);
  }
  // list (all + by group).
  {
    std::string r = HandleMethod("list", Req("{}"), ctx);
    XR_EXPECT_EQ(OkCount(r), 20);
    std::string rg = HandleMethod("list", Req("{\"group\":\"Trust\"}"), ctx);
    XR_EXPECT_EQ(OkCount(rg), 5);  // 4 dial + shield.toggle
  }
  // query: tor.open is present but disabled-with-reason (never absent).
  {
    std::string r = HandleMethod("query", Req("{\"query\":\"tor\"}"), ctx);
    XR_EXPECT(r.find("\"id\":\"tor.open\"") != std::string::npos);
    XR_EXPECT(r.find("\"available\":false") != std::string::npos);
    XR_EXPECT(r.find("P31") != std::string::npos);  // placeholder reason
  }
  // menu-model: tier1 separated, tor.open unavailable, tier1 count == 7.
  {
    std::string r = HandleMethod("menu-model", Req("{}"), ctx);
    auto p = ParseJson(r);
    const JsonValue* tier1 = p.value.find("ok")->find("tier1");
    XR_EXPECT_EQ(tier1->find("count")->as_int(), 7);
    XR_EXPECT(r.find("\"id\":\"tab.close-all\"") != std::string::npos);  // a tier1 item
  }

  // register: new command, then the Tier-1 cap (7 default + 2 = 9, 10th rejects).
  {
    const char* reg =
        "{\"descriptor\":{\"id\":\"extra.one\",\"title\":\"Extra\",\"attention_tier\":\"tier1\","
        "\"danger_class\":\"safe\",\"surface\":\"tools-menu\",\"handler\":\"action.extra\"},"
        "\"registry\":{\"keywords\":[],\"group\":\"X\",\"scope\":\"global\",\"predicate_id\":\"always\"}}";
    std::string r = HandleMethod("register", Req(reg), ctx);
    XR_EXPECT(r.find("\"status\":\"registered\"") != std::string::npos);
    XR_EXPECT_EQ(OkField(r, "id").compare("extra.one"), 0);
    // one more tier1 -> 9
    std::string r2 = HandleMethod(
        "register",
        Req("{\"descriptor\":{\"id\":\"extra.two\",\"title\":\"E2\",\"attention_tier\":\"tier1\","
            "\"danger_class\":\"safe\",\"surface\":\"tools-menu\",\"handler\":\"action.x2\"},"
            "\"registry\":{\"keywords\":[],\"group\":\"X\",\"scope\":\"global\",\"predicate_id\":\"always\"}}"),
        ctx);
    XR_EXPECT(r2.find("\"status\":\"registered\"") != std::string::npos);
    // 10th tier1 => rejected with the cited rule
    std::string r3 = HandleMethod(
        "register",
        Req("{\"descriptor\":{\"id\":\"extra.three\",\"title\":\"E3\",\"attention_tier\":\"tier1\","
            "\"danger_class\":\"safe\",\"surface\":\"tools-menu\",\"handler\":\"action.x3\"},"
            "\"registry\":{\"keywords\":[],\"group\":\"X\",\"scope\":\"global\",\"predicate_id\":\"always\"}}"),
        ctx);
    XR_EXPECT(r3.find("kRejected") != std::string::npos);
    XR_EXPECT(r3.find("<=9") != std::string::npos);
    // unknown availability predicate => rejected (no arbitrary code)
    std::string r4 = HandleMethod(
        "register",
        Req("{\"descriptor\":{\"id\":\"bad.pred\",\"title\":\"B\",\"attention_tier\":\"tier2\","
            "\"danger_class\":\"safe\",\"surface\":\"s\",\"handler\":\"h\"},"
            "\"registry\":{\"keywords\":[],\"group\":\"X\",\"scope\":\"global\",\"predicate_id\":\"evil\"}}"),
        ctx);
    XR_EXPECT(r4.find("unknown availability predicate") != std::string::npos);
  }

  // bindings round-trip through the store (persistent across HandleMethod calls).
  {
    std::string s = HandleMethod("bindings-set",
                                 Req("{\"command_id\":\"tab.new\",\"accelerator\":\"Ctrl+K\"}"), ctx);
    XR_EXPECT(s.find("\"bound\":true") != std::string::npos);
    std::string l = HandleMethod("bindings-list", Req("{}"), ctx);
    XR_EXPECT(l.find("\"accelerator\":\"CTRL+K\"") != std::string::npos);
    // conflict: bind another command to the same accelerator
    std::string c = HandleMethod("bindings-set",
                                 Req("{\"command_id\":\"window.new\",\"accelerator\":\"ctrl+k\"}"), ctx);
    XR_EXPECT(c.find("\"conflict\":\"duplicate\"") != std::string::npos);
    XR_EXPECT(c.find("tab.new") != std::string::npos);  // named
    std::string cl = HandleMethod("bindings-clear", Req("{\"command_id\":\"tab.new\"}"), ctx);
    XR_EXPECT(cl.find("\"cleared\"") != std::string::npos);
  }

  // ---- flag OFF: stock chrome (empty registry, invoke rejected) ----
  Context off;
  XR_EXPECT_MSG(LoadContext(&off, kDir, kRoster, &err), "reload: " + err);
  off.flag_registry = "off";
  {
    XR_EXPECT_EQ(OkCount(HandleMethod("list", Req("{}"), off)), 0);
    XR_EXPECT_EQ(OkCount(HandleMethod("query", Req("{\"query\":\"tab\"}"), off)), 0);
    std::string mi = HandleMethod("invoke",
                                  Req("{\"id\":\"tab.new\",\"source\":\"palette\"}"), off);
    XR_EXPECT(mi.find("\"status\":\"rejected\"") != std::string::npos);
    XR_EXPECT(mi.find("stock chrome") != std::string::npos);
    std::string mm = HandleMethod("menu-model", Req("{}"), off);
    auto p = ParseJson(mm);
    XR_EXPECT_EQ(p.value.find("ok")->find("tier1")->find("count")->as_int(), 0);
    std::string rg = HandleMethod("register",
                                  Req("{\"descriptor\":{\"id\":\"x\",\"title\":\"x\",\"attention_tier\":\"tier2\","
                                      "\"danger_class\":\"safe\",\"surface\":\"s\",\"handler\":\"h\"},"
                                      "\"registry\":{\"keywords\":[],\"group\":\"g\",\"scope\":\"global\",\"predicate_id\":\"always\"}}"),
                                  off);
    XR_EXPECT(rg.find("stock chrome") != std::string::npos);
  }

  std::string teardown = "rm -rf " + kDir;
  int teardown_rc = std::system(teardown.c_str());
  (void)teardown_rc;
  return xrtest::Report("test_host");
}
