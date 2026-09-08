// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// command-host-protocol v1 handlers. See protocol.h + host_protocol.md for the
// exact canonical output shapes (the Python fake matches them byte-for-byte).
#include "commands/host/protocol.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "commands/core/dispatch.h"
#include "commands/core/matcher.h"

namespace xr::commands::host {
namespace {

std::string ReadAll(const std::string& path, bool* exists) {
  *exists = false;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return {};
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  std::fclose(f);
  *exists = true;
  return data;
}
bool WriteAll(const std::string& path, const std::string& bytes) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  size_t w = std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  return w == bytes.size();
}

std::string ArgString(const JsonValue& args, const char* k, const std::string& dflt) {
  const JsonValue* v = args.find(k);
  return (v && v->is_string()) ? v->as_string() : dflt;
}
bool ArgBool(const JsonValue& args, const char* k) {
  const JsonValue* v = args.find(k);
  return v && v->is_bool() && v->as_bool();
}

JsonValue CommandEntry(const Command* c) {
  JsonValue::Object desc = {
      {"attention_tier", JsonValue(c->attention_tier)},
      {"danger_class", JsonValue(c->danger_class)},
      {"handler", JsonValue(c->handler)},
      {"id", JsonValue(c->id)},
      {"surface", JsonValue(c->surface)},
      {"title", JsonValue(c->title)},
  };
  JsonValue::Array kw;
  for (const auto& k : c->keywords) kw.push_back(JsonValue(k));
  JsonValue::Object meta = {
      {"group", JsonValue(c->group)},
      {"keywords", JsonValue(kw)},
      {"predicate_id", JsonValue(c->predicate_id)},
      {"scope", JsonValue(ToString(c->scope))},
  };
  JsonValue::Object o = {{"descriptor", JsonValue(desc)}, {"registry", JsonValue(meta)}};
  return JsonValue(o);
}

std::string Ok(const JsonValue& v) {
  return JsonValue(JsonValue::Object{{"ok", v}}).Canonical();
}
std::string Err(const std::string& code, const std::string& detail) {
  JsonValue::Object o = {{"error", JsonValue(code)}};
  if (!detail.empty()) o.emplace("detail", JsonValue(detail));
  return JsonValue(o).Canonical();
}

// --- methods ---------------------------------------------------------------

std::string MFlagStatus(const JsonValue&, Context& ctx) {
  JsonValue::Object flags = {{"xr_command_registry_v1", JsonValue(ctx.flag_registry)}};
  return Ok(JsonValue(flags));
}

std::string MList(const JsonValue& args, Context& ctx) {
  JsonValue::Array cmds;
  const std::string group = ArgString(args, "group", std::string());
  for (const Command* c : ctx.registry.List()) {
    if (!group.empty() && c->group != group) continue;
    if (ctx.FlagOn()) cmds.push_back(CommandEntry(c));
  }
  JsonValue::Object o = {{"commands", JsonValue(cmds)},
                         {"count", JsonValue(static_cast<int64_t>(cmds.size()))}};
  return Ok(JsonValue(o));
}

std::string MQuery(const JsonValue& args, Context& ctx) {
  JsonValue::Array results;
  if (ctx.FlagOn()) {
    const std::string q = ArgString(args, "query", std::string());
    const std::vector<RankedMatch> ranked = MatchQuery(q, ctx.registry.List());
    const JsonValue snap = ctx.policy.Snapshot();  // pinned copy
    for (const auto& m : ranked) {
      const Command* c = ctx.registry.Find(m.id);
      // Availability against the PINNED snapshot copy (TOCTOU-safe).
      AvailabilityVerdict av = Availability().Evaluate(c->predicate_id, snap);
      JsonValue::Object r = {
          {"available", JsonValue(av.available)},
          {"id", JsonValue(m.id)},
          {"kind", JsonValue(static_cast<int64_t>(m.kind))},
          {"reason", JsonValue(av.available ? std::string() : av.reason)},
          {"score", JsonValue(static_cast<int64_t>(m.score))},
          {"title", JsonValue(m.title)},
      };
      results.push_back(JsonValue(r));
    }
  }
  JsonValue::Object o = {{"count", JsonValue(static_cast<int64_t>(results.size()))},
                         {"results", JsonValue(results)}};
  return Ok(JsonValue(o));
}

std::string MInvoke(const JsonValue& args, Context& ctx) {
  const std::string id = ArgString(args, "id", std::string());
  const std::string source = ArgString(args, "source", std::string());
  const bool confirmed = ArgBool(args, "confirmed");
  const std::string site = ArgString(args, "site", "*");

  if (!ctx.FlagOn()) {
    JsonValue::Object o = {
        {"id", JsonValue(id)},
        {"reason", JsonValue("feature flag xr_command_registry_v1 is off (stock chrome)")},
        {"source", JsonValue(source)},
        {"status", JsonValue("rejected")},
    };
    return Ok(JsonValue(o));
  }
  Dispatcher d(ctx.registry);
  InvokeOutcome out = d.Invoke(id, source, confirmed);

  // Execute the handler (authorized only). Dials toggle P6 trust state.
  JsonValue::Object effect;
  if (out.authorized) {
    std::string terr;
    if (out.handler == "action.dial.standard" || out.handler == "action.dial.shield" ||
        out.handler == "action.dial.fortress") {
      const char* tier = out.handler == "action.dial.standard"
                             ? "kStandard"
                             : (out.handler == "action.dial.shield" ? "kShield" : "kFortress");
      std::string prev = ctx.policy.SetTrust(site, "", tier, &terr);
      effect.emplace("prev", JsonValue(prev));
      effect.emplace("site", JsonValue(site));
      effect.emplace("trust", JsonValue(tier));
    } else if (out.handler == "action.dial.reset") {
      std::string prev = ctx.policy.RemoveTrust(site, "", &terr);
      effect.emplace("prev", JsonValue(prev));
      effect.emplace("removed", JsonValue(true));
      effect.emplace("site", JsonValue(site));
    }
    if (!terr.empty()) {
      // A policy write failure is a typed reject, not a silent no-op.
      JsonValue::Object o = {
          {"id", JsonValue(id)},
          {"reason", JsonValue("policy state write failed: " + terr)},
          {"source", JsonValue(source)},
          {"status", JsonValue("rejected")},
      };
      return Ok(JsonValue(o));
    }
  }

  JsonValue::Object o = {
      {"id", JsonValue(out.id)},
      {"status", JsonValue(out.status)},
      {"source", JsonValue(out.source)},
  };
  if (!out.handler.empty()) o.emplace("handler", JsonValue(out.handler));
  if (!out.danger_class.empty()) o.emplace("danger_class", JsonValue(out.danger_class));
  if (!out.reason.empty()) o.emplace("reason", JsonValue(out.reason));
  o.emplace("effect", JsonValue(effect));
  JsonValue::Array ledger;
  for (const auto& row : out.ledger_rows) ledger.push_back(JsonValue(row));
  o.emplace("ledger", JsonValue(ledger));
  return Ok(JsonValue(o));
}

std::string MBindingsSet(const JsonValue& args, Context& ctx) {
  const std::string cid = ArgString(args, "command_id", std::string());
  const std::string acc = ArgString(args, "accelerator", std::string());
  if (!ctx.registry.KnownId(cid)) {
    JsonValue::Object o = {{"bound", JsonValue(false)},
                           {"command_id", JsonValue(cid)},
                           {"conflict", JsonValue("none")},
                           {"error", JsonValue("unknown command id '" + cid + "'")}};
    return Ok(JsonValue(o));
  }
  BindResult r = ctx.shortcuts.Bind(cid, acc);
  bool persisted = false;
  if (r.ok) persisted = ctx.shortcuts.Save(nullptr);
  JsonValue::Object o = {
      {"bound", JsonValue(r.ok)},
      {"command_id", JsonValue(cid)},
      {"conflict", JsonValue(ToString(r.conflict))},
      {"persisted", JsonValue(persisted)},
  };
  if (r.conflict == ConflictKind::kDuplicate)
    o.emplace("conflicting_command", JsonValue(r.conflicting_command));
  if (!r.error.empty()) o.emplace("error", JsonValue(r.error));
  return Ok(JsonValue(o));
}

std::string MBindingsList(const JsonValue&, Context& ctx) {
  auto binds = ctx.shortcuts.bindings();
  std::vector<Binding> sorted = binds;
  std::stable_sort(sorted.begin(), sorted.end(), [](const Binding& a, const Binding& b) {
    if (a.accelerator != b.accelerator) return a.accelerator < b.accelerator;
    return a.command_id < b.command_id;
  });
  JsonValue::Array arr;
  for (const auto& b : sorted) {
    JsonValue::Object o = {{"accelerator", JsonValue(b.accelerator)},
                           {"command_id", JsonValue(b.command_id)}};
    arr.push_back(JsonValue(o));
  }
  JsonValue::Object o = {{"bindings", JsonValue(arr)},
                         {"count", JsonValue(static_cast<int64_t>(arr.size()))}};
  return Ok(JsonValue(o));
}

std::string MBindingsClear(const JsonValue& args, Context& ctx) {
  const std::string cid = ArgString(args, "command_id", std::string());
  bool persisted = false;
  if (!cid.empty()) {
    std::string err;
    bool removed = ctx.shortcuts.Unbind(cid, &err);
    if (removed) persisted = ctx.shortcuts.Save(nullptr);
    JsonValue::Object o = {{"cleared", removed ? JsonValue(cid) : JsonValue()},
                           {"persisted", JsonValue(persisted)}};
    return Ok(JsonValue(o));
  }
  // clear all
  std::vector<std::string> ids;
  for (const auto& b : ctx.shortcuts.bindings()) ids.push_back(b.command_id);
  for (const auto& i : ids) (void)ctx.shortcuts.Unbind(i, nullptr);
  persisted = ctx.shortcuts.Save(nullptr);
  JsonValue::Object o = {{"cleared_all", JsonValue(true)},
                         {"persisted", JsonValue(persisted)}};
  return Ok(JsonValue(o));
}

std::string MMenuModel(const JsonValue&, Context& ctx) {
  JsonValue::Array tier1_items;
  JsonValue::Array menu_items;
  if (ctx.FlagOn()) {
    const JsonValue snap = ctx.policy.Snapshot();
    Availability availability;
    for (const Command* c : ctx.registry.List()) {
      AvailabilityVerdict av = availability.Evaluate(c->predicate_id, snap);
      JsonValue::Object item = {
          {"available", JsonValue(av.available)},
          {"danger_class", JsonValue(c->danger_class)},
          {"group", JsonValue(c->group)},
          {"id", JsonValue(c->id)},
          {"reason", JsonValue(av.available ? std::string() : av.reason)},
          {"tier", JsonValue(c->attention_tier)},
          {"title", JsonValue(c->title)},
      };
      if (c->attention_tier == "tier1") tier1_items.push_back(JsonValue(item));
      else menu_items.push_back(JsonValue(item));
    }
  }
  JsonValue::Object tools = {{"items", JsonValue(menu_items)},
                             {"kind", JsonValue("tools")}};
  JsonValue::Array menus = {JsonValue(tools)};
  JsonValue::Object tier1 = {
      {"count", JsonValue(static_cast<int64_t>(tier1_items.size()))},
      {"items", JsonValue(tier1_items)}};
  JsonValue::Object o = {
      {"flag", JsonValue(ctx.flag_registry)},
      {"menus", JsonValue(menus)},
      {"tier1", JsonValue(tier1)}};
  return Ok(JsonValue(o));
}

std::string MRegister(const JsonValue& args, Context& ctx) {
  if (!ctx.FlagOn()) {
    return Ok(JsonValue(JsonValue::Object{
        {"status", JsonValue("rejected")},
        {"reason", JsonValue("feature flag xr_command_registry_v1 is off (stock chrome)")}}));
  }
  const JsonValue* desc = args.find("descriptor");
  const JsonValue* meta = args.find("registry");
  if (!desc || !meta) return Err("kMalformedInput", "register needs descriptor + registry");
  DescriptorResult dr = ValidateDescriptor(*desc);
  if (!dr.ok) return Err("kMalformedInput", dr.error);
  Command c;
  c.id = dr.fields.id; c.title = dr.fields.title; c.attention_tier = dr.fields.attention_tier;
  c.danger_class = dr.fields.danger_class; c.surface = dr.fields.surface; c.handler = dr.fields.handler;
  const JsonValue* kw = meta->find("keywords");
  if (kw && kw->is_array())
    for (const auto& k : kw->as_array()) if (k.is_string()) c.keywords.push_back(k.as_string());
  const JsonValue* g = meta->find("group");
  if (g && g->is_string()) c.group = g->as_string();
  const JsonValue* sc = meta->find("scope");
  if (sc && sc->is_string()) c.scope = ScopeFromString(sc->as_string());
  const JsonValue* pid = meta->find("predicate_id");
  if (pid && pid->is_string() && !pid->as_string().empty()) c.predicate_id = pid->as_string();

  // unknown predicate => reject (no arbitrary code in descriptors).
  if (!Availability().KnownPredicate(c.predicate_id))
    return Err("kMalformedInput", "unknown availability predicate '" + c.predicate_id + "'");

  // In-memory register; if it succeeds and we have a store dir, persist.
  Registry scratch = ctx.registry;
  RegisterResult rr = scratch.Register(c);
  if (!rr.ok) return Err("kRejected", rr.error);
  ctx.registry = scratch;
  if (!ctx.store_dir_empty) {
    bool saved = WriteAll(ctx.registry_path, ctx.registry.ToJson().Canonical() + "\n");
    if (!saved) return Err("kIoError", "could not persist registry");
  }
  JsonValue::Object o = {
      {"id", JsonValue(c.id)},
      {"order", JsonValue(static_cast<int64_t>(rr.order))},
      {"status", JsonValue("registered")},
      {"tier1_count", JsonValue(static_cast<int64_t>(ctx.registry.Tier1Count()))},
  };
  return Ok(JsonValue(o));
}

}  // namespace

