// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — identity_host: the JSON-over-stdio façade for the identity
// core (P14-T1). Same conventions as every host (fakes/README.md +
// identity/host_protocol.md): ONE canonical JSON line out (sorted keys,
// \uXXXX non-ASCII), exit 0 = typed result, 1 = typed error, 2 = usage.
// No exceptions cross the boundary. No clock, no RNG, no environment: the
// mint entropy, the scheduler tick, the attribution samples all ride in the
// request — deterministic because the host is.
//
// TWO surfaces (the shield_host vocabulary law + the policy_host dispatch
// precedent):
//   * the FROZEN mojom surface (mojom/identity.mojom v1: Create, Activate,
//     Hibernate, PromoteToFortressProfile, Destroy, MoveTab) — dispatched
//     via `method == "…"` on the {method,args} JSON form, keeping the P5
//     fake envelope {"ok":…}/{"error":…} and the frozen spellings, so
//     fakes/fixtures/identity-v1.json and tools/parity/corpus-identity.json
//     replay BYTE-IDENTICALLY against this host and fakes/identity.py
//     (the parity law). Frozen Create is the FAKE's shape: it mints from
//     the deterministic fixture table (the fake's _MINTS) and accepts
//     `_seed` rows — production provisioning is the LIVING `provision`
//     subcommand, which DEMANDS entropy (the host cannot accidentally mint
//     from the fixture table).
//   * the LIVING host surface (provision, templates, ceremony, resolve-pref,
//     scheduler activate/hibernate/wake, destroy, bind-*, suggest,
//     autoswitch-probe, audit, attribute, scenario) is a SUBCOMMAND
//     surface (argv[1], the policy_host pattern): bare canonical results,
//     {"error","detail"} typed errors (exit 1), {"error":"kRejected",
//     "reason"} typed rejections (exit 0). The living surface is NOT the
//     frozen contract and has no fake counterpart — it is covered by the
//     C++ suite + tools/tests/test_p14_identity.py (the policy-precedent
//     coverage split), never by silent omission.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "common/core/json.h"
#include "core/attribution.h"
#include "core/binding.h"
#include "core/hibernate.h"
#include "core/identity.h"
#include "core/ledger_tag.h"
#include "core/mint.h"
#include "core/templates.h"

namespace {

using xr::common::JsonValue;
using xr::common::JsonParseResult;
using xr::common::ParseJson;
using xr::identity::AttributionModel;
using xr::identity::AttributionRow;
using xr::identity::BindingModel;
using xr::identity::ChangeCause;
using xr::identity::CreateRequest;
using xr::identity::Grade;
using xr::identity::IdentityRecord;
using xr::identity::IdentityStore;
using xr::identity::Manager;
using xr::identity::ProcessSample;
using xr::identity::Scheduler;

int Emit(const JsonValue& v) {
  std::printf("%s\n", v.Canonical().c_str());
  return 0;
}
int EmitErr(const std::string& code, const std::string& detail) {
  JsonValue::Object o;
  o["error"] = JsonValue(code);
  o["detail"] = JsonValue(detail);
  Emit(JsonValue(std::move(o)));
  return 1;  // typed error: exit 1 (the response law)
}
int EmitReject(const std::string& reason) {
  JsonValue::Object o;
  o["error"] = JsonValue("kRejected");
  o["reason"] = JsonValue(reason);
  return Emit(JsonValue(std::move(o)));  // caller returns 0
}

std::string ReadAllStdin() {
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) data.append(buf, n);
  return data;
}

// Shared living state (one request = one process = one state).
struct HostState {
  IdentityStore store;
  Manager manager{&store};
  Scheduler scheduler{&manager};
  BindingModel binding;
};

std::string Str(const JsonValue* v, const char* key,
                const std::string& dflt = "") {
  if (v == nullptr) return dflt;
  const JsonValue* f = v->find(key);
  return (f && f->is_string()) ? f->as_string() : dflt;
}
int64_t Int(const JsonValue* v, const char* key, int64_t dflt = 0) {
  if (v == nullptr) return dflt;
  const JsonValue* f = v->find(key);
  return (f && f->is_int()) ? f->as_int() : dflt;
}
bool Bool(const JsonValue* v, const char* key, bool dflt = false) {
  if (v == nullptr) return dflt;
  const JsonValue* f = v->find(key);
  return (f && f->is_bool()) ? f->as_bool() : dflt;
}

