// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Command Registry implementation. Tier-1 ceiling + duplicate/field/danger
// enforcement live here (the registry itself), per Plan §1.10. See registry.h.
#include "commands/core/registry.h"

#include <algorithm>
#include <set>

namespace xr::commands {
namespace {

bool ValidTier(const std::string& t) {
  return std::find(kAttentionTiers.begin(), kAttentionTiers.end(), t) !=
         kAttentionTiers.end();
}
bool ValidDanger(const std::string& d) {
  return std::find(kDangerClasses.begin(), kDangerClasses.end(), d) !=
         kDangerClasses.end();
}

// Registry-metadata (post-freeze) field: scope must be a known scope token.
bool ValidScope(const std::string& s) {
  return s == "global" || s == "window" || s == "identity" || s == "site";
}

}  // namespace

Scope ScopeFromString(const std::string& s) {
  if (s == "window") return Scope::kWindow;
  if (s == "identity") return Scope::kIdentity;
  if (s == "site") return Scope::kSite;
  return Scope::kGlobal;  // unknown => conservative default (not a reject here)
}
const char* ToString(Scope s) {
  switch (s) {
    case Scope::kWindow: return "window";
    case Scope::kIdentity: return "identity";
    case Scope::kSite: return "site";
    case Scope::kGlobal:
    default:
      return "global";
  }
}

RegisterResult Registry::Register(const Command& cmd) {
  RegisterResult r;
  // Missing frozen field.
  for (const char* f : {cmd.id.c_str(), cmd.title.c_str(),
                        cmd.attention_tier.c_str(), cmd.danger_class.c_str(),
                        cmd.surface.c_str(), cmd.handler.c_str()}) {
    if (f[0] == '\0') {
      r.error = "rejected: a frozen descriptor field is empty "
                "(command-descriptor-v1: all six fields required)";
      return r;
    }
  }
  // danger-class / attention-tier validity (re-run; the registry is the law).
  if (!ValidDanger(cmd.danger_class)) {
    r.error = "rejected: danger_class '" + cmd.danger_class +
              "' invalid (must be safe|caution|destructive)";
    return r;
  }
  if (!ValidTier(cmd.attention_tier)) {
    r.error = "rejected: attention_tier '" + cmd.attention_tier +
              "' invalid (must be tier0|tier1|tier2)";
    return r;
  }
  // Duplicate id.
  if (by_id_.count(cmd.id)) {
    r.error = "rejected: duplicate id '" + cmd.id +
              "' (registry ids are unique; re-register is an update, not a dup)";
    return r;
  }
  // Tier-1 ceiling: a 10th tier1 control is refused at registration.
  if (cmd.attention_tier == "tier1" && tier1_ >= kMaxTier1) {
    r.error = "rejected: id '" + cmd.id +
              "' would be the " + std::to_string(tier1_ + 1) +
              "th tier1 control — 'Tier-1 always-visible <=9 controls' "
              "(Plan §1.10 Chrome tiers / Attention Budget). Demote it to "
              "tier2 (Palette/Panel one interaction) to register.";
    return r;
  }

  Command c = cmd;
  c.order = order_.size();
  by_id_[c.id] = c;
  order_.push_back(c.id);
  if (c.attention_tier == "tier1") ++tier1_;
  r.ok = true;
  r.order = c.order;
  return r;
}

bool Registry::KnownId(const std::string& id) const {
  return by_id_.count(id) != 0;
}
const Command* Registry::Find(const std::string& id) const {
  auto it = by_id_.find(id);
  return it == by_id_.end() ? nullptr : &it->second;
}
std::vector<const Command*> Registry::List() const {
  std::vector<const Command*> out;
  out.reserve(order_.size());
  for (const auto& id : order_) out.push_back(&by_id_.at(id));
  return out;
}
std::vector<const Command*> Registry::Group(const std::string& group) const {
  std::vector<const Command*> out;
  for (const auto* c : List())
    if (c->group == group) out.push_back(c);
  return out;
}
std::vector<std::string> Registry::Groups() const {
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (const auto* c : List()) {
    if (seen.insert(c->group).second) out.push_back(c->group);
  }
  return out;
}
size_t Registry::Tier1Count() const { return tier1_; }

JsonValue Registry::ToJson() const {
  JsonValue::Array cmds;
  for (const auto* c : List()) {
    JsonValue::Object desc = {
        {"id", JsonValue(c->id)},
        {"title", JsonValue(c->title)},
        {"attention_tier", JsonValue(c->attention_tier)},
        {"danger_class", JsonValue(c->danger_class)},
        {"surface", JsonValue(c->surface)},
        {"handler", JsonValue(c->handler)},
    };
    JsonValue::Array kw;
    for (const auto& k : c->keywords) kw.push_back(JsonValue(k));
    JsonValue::Object meta = {
        {"keywords", JsonValue(kw)},
        {"group", JsonValue(c->group)},
        {"scope", JsonValue(ToString(c->scope))},
        {"predicate_id", JsonValue(c->predicate_id)},
    };
    JsonValue::Object entry = {
        {"descriptor", JsonValue(desc)},
        {"registry", JsonValue(meta)},
    };
    cmds.push_back(JsonValue(entry));
  }
  JsonValue::Object doc = {
      {"schema", JsonValue("xr-commands-registry")},
      {"schema_version", JsonValue(int64_t{1})},
      {"commands", JsonValue(cmds)},
  };
  return JsonValue(doc);
}

bool Registry::FromJson(const JsonValue& doc, std::string* error) {
  Registry* self = const_cast<Registry*>(this);
  auto set_err = [&](const std::string& m) {
    if (error) *error = m;
    return false;
  };
  if (!doc.is_object()) return set_err("registry doc must be an object");
  const std::string schema =
      doc.find("schema") && doc.find("schema")->is_string()
          ? doc.find("schema")->as_string() : "";
  if (schema != "xr-commands-registry")
    return set_err("bad schema id '" + schema + "' (want xr-commands-registry)");
  const JsonValue* sv = doc.find("schema_version");
  if (!sv || !sv->is_int() || sv->as_int() != 1)
    return set_err("unsupported schema_version (want 1)");
  const JsonValue* arr = doc.find("commands");
  if (!arr || !arr->is_array()) return set_err("missing commands[]");

  // Re-register in array order (deterministic order + re-checks all invariants).
  for (const auto& entry : arr->as_array()) {
    const JsonValue* desc = entry.find("descriptor");
    const JsonValue* meta = entry.find("registry");
    if (!desc || !meta) return set_err("entry missing descriptor/registry");
    DescriptorResult dr = ValidateDescriptor(*desc);
    if (!dr.ok) return set_err("bad descriptor: " + dr.error);
    Command c;
    c.id = dr.fields.id;
    c.title = dr.fields.title;
    c.attention_tier = dr.fields.attention_tier;
    c.danger_class = dr.fields.danger_class;
    c.surface = dr.fields.surface;
    c.handler = dr.fields.handler;
    const JsonValue* kw = meta->find("keywords");
    if (kw && kw->is_array())
      for (const auto& k : kw->as_array())
        if (k.is_string()) c.keywords.push_back(k.as_string());
    const JsonValue* g = meta->find("group");
    if (g && g->is_string()) c.group = g->as_string();
    const JsonValue* sc = meta->find("scope");
    if (sc && sc->is_string()) {
      if (!ValidScope(sc->as_string()))
        return set_err("bad scope '" + sc->as_string() + "'");
      c.scope = ScopeFromString(sc->as_string());
    }
    const JsonValue* pid = meta->find("predicate_id");
    if (pid && pid->is_string() && !pid->as_string().empty())
      c.predicate_id = pid->as_string();

    RegisterResult rr = self->Register(c);
    if (!rr.ok) return set_err("re-register failed: " + rr.error);
  }
  return true;
}

}  // namespace xr::commands