bool LoadContext(Context* ctx, const std::string& store_dir,
                 const std::string& roster_path, std::string* error) {
  ctx->shortcuts = ShortcutStore(store_dir);
  ctx->policy = PolicyState(store_dir);
  ctx->store_dir_empty = store_dir.empty();
  ctx->registry_path = store_dir.empty() ? "" : store_dir + "/registry.json";

  bool exists = false;
  std::string bytes = ReadAll(ctx->registry_path, &exists);
  if (exists && !bytes.empty()) {
    auto p = ParseJson(bytes);
    if (!p.ok) {
      if (error) *error = "registry.json not valid: " + p.error;
      return false;
    }
    if (!ctx->registry.FromJson(p.value, error)) return false;
  } else {
    // Seed from the checked-in first-20 roster.
    std::string rbytes = ReadAll(roster_path, &exists);
    if (!exists || rbytes.empty()) {
      if (error) *error = "no registry.json and cannot read roster " + roster_path;
      return false;
    }
    auto p = ParseJson(rbytes);
    if (!p.ok) {
      if (error) *error = "roster not valid: " + p.error;
      return false;
    }
    if (!ctx->registry.FromJson(p.value, error)) return false;
    if (!store_dir.empty()) {
      if (!WriteAll(ctx->registry_path, ctx->registry.ToJson().Canonical() + "\n")) {
        if (error) *error = "could not persist seeded registry";
        return false;
      }
    }
  }
  auto sr = ctx->shortcuts.Load();
  if (!sr.ok) {
    // deny-preserve: a corrupt shortcuts file is surfaced, not silently dropped.
    if (error) *error = sr.error;
    return false;
  }
  return true;
}

std::string HandleMethod(const std::string& method, const JsonValue& args,
                         Context& ctx) {
  if (method == "flag-status") return MFlagStatus(args, ctx);
  if (method == "list") return MList(args, ctx);
  if (method == "query") return MQuery(args, ctx);
  if (method == "invoke") return MInvoke(args, ctx);
  if (method == "bindings-set") return MBindingsSet(args, ctx);
  if (method == "bindings-list") return MBindingsList(args, ctx);
  if (method == "bindings-clear") return MBindingsClear(args, ctx);
  if (method == "menu-model") return MMenuModel(args, ctx);
  if (method == "register") return MRegister(args, ctx);
  return Err("kUnknownMethod", "unknown method '" + method + "'");
}

}  // namespace xr::commands::host