const char* StateName(xr::identity::State s) {
  switch (s) {
    case xr::identity::State::kActive: return "kActive";
    case xr::identity::State::kHibernated: return "kHibernated";
    case xr::identity::State::kDestroyed: return "kDestroyed";
  }
  return "kDestroyed";
}

JsonValue RecordJson(const IdentityRecord& r) {
  JsonValue::Object o;
  o["color"] = JsonValue(r.color);
  o["display_name"] = JsonValue(r.display_name);
  o["domain"] = JsonValue(r.domain);
  o["glyph"] = JsonValue(r.glyph);
  o["grade"] = JsonValue(r.grade == Grade::kFortress ? "kFortress"
                                                      : "kStandard");
  o["in_memory"] = JsonValue(r.in_memory);
  o["state"] = JsonValue(StateName(r.state));
  o["template_id"] = JsonValue(r.template_id);
  return JsonValue(std::move(o));
}

// ---- frozen (mojom v1) surface: the fake's exact envelope ---------------

JsonValue FrozenInfo(const IdentityRecord& r) {
  JsonValue::Object id;
  id["value"] = JsonValue(r.domain);
  JsonValue::Object o;
  o["grade"] = JsonValue(r.grade == Grade::kFortress ? "kFortress"
                                                      : "kStandard");
  o["id"] = JsonValue(std::move(id));
  o["in_memory"] = JsonValue(r.in_memory);
  o["state"] = JsonValue(StateName(r.state));
  return JsonValue(std::move(o));
}

JsonValue Ok(JsonValue v) {
  JsonValue::Object o;
  o["ok"] = std::move(v);
  return JsonValue(std::move(o));
}
JsonValue ErrF(const std::string& code) {
  JsonValue::Object o;
  o["error"] = JsonValue(code);
  return JsonValue(std::move(o));
}

// Runs a frozen method against a FRESH seeded state (the fake's call()
// semantics — fakes/fixtures/identity-v1.json replays identically).
JsonValue FrozenCall(const std::string& method, const JsonValue& args) {
  IdentityStore store;
  Manager m(&store);
  // _seed: prior creates, exactly like the fake.
  if (const JsonValue* seed = args.find("_seed")) {
    if (seed->is_array()) {
      for (const auto& pre : seed->as_array()) {
        CreateRequest req;
        req.replay = true;
        if (const JsonValue* g = pre.find("grade");
            g && g->is_string() && g->as_string() == "kFortress") {
          req.grade = Grade::kFortress;
        }
        req.in_memory = Bool(&pre, "ephemeral");
        IdentityRecord rec;
        m.Create(req, &rec);
      }
    }
  }
  if (method == "Create") {
    CreateRequest req;
    req.replay = true;  // the frozen fake shape mints from the fixture table
    const std::string grade = Str(&args, "grade", "kStandard");
    if (grade != "kStandard" && grade != "kFortress") return ErrF("kMalformedInput");
    req.grade = grade == "kFortress" ? Grade::kFortress : Grade::kStandard;
    req.in_memory = Bool(&args, "ephemeral");
    IdentityRecord rec;
    if (!m.Create(req, &rec).ok) return ErrF("kFailClosed");
    return Ok(FrozenInfo(rec));
  }
  const JsonValue* id_arg = args.find("id");
  const std::string vid =
      Str((id_arg != nullptr && id_arg->is_object()) ? id_arg : nullptr,
          "value");
  if (method == "Activate" || method == "Hibernate") {
    const IdentityRecord* r = store.Find(vid);
    if (r == nullptr) return ErrF("kUnknownIdentity");
    auto res = method == "Activate" ? m.Activate(vid) : m.Hibernate(vid);
    if (!res.ok) return ErrF(res.error.substr(0, res.error.find(' ')));
    return Ok(FrozenInfo(*store.Find(vid)));
  }
  if (method == "PromoteToFortressProfile") {
    IdentityRecord rec;
    if (!m.PromoteToFortressProfile(vid, &rec).ok) {
      return ErrF("kUnknownIdentity");
    }
    return Ok(FrozenInfo(rec));
  }
  if (method == "Destroy") {
    bool verified = false;
    if (!m.Destroy(vid, &verified).ok) return ErrF("kUnknownIdentity");
    JsonValue::Object o;
    o["zero_residual_verified"] = JsonValue(verified);
    return Ok(JsonValue(std::move(o)));
  }
  if (method == "MoveTab") {
    if (Bool(&args, "after_nav")) return ErrF("kNotPermitted");
    const std::string to = Str(&args, "to");
    const IdentityRecord* r = store.Find(to);
    if (r == nullptr) return ErrF("kUnknownIdentity");
    return Ok(FrozenInfo(*r));
  }
  return ErrF("kUnknownMethod");  // unreachable (dispatch checked)
}

// ---- living surface ------------------------------------------------------

JsonValue TemplatesJson() {
  JsonValue::Array arr;
  for (const auto& t : xr::identity::AllTemplates()) {
    JsonValue::Object o;
    o["color"] = JsonValue(t.color);
    o["disposable"] = JsonValue(t.disposable);
    o["glyph"] = JsonValue(t.glyph);
    o["id"] = JsonValue(t.id);
    o["label"] = JsonValue(t.label);
    o["route"] = JsonValue(t.route);
    o["route_bound"] = JsonValue(t.route_bound);
    arr.push_back(JsonValue(std::move(o)));
  }
  JsonValue::Object out;
  out["count"] = JsonValue(static_cast<int64_t>(arr.size()));
  out["templates"] = JsonValue(std::move(arr));
  return JsonValue(std::move(out));
}

JsonValue CeremonyJson(const std::string& id) {
  auto c = xr::identity::Ceremony(id);
  if (!c) return JsonValue();  // caller rejects
  JsonValue::Array lines;
  for (const auto& l : *c) lines.push_back(JsonValue(l));
  JsonValue::Object o;
  o["lines"] = JsonValue(std::move(lines));
  o["template_id"] = JsonValue(id);
  return JsonValue(std::move(o));
}

JsonValue AuditJson(const HostState& s) {
  JsonValue::Array changes;
  for (const auto& ch : s.binding.changes()) {
    JsonValue::Object o;
    o["cause"] = JsonValue(ch.cause == ChangeCause::kWindowDefault
                               ? "window-default"
                           : ch.cause == ChangeCause::kUserConfirmedMove
                               ? "user-confirmed-move"
                           : ch.cause == ChangeCause::kRestore ? "restore"
                                                                   : "suggestion");
    o["from"] = JsonValue(ch.from);
    o["tab_id"] = JsonValue(static_cast<int64_t>(ch.tab_id));
    o["to"] = JsonValue(ch.to);
    changes.push_back(JsonValue(std::move(o)));
  }
  JsonValue::Array sugg;
  for (const auto& g : s.binding.suggestions()) {
    JsonValue::Object o;
    o["domain"] = JsonValue(g.domain);
    o["site"] = JsonValue(g.site);
    o["surfaced"] = JsonValue(g.surfaced);
    sugg.push_back(JsonValue(std::move(o)));
  }
  JsonValue::Array evict;
  for (const auto& e : s.scheduler.evictions()) {
    JsonValue::Object o;
    o["at_tick"] = JsonValue(static_cast<int64_t>(e.at_tick));
    o["domain"] = JsonValue(e.domain);
    o["reason"] = JsonValue(e.reason);
    evict.push_back(JsonValue(std::move(o)));
  }
  JsonValue::Object o;
  o["changes"] = JsonValue(std::move(changes));
  o["evictions"] = JsonValue(std::move(evict));
  o["suggestions"] = JsonValue(std::move(sugg));
  return JsonValue(std::move(o));
}

}  // namespace

// The LIVING subcommand surface (policy_host precedent): argv[1] is the
// subcommand, argv[2] (or stdin via '-') the args object. NOT part of the
// frozen {method,args} table — see the header comment.
int RunLiving(const std::string& cmd, const JsonValue& args) {
  HostState s;

  if (cmd == "flag-status") {
    JsonValue::Object o;
    o["xr_identity_v1"] = JsonValue("on");
    return Emit(JsonValue(std::move(o)));
  }
  if (cmd == "templates") {
    return Emit(TemplatesJson());
  }
  if (cmd == "ceremony") {
    const std::string id = Str(&args, "template_id");
    if (!xr::identity::FindTemplate(id)) {
      return EmitReject("unknown template '" + id + "'");
    }
    return Emit(CeremonyJson(id));
  }
  if (cmd == "provision") {
    CreateRequest req;
    req.entropy = Str(&args, "entropy");
    req.replay = Bool(&args, "replay");
    req.display_name = Str(&args, "display_name");
    req.template_id = Str(&args, "template_id");
    req.in_memory = Bool(&args, "in_memory");
    if (const JsonValue* p = args.find("prefs"); p && p->is_object()) {
      for (const auto& [k, v] : p->as_object()) {
        if (v.is_string()) req.prefs_overrides[k] = v.as_string();
      }
    }
    IdentityRecord rec;
    auto res = s.manager.Create(req, &rec);
    if (!res.ok) return EmitErr("kMalformedInput", res.error);
    if (!req.template_id.empty()) {
      std::map<std::string, std::string> rows = rec.prefs.rows;
      std::string color, glyph;
      if (!xr::identity::ApplyTemplate(req.template_id, &rows, &color,
                                       &glyph)) {
        return EmitErr("kMalformedInput", "unknown template '" +
                          req.template_id + "'");
      }
      rec.prefs.rows = rows;
      rec.color = color;
      rec.glyph = glyph;
      if (auto* live = s.store.Find(rec.domain)) {
        live->prefs.rows = rows;
        live->color = color;
        live->glyph = glyph;
      }
    }
    return Emit(RecordJson(rec));
  }
  if (cmd == "resolve-pref") {
    const std::string domain = Str(&args, "domain");
    const std::string key = Str(&args, "key");
    auto v = s.manager.ResolvePref(domain, key);
    JsonValue::Object o;
    o["key"] = JsonValue(key);
    o["value"] = v ? JsonValue(*v) : JsonValue();  // null = NO overrides
    return Emit(JsonValue(std::move(o)));
  }
  if (cmd == "activate" || cmd == "hibernate" || cmd == "wake") {
    const std::string domain = Str(&args, "domain");
    const int64_t tick = Int(&args, "tick", 1);
    auto res = cmd == "activate"
                   ? s.scheduler.Activate(domain, static_cast<uint64_t>(tick))
                   : cmd == "hibernate"
                         ? s.scheduler.Hibernate(domain,
                                                 static_cast<uint64_t>(tick))
                         : s.scheduler.Wake(domain, static_cast<uint64_t>(tick));
    if (!res.ok) return EmitReject(res.error);
    JsonValue::Object o;
    o["domain"] = JsonValue(domain);
    o["ok"] = JsonValue(true);
    o["state"] = JsonValue(StateName(s.store.Find(domain)->state));
    return Emit(JsonValue(std::move(o)));
  }
  if (cmd == "destroy") {
    const std::string domain = Str(&args, "domain");
    bool verified = false;
    auto res = s.manager.Destroy(domain, &verified);
    if (!res.ok) return EmitErr("kInternal", res.error);
    JsonValue::Object o;
    o["ok"] = JsonValue(true);
    o["zero_residual_verified"] = JsonValue(verified);
    return Emit(JsonValue(std::move(o)));
  }
  if (cmd == "plant-residual") {
    // TEST HOOK, named as such: the §1.4 negative's planter. The parity
    // corpus uses it; production code never does.
    s.store.PlantResidual(Str(&args, "domain"),
                          {Str(&args, "kind", "cookies"),
                           static_cast<size_t>(Int(&args, "bytes", 1))});
    JsonValue::Object o;
    o["ok"] = JsonValue(true);
    o["test_hook"] = JsonValue("plant-residual");
    return Emit(JsonValue(std::move(o)));
  }
  if (cmd == "bind-open") {
    const std::string opener = Str(&args, "opener");
    auto res = s.binding.OpenTab(Str(&args, "window"),
                                 static_cast<uint64_t>(Int(&args, "tab_id")),
                                 opener);
    if (!res.ok) return EmitReject(res.error);
    JsonValue::Object o;
    o["domain"] = JsonValue(*s.binding.TabIdentity(
        static_cast<uint64_t>(Int(&args, "tab_id"))));
    o["ok"] = JsonValue(true);
    return Emit(JsonValue(std::move(o)));
  }
  if (cmd == "bind-move") {
    std::map<uint64_t, std::string> peers;
    if (const JsonValue* p = args.find("group_peers"); p && p->is_object()) {
      for (const auto& [k, v] : p->as_object()) {
        if (v.is_string()) {
          peers[static_cast<uint64_t>(std::stoull(k))] = v.as_string();
        }
      }
    }
    auto res = s.binding.MoveTab(
        Str(&args, "from"), Str(&args, "to"),
        static_cast<uint64_t>(Int(&args, "tab_id")), Bool(&args, "after_nav"),
        peers);
    if (!res.ok) return EmitReject(res.error);
    JsonValue::Object o;
    o["ok"] = JsonValue(true);
    return Emit(JsonValue(std::move(o)));
  }
  if (cmd == "suggest") {
    auto res = s.binding.RecordSuggestion(Str(&args, "site"),
                                          Str(&args, "domain"));
    if (!res.ok) return EmitReject(res.error);
    JsonValue::Object o;
    o["ok"] = JsonValue(true);
    o["surfaced"] = JsonValue(true);  // shown, never applied
    return Emit(JsonValue(std::move(o)));
  }
  if (cmd == "autoswitch-probe") {
    // The planted auto-switch (never-auto-switch law, P14-T3): this probe
    // MUST be rejected. A future refactor that makes it succeed has broken
    // the law; the corpus + negatives pin the refusal.
    auto res = s.binding.TestPlantedAutoSwitch(
        static_cast<uint64_t>(Int(&args, "tab_id")), Str(&args, "to"));
    if (!res.ok) return EmitReject(res.error);
    return EmitErr("kInternal",
                   "autoswitch-probe SUCCEEDED — the never-auto-switch law "
                   "is broken; this is a red, not a feature");
  }
  if (cmd == "restore") {
    auto res = s.binding.RestoreTab(static_cast<uint64_t>(Int(&args, "tab_id")),
                                    Str(&args, "domain"));
    if (!res.ok) return EmitReject(res.error);
    JsonValue::Object o;
    o["ok"] = JsonValue(true);
    return Emit(JsonValue(std::move(o)));
  }
  if (cmd == "audit") {
    return Emit(AuditJson(s));
  }  // P14-T5 (P14-CLOSE C-2): the ledger identity overlay. The Python twin is
  // xr-core/fakes/ledger_identity.py; the byte-law is
  // docs/contracts/vectors/ledger-identity-overlay-v1.json (both backends).
  if (cmd == "ledger-tag" || cmd == "omnibox-filter" ||
      cmd == "history-oracle") {
    JsonValue out;
    std::string why;
    const bool ok = cmd == "ledger-tag"
                        ? xr::identity::TagEvent(args, &out, &why)
                        : cmd == "omnibox-filter"
                              ? xr::identity::FilterOmnibox(args, &out, &why)
                              : xr::identity::HistoryOracle(args, &out, &why);
    if (!ok) return EmitReject(why);
    return Emit(out);
  }

  if (cmd == "attribute") {
    std::vector<ProcessSample> samples;
    if (const JsonValue* sa = args.find("samples");
        sa && sa->is_array()) {
      for (const auto& sj : sa->as_array()) {
        ProcessSample ps;
        ps.pid = static_cast<uint64_t>(Int(&sj, "pid"));
        ps.rss_kb = static_cast<size_t>(Int(&sj, "rss_kb"));
        if (const JsonValue* sv = sj.find("serves");
            sv && sv->is_array()) {
          for (const auto& d : sv->as_array()) {
            if (d.is_string()) ps.serves.push_back(d.as_string());
          }
        }
        samples.push_back(std::move(ps));
      }
    }
    const size_t total = static_cast<size_t>(Int(&args, "total_kb"));
    std::vector<AttributionRow> rows;
    AttributionModel model;
    if (!model.Attribute(samples, total, &rows)) {
      return EmitReject("samples exceed the measured total — no table "
                        "(attribution unavailable, never a wrong one)");
    }
    JsonValue::Array rj;
    size_t sum = 0;
    for (const auto& r : rows) {
      JsonValue::Object o;
      o["domain"] = JsonValue(r.domain);
      o["kb"] = JsonValue(static_cast<int64_t>(r.kb));
      o["processes"] = JsonValue(static_cast<int64_t>(r.processes));
      sum += r.kb;
      rj.push_back(JsonValue(std::move(o)));
    }
    JsonValue::Object o;
    o["rows"] = JsonValue(std::move(rj));
    o["sum_kb"] = JsonValue(static_cast<int64_t>(sum));
    o["total_kb"] = JsonValue(static_cast<int64_t>(total));
    return Emit(JsonValue(std::move(o)));
  }
  if (cmd == "scenario") {
    // A deterministic op-list against ONE state (multi-step parity cases:
    // cap evictions, wake refusals, binding audit trails).
    JsonValue::Array steps;
    const JsonValue* ops = args.find("ops");
    if (ops == nullptr || !ops->is_array()) {
      return EmitErr("kMalformedInput", "scenario needs ops: [...]");
    }
    for (const auto& op : ops->as_array()) {
      const std::string name = Str(&op, "op");
      JsonValue::Object st;
      st["op"] = JsonValue(name);
      if (name == "provision") {
        CreateRequest req;
        req.entropy = Str(&op, "entropy");
        req.replay = Bool(&op, "replay");
        req.display_name = Str(&op, "display_name");
        req.template_id = Str(&op, "template_id");
        req.in_memory = Bool(&op, "in_memory");
        IdentityRecord rec;
        auto res = s.manager.Create(req, &rec);
        st["ok"] = JsonValue(res.ok);
        if (res.ok) {
          st["record"] = RecordJson(rec);
        } else {
          st["error"] = JsonValue(res.error);
        }
      } else if (name == "activate" || name == "hibernate" || name == "wake") {
        const std::string domain = Str(&op, "domain");
        const uint64_t tick = static_cast<uint64_t>(Int(&op, "tick", 1));
        auto res = name == "activate" ? s.scheduler.Activate(domain, tick)
                     : name == "hibernate"
                         ? s.scheduler.Hibernate(domain, tick)
                         : s.scheduler.Wake(domain, tick);
        st["ok"] = JsonValue(res.ok);
        if (!res.ok) st["error"] = JsonValue(res.error);
        else if (const IdentityRecord* r = s.store.Find(domain)) {
          st["state"] = JsonValue(StateName(r->state));
        }
      } else if (name == "destroy") {
        const std::string domain = Str(&op, "domain");
        bool verified = false;
        auto res = s.manager.Destroy(domain, &verified);
        st["ok"] = JsonValue(res.ok);
        st["zero_residual_verified"] = JsonValue(verified);
        if (!res.ok) st["error"] = JsonValue(res.error);
      } else if (name == "plant-residual") {
        s.store.PlantResidual(Str(&op, "domain"),
                              {Str(&op, "kind", "cookies"),
                               static_cast<size_t>(Int(&op, "bytes", 1))});
        st["ok"] = JsonValue(true);
      } else if (name == "plant-fs-leftover") {
        // TEST HOOK (P14-CLOSE C-4, T6): writes ONE real file, relative to
        // the host's cwd, so the FS-diff around a disposable cycle has a
        // planted leftover to catch. The core itself never touches the
        // filesystem (disposables are in-memory); this op is the only
        // writer and it refuses any path that could leave the cwd.
        const std::string rel = Str(&op, "path", "leftover.bin");
        const bool safe = !rel.empty() && rel.find('/') == std::string::npos &&
                          rel.find("..") == std::string::npos;
        bool wrote = false;
        if (safe) {
          if (std::FILE* f = std::fopen(rel.c_str(), "wb")) {
            const std::string bytes(static_cast<size_t>(Int(&op, "bytes", 1)), 'x');
            wrote = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
            wrote = (std::fclose(f) == 0) && wrote;
          }
        }
        st["ok"] = JsonValue(wrote);
        st["test_hook"] = JsonValue("plant-fs-leftover");
        if (!wrote) st["error"] = JsonValue(safe ? "kInternal" : "kRejected");
      } else if (name == "suggest") {
        auto res = s.binding.RecordSuggestion(Str(&op, "site"),
                                              Str(&op, "domain"));
        st["ok"] = JsonValue(res.ok);
        if (!res.ok) st["error"] = JsonValue(res.error);
      } else if (name == "autoswitch") {
        auto res = s.binding.TestPlantedAutoSwitch(
            static_cast<uint64_t>(Int(&op, "tab_id")), Str(&op, "to"));
        st["ok"] = JsonValue(res.ok);  // MUST stay false (the law)
        if (!res.ok) st["error"] = JsonValue(res.error);
      } else {
        st["error"] = JsonValue("kUnknownMethod");
        st["ok"] = JsonValue(false);
      }
      steps.push_back(JsonValue(std::move(st)));
    }
    JsonValue::Object out;
    out["final"] = AuditJson(s);
    out["steps"] = JsonValue(std::move(steps));
    return Emit(JsonValue(std::move(out)));
  }

  JsonValue::Object o;
  o["error"] = JsonValue("kUnknownMethod");
  o["detail"] = JsonValue("unknown subcommand '" + cmd + "'");
  Emit(JsonValue(std::move(o)));
  return 1;
}

int main(int argc, char** argv) {
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) positional.push_back(argv[i]);

  // Subcommand form (the LIVING surface): identity_host <cmd> ['<args>'].
  if (!positional.empty() && positional[0].rfind("{", 0) != 0 &&
      positional[0] != "Create" && positional[0] != "Activate" &&
      positional[0] != "Hibernate" &&
      positional[0] != "PromoteToFortressProfile" &&
      positional[0] != "Destroy" && positional[0] != "MoveTab") {
    std::string args_text = positional.size() >= 2 ? positional[1] : "{}";
    if (args_text == "-") args_text = ReadAllStdin();
    JsonParseResult apr = ParseJson(args_text);
    if (!apr.ok || !apr.value.is_object()) {
      return EmitErr("kMalformedInput", "args must be a JSON object");
    }
    return RunLiving(positional[0], apr.value);
  }

  // {method,args} JSON form (the FROZEN surface): positional method name,
  // json-with-method, or request on stdin.
  std::string method, args_text;
  if (positional.empty()) {
    const std::string req = ReadAllStdin();
    JsonParseResult pr = ParseJson(req);
    if (!pr.ok || !pr.value.is_object()) {
      return EmitErr("kMalformedInput", "stdin request must be a JSON object");
    }
    const JsonValue* m = pr.value.find("method");
    const JsonValue* a = pr.value.find("args");
    if (m == nullptr || !m->is_string()) {
      return EmitErr("kMalformedInput", "request missing 'method'");
    }
    method = m->as_string();
    args_text = a ? a->Canonical() : "{}";
  } else if (positional[0].rfind("{", 0) == 0) {
    JsonParseResult pr = ParseJson(positional[0]);
    if (!pr.ok || !pr.value.is_object()) {
      return EmitErr("kMalformedInput", "json-with-method must be an object");
    }
    const JsonValue* m = pr.value.find("method");
    const JsonValue* a = pr.value.find("args");
    if (m == nullptr || !m->is_string()) {
      return EmitErr("kMalformedInput", "json-with-method missing 'method'");
    }
    method = m->as_string();
    args_text = a ? a->Canonical() : "{}";
  } else {
    method = positional[0];
    args_text = positional.size() >= 2 ? positional[1] : "{}";
  }
  JsonParseResult apr = ParseJson(args_text);
  if (!apr.ok || !apr.value.is_object()) {
    return EmitErr("kMalformedInput", "args must be a JSON object");
  }
  const JsonValue& args = apr.value;
  if (method == "Create" || method == "Activate" || method == "Hibernate" ||
      method == "PromoteToFortressProfile" || method == "Destroy" ||
      method == "MoveTab") {
    return Emit(FrozenCall(method, args));
  }
  JsonValue::Object o;
  o["error"] = JsonValue("kUnknownMethod");
  o["detail"] = JsonValue("unknown method '" + method + "'");
  Emit(JsonValue(std::move(o)));
  return 1;
}
