// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — shield_host: the JSON-over-stdio façade for the shield
// decision core (P11-T2). SAME protocol conventions as update_host
// (fakes/README.md + shield/host_protocol.md): one JSON request on
// argv[1..2] or stdin, one canonical JSON line on stdout (sorted keys,
// \uXXXX non-ASCII), exit 0 ok/typed-rejection · 1 typed error · 2 usage.
// No exceptions cross this boundary. No clock, no RNG, no environment:
// every decision input (bundle, state, scopes, ring, posture flags,
// now_mono) rides in the request — the golden vectors are deterministic
// because the host is.
//
// TWO output vocabularies, by design:
//   * the FROZEN mojom surface (Status, RecentEvents — mojom/shield.mojom,
//     kContractVersion=1) keeps the P5 fake envelope {"ok":…}/{"error":…}
//     and the frozen enum spellings (kBlocked, kScript). The frozen
//     fixture case (fakes/fixtures/shield-v1.json) passes byte-identical
//     against this host and against fakes/shield.py.
//   * the LIVING host surface (flag-status, bundle-load, bundle-check,
//     match, posture, apply) uses the update-host conventions: bare
//     canonical result objects, {"error","detail"} typed errors (exit 1),
//     {"error":"kRejected","reason"} typed rejections (exit 0), lowercase
//     living enum names. shield/host_protocol.md is the living contract.
//
// Methods (table + arg shapes in shield/host_protocol.md):
//   flag-status {}                          -> {"xr_shield_v1":"on"|"off"}
//   Status {identity,origin,ring?}          -> {"ok":{enabled,blocked_count}}
//   RecentEvents {identity,max_events,ring?}-> {"ok":[BlockEvent…]}
//   bundle-load {bundle}                    -> summary{name,version,digest,…}
//   bundle-check {bundle,manifest}          -> {"bound":true,…} | kRejected
//   match {context,bundle?,scopes?,…}       -> {fail_closed,posture,verdict}
//   posture {engine_alive?,…}               -> {mode,chip,reason}
//   apply {bundle,state,now_mono}           -> {"state":…} | kRejected
//
// The engine binding here is the TEST-ONLY TableEngine (fake_engine.h —
// the v1 reference matcher). The vendored adblock-rust binding lands
// behind the same BlockingEngine interface via the engine/ FFI shim; T8's
// parity job measures the two against each other. This host never claims
// the Rust engine runs.
#include <cstdio>
#include <string>
#include <vector>

#include "common/core/json.h"
#include "shield/core/apply.h"
#include "shield/core/bundle.h"
#include "shield/core/context.h"
#include "shield/core/engine.h"
#include "shield/core/events.h"
#include "shield/core/fake_engine.h"
#include "shield/core/match.h"
#include "shield/core/posture.h"
#include "shield/core/scope.h"

namespace {

using xr::common::JsonParseResult;
using xr::common::JsonValue;
using xr::common::ParseJson;
using namespace xr::shield;

void Emit(const JsonValue& v) {
  std::printf("%s\n", v.Canonical().c_str());
}

// Typed error (exit 1) — living surface shape, mirrors update_host.
void EmitError(const char* code, const std::string& detail) {
  Emit(JsonValue(JsonValue::Object{
      {"detail", JsonValue(detail)},
      {"error", JsonValue(std::string(code))},
  }));
}

// Typed rejection (exit 0) — a legitimate refused outcome, not a protocol
// error (update_host precedent).
void EmitReject(const std::string& reason) {
  Emit(JsonValue(JsonValue::Object{
      {"error", JsonValue("kRejected")},
      {"reason", JsonValue(reason)},
  }));
}

// Frozen-envelope error (exit 0): {"error":"<ErrorCode>"} — the P5 fake
// shape, byte-identical to fakes/_base.py err().
void EmitFrozenError(const char* code) {
  Emit(JsonValue(JsonValue::Object{{"error", JsonValue(std::string(code))}}));
}

// ---- P11-T4: exception-surface plumbing ----------------------------------
// Parse the optional `scopes` arg (absent = empty set) with the T2 grammar.
// ANY non-kOk ScopeResult maps to kMalformedInput (exit 1) — the match
// method's law, unchanged by T4.
bool ParseScopesArg(const JsonValue& args, ScopeSet* out,
                    std::string* detail) {
  const JsonValue* sv = args.find("scopes");
  if (sv == nullptr) return true;
  JsonValue wrapper(JsonValue::Object{{"scopes", *sv}});
  return ParseScopeSet(wrapper, out, detail) == ScopeResult::kOk;
}

// Canonical wire form of a resulting set: {"scopes":[ScopeToJson...]}.
JsonValue ScopesOut(const ScopeSet& set) {
  JsonValue::Array arr;
  for (const auto& s : set.scopes) arr.push_back(ScopeToJson(s));
  return JsonValue(arr);
}

std::string ReadAllStdin() {
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) data.append(buf, n);
  return data;
}

int Usage() {
  std::fprintf(stderr,
               "usage: shield_host <method> ['<json-args>'] [options]\n"
               "       shield_host '<json-with-method>' [options]\n"
               "       shield_host [options]   # request JSON on stdin\n"
               "options: --flag xr_shield_v1=on|off   (default on)\n"
               "methods: flag-status Status RecentEvents bundle-load\n"
               "         bundle-check match posture apply\n");
  return 2;
}

// Strict key allowlist (house law: unknown keys are refusals, never
// silently dropped). Returns false and names the offender.
bool OnlyKeys(const JsonValue& obj, std::vector<const char*> keys,
              std::string* bad) {
  for (const auto& kv : obj.as_object()) {
    bool ok = false;
    for (const char* k : keys) {
      if (kv.first == k) { ok = true; break; }
    }
    if (!ok) { *bad = kv.first; return false; }
  }
  return true;
}

// The four posture booleans, all optional with the normal-posture
// defaults (living arg shape; documented in host_protocol.md).
bool ParsePostureArgs(const JsonValue& args, PostureInputs* in,
                      std::string* detail) {
  *in = PostureInputs{};
  struct { const char* key; bool* tgt; } map[] = {
      {"engine_alive", &in->engine_alive},
      {"engine_poisoned", &in->engine_poisoned},
      {"route_bound", &in->route_bound},
      {"kill_switch_on", &in->kill_switch_on},
  };
  for (auto& m : map) {
    const JsonValue* v = args.find(m.key);
    if (v == nullptr) continue;
    if (!v->is_bool()) {
      *detail = std::string("field-not-bool:") + m.key;
      return false;
    }
    *m.tgt = v->as_bool();
  }
  return true;
}

// Frozen list-bundle-manifest-v1 document -> the binding entries. The
// manifest SCHEMA is frozen and validated by the T3 pipeline; here we
// parse exactly the fields the binding needs and refuse anything else.
bool ParseManifestLists(const JsonValue& man,
                        std::vector<ManifestListEntry>* out,
                        std::string* detail) {
  if (!man.is_object()) {
    *detail = "manifest-not-object";
    return false;
  }
  std::string bad;
  if (!OnlyKeys(man, {"schema_version", "bundle_id", "created_epoch", "lists",
                      "key_pin", "last_known_good_bundle_id"}, &bad)) {
    *detail = "unknown-field:" + bad;
    return false;
  }
  const JsonValue* lists = man.find("lists");
  if (lists == nullptr || !lists->is_array()) {
    *detail = "manifest-lists-not-array";
    return false;
  }
  for (const JsonValue& lv : lists->as_array()) {
    if (!lv.is_object() ||
        !OnlyKeys(lv, {"name", "sha256", "rules", "attribution"}, &bad)) {
      *detail = lv.is_object() ? "unknown-field:" + bad : "manifest-entry-not-object";
      return false;
    }
    ManifestListEntry e;
    const JsonValue* n = lv.find("name");
    const JsonValue* s = lv.find("sha256");
    const JsonValue* r = lv.find("rules");
    if (n == nullptr || !n->is_string() || n->as_string().empty() ||
        s == nullptr || !s->is_string() || s->as_string().size() != 64 ||
        r == nullptr || !r->is_int() || r->as_int() < 0) {
      *detail = "bad-manifest-entry";
      return false;
    }
    e.name = n->as_string();
    e.sha256 = s->as_string();
    e.rules = r->as_int();
    const JsonValue* a = lv.find("attribution");
    if (a != nullptr) {
      if (!a->is_string() || a->as_string().empty()) {
        *detail = "bad-manifest-entry";
        return false;
      }
      e.has_attribution = true;
      e.attribution = a->as_string();
    }
    out->push_back(std::move(e));
  }
  return true;
}

// Optional "ring" helper key (test affordance per fakes/README.md — the
// host is stateless in v1; the ring rides in the request).
bool ParseRingArg(const JsonValue& args, EventRing* ring, bool* present,
                  std::string* detail) {
  *present = false;
  const JsonValue* rv = args.find("ring");
  if (rv == nullptr) return true;
  *present = true;
  return ParseRing(*rv, ring, detail);
}

// Parse a bundle argument into a NormalizedBundle. Returns 0 = ok,
// 1 = typed error already emitted, 2 = rejection already emitted.
int LoadBundleArg(const JsonValue& args, NormalizedBundle* bundle) {
  const JsonValue* bv = args.find("bundle");
  if (bv == nullptr) {
    EmitError("kMalformedInput", "missing-bundle");
    return 1;
  }
  std::string detail;
  BundleResult br = ParseBundle(*bv, bundle, &detail);
  switch (br) {
    case BundleResult::kOk:
      return 0;
    case BundleResult::kUnsupportedDirective:
    case BundleResult::kManifestMismatch:
      EmitReject(detail);  // content refusal: legitimate refused outcome
      return 2;
    case BundleResult::kMalformed:
    case BundleResult::kUnknownField:
      EmitError("kMalformedInput", detail);
      return 1;
  }
  EmitError("kMalformedInput", "bundle-parse-failed");
  return 1;
}

// ---- frozen mojom surface ------------------------------------------------

int MethodStatus(const JsonValue& args, const std::string& flag) {
  std::string bad;
  if (!OnlyKeys(args, {"identity", "origin", "ring"}, &bad)) {
    EmitFrozenError("kMalformedInput");
    return 0;
  }
  // frozen law: unknown identity first, then unknown origin
  const JsonValue* idv = args.find("identity");
  if (idv == nullptr || !idv->is_object()) {
    EmitFrozenError("kUnknownIdentity");
    return 0;
  }
  const JsonValue* val = idv->find("value");
  if (val == nullptr || !val->is_string() || val->as_string().empty()) {
    EmitFrozenError("kUnknownIdentity");
    return 0;
  }
  const JsonValue* org = args.find("origin");
  std::string scheme;
  if (org != nullptr && org->is_object()) {
    const JsonValue* sc = org->find("scheme");
    if (sc != nullptr && sc->is_string()) scheme = sc->as_string();
  }
  if (scheme != "http" && scheme != "https") {
    EmitFrozenError("kUnknownOrigin");
    return 0;
  }
  // living extension (helper key): blocked_count over an explicit ring;
  // absent ring => 0, byte-identical to the frozen fake
  EventRing ring;
  bool have_ring = false;
  std::string detail;
  if (!ParseRingArg(args, &ring, &have_ring, &detail)) {
    EmitFrozenError("kMalformedInput");
    return 0;
  }
  int blocked = 0;
  if (have_ring) {
    for (const auto& e : ring.events) {
      if (e.identity == val->as_string() && e.action == BlockAction::kBlocked)
        ++blocked;
    }
  }
  Emit(JsonValue(JsonValue::Object{
      {"ok", JsonValue(JsonValue::Object{
                 {"blocked_count", JsonValue(blocked)},
                 {"enabled", JsonValue(flag == "on")},
             })}}));
  return 0;
}

int MethodRecentEvents(const JsonValue& args) {
  std::string bad;
  if (!OnlyKeys(args, {"identity", "max_events", "ring"}, &bad)) {
    EmitFrozenError("kMalformedInput");
    return 0;
  }
  const JsonValue* idv = args.find("identity");
  if (idv == nullptr || !idv->is_object()) {
    EmitFrozenError("kMalformedInput");
    return 0;
  }
  const JsonValue* val = idv->find("value");
  if (val == nullptr || !val->is_string() || val->as_string().empty()) {
    EmitFrozenError("kMalformedInput");
    return 0;
  }
  int max_events = 0;
  const JsonValue* mx = args.find("max_events");
  if (mx != nullptr) {
    if (!mx->is_int()) {
      EmitFrozenError("kMalformedInput");
      return 0;
    }
    max_events = mx->as_int();
  }
  EventRing ring;
  bool have_ring = false;
  std::string detail;
  if (!ParseRingArg(args, &ring, &have_ring, &detail)) {
    EmitFrozenError("kMalformedInput");
    return 0;
  }
  JsonValue::Array events;
  if (have_ring) {
    for (auto& e : RingView(ring, val->as_string(), max_events))
      events.push_back(std::move(e));
  } else {
    // frozen affordance: the deterministic fixed sample (no clock), the
    // identity echoed exactly as received — fakes/shield.py parity
    int n = max_events < 0 ? 0 : (max_events < 1 ? max_events : 1);
    for (int i = 0; i < n; ++i) {
      events.push_back(JsonValue(JsonValue::Object{
          {"action", JsonValue("kBlocked")},
          {"identity", *idv},
          {"list_provenance", JsonValue("xr-default-list-v1")},
          {"origin", JsonValue(JsonValue::Object{
                         {"registrable_domain", JsonValue("tracker.example")},
                         {"scheme", JsonValue("https")},
                     })},
          {"request_class", JsonValue("kScript")},
          {"rule", JsonValue("||tracker.example^")},
          {"tab_id", JsonValue(1)},
          {"target", JsonValue("https://tracker.example/a.js")},
          {"ts_millis", JsonValue(1000)},
      }));
    }
  }
  Emit(JsonValue(
      JsonValue::Object{{"ok", JsonValue(events)}}));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string flag = "on";
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--flag" && i + 1 < argc) {
      std::string kv = argv[++i];
      auto eq = kv.find('=');
      if (eq == std::string::npos) {
        std::fprintf(stderr, "usage error: --flag k=v\n");
        return 2;
      }
      if (kv.substr(0, eq) == "xr_shield_v1") flag = kv.substr(eq + 1);
      else {
        std::fprintf(stderr, "usage error: unknown flag %s\n",
                     kv.substr(0, eq).c_str());
        return 2;
      }
    } else if (a.rfind("--", 0) == 0) {
      // no --store-dir in v1: the host is stateless (every state rides in
      // the request); T5's ledger persistence adds it by contract change
      std::fprintf(stderr, "usage error: unknown option %s\n", a.c_str());
      return 2;
    } else {
      positional.push_back(a);
    }
  }
  if (flag != "on" && flag != "off") {
    std::fprintf(stderr, "usage error: xr_shield_v1 must be on|off\n");
    return 2;
  }

  std::string method;
  std::string args_json = "{}";
  if (positional.empty()) {
    const std::string raw = ReadAllStdin();
    JsonParseResult p = ParseJson(raw);
    if (!p.ok) {
      EmitError("kMalformedInput", "bad stdin frame");
      return 1;
    }
    const JsonValue* m = p.value.find("method");
    const JsonValue* a = p.value.find("args");
    method = (m && m->is_string()) ? m->as_string() : "";
    args_json = a ? a->Canonical() : std::string("{}");
  } else {
    JsonParseResult p = ParseJson(positional[0]);
    if (p.ok && p.value.is_object() && p.value.find("method")) {
      const JsonValue* m = p.value.find("method");
      const JsonValue* a = p.value.find("args");
      method = (m && m->is_string()) ? m->as_string() : "";
      args_json = a ? a->Canonical() : std::string("{}");
    } else if (positional.size() == 1 || positional.size() == 2) {
      method = positional[0];
      if (positional.size() == 2) args_json = positional[1];
    } else {
      return Usage();
    }
  }
  if (method.empty()) return Usage();

  JsonParseResult ap = ParseJson(args_json);
  if (!ap.ok || !ap.value.is_object()) {
    EmitError("kMalformedInput", "args not an object");
    return 1;
  }
  const JsonValue& args = ap.value;
  std::string bad;

  if (method == "flag-status") {
    if (!OnlyKeys(args, {}, &bad)) {
      EmitError("kMalformedInput", "unknown-field:" + bad);
      return 1;
    }
    Emit(JsonValue(JsonValue::Object{{"xr_shield_v1", JsonValue(flag)}}));
    return 0;
  }

  if (method == "Status") return MethodStatus(args, flag);
  if (method == "RecentEvents") return MethodRecentEvents(args);

  if (method == "posture") {
    if (!OnlyKeys(args, {"engine_alive", "engine_poisoned", "route_bound",
                         "kill_switch_on"}, &bad)) {
      EmitError("kMalformedInput", "unknown-field:" + bad);
      return 1;
    }
    PostureInputs in;
    std::string detail;
    if (!ParsePostureArgs(args, &in, &detail)) {
      EmitError("kMalformedInput", detail);
      return 1;
    }
    Posture p = DecidePosture(in);
    Emit(JsonValue(JsonValue::Object{
        {"chip", JsonValue(ChipName(p.chip))},
        {"mode", JsonValue(PostureModeName(p.mode))},
        {"reason", JsonValue(p.reason)},
    }));
    return 0;
  }

  if (method == "bundle-load") {
    if (!OnlyKeys(args, {"bundle"}, &bad)) {
      EmitError("kMalformedInput", "unknown-field:" + bad);
      return 1;
    }
    NormalizedBundle bundle;
    int rc = LoadBundleArg(args, &bundle);
    if (rc != 0) return rc == 2 ? 0 : 1;
    Emit(BundleSummaryJson(bundle));
    return 0;
  }

  if (method == "bundle-check") {
    if (!OnlyKeys(args, {"bundle", "manifest"}, &bad)) {
      EmitError("kMalformedInput", "unknown-field:" + bad);
      return 1;
    }
    NormalizedBundle bundle;
    int rc = LoadBundleArg(args, &bundle);
    if (rc != 0) return rc == 2 ? 0 : 1;
    const JsonValue* mv = args.find("manifest");
    if (mv == nullptr) {
      EmitError("kMalformedInput", "missing-manifest");
      return 1;
    }
    std::vector<ManifestListEntry> man;
    std::string detail;
    if (!ParseManifestLists(*mv, &man, &detail)) {
      EmitError("kMalformedInput", detail);
      return 1;
    }
    BundleResult br = CheckAgainstManifest(bundle, man, &detail);
    if (br != BundleResult::kOk) {
      EmitReject(detail);  // a binding failure is a refused outcome
      return 0;
    }
    Emit(JsonValue(JsonValue::Object{
        {"bound", JsonValue(true)},
        {"bundle_version", JsonValue(static_cast<int>(bundle.bundle_version))},
        {"digest", JsonValue(bundle.digest)},
        {"list_count", JsonValue(static_cast<int>(man.size()))},
    }));
    return 0;
  }

  if (method == "match") {
    if (!OnlyKeys(args, {"context", "bundle", "scopes", "engine_alive",
                         "engine_poisoned", "route_bound", "kill_switch_on",
                         "now_mono"}, &bad)) {
      EmitError("kMalformedInput", "unknown-field:" + bad);
      return 1;
    }
    const JsonValue* cv = args.find("context");
    if (cv == nullptr) {
      EmitError("kMalformedInput", "missing-context");
      return 1;
    }
    RequestContext ctx;
    std::string detail;
    ParseResult pr = ParseRequestContext(*cv, &ctx, &detail);
    if (pr != ParseResult::kOk) {
      EmitError("kMalformedInput", detail);
      return 1;
    }
    // bundle is optional (no-bundle is a legal deciding state)
    NormalizedBundle bundle;
    bool have_bundle = args.find("bundle") != nullptr;
    if (have_bundle) {
      int rc = LoadBundleArg(args, &bundle);
      if (rc != 0) return rc == 2 ? 0 : 1;
    }
    ScopeSet scopes;
    if (const JsonValue* sv = args.find("scopes")) {
      JsonValue wrapper(JsonValue::Object{{"scopes", *sv}});
      ScopeResult sr = ParseScopeSet(wrapper, &scopes, &detail);
      if (sr != ScopeResult::kOk) {
        EmitError("kMalformedInput", detail);
        return 1;
      }
    }
    PostureInputs pin;
    if (!ParsePostureArgs(args, &pin, &detail)) {
      EmitError("kMalformedInput", detail);
      return 1;
    }
    long long now_mono = 0;
    if (const JsonValue* nm = args.find("now_mono")) {
      if (!nm->is_int() || nm->as_int() < 0) {
        EmitError("kMalformedInput", "bad-now-mono");
        return 1;
      }
      now_mono = nm->as_int();
    }
    // the TEST-ONLY engine binding (see file header): alive/poisoned ride
    // in the posture args so vectors drive engine death deterministically
    TableEngine engine(have_bundle ? &bundle : nullptr, pin.engine_alive,
                       pin.engine_poisoned);
    MatchOutcome out =
        DecideMatch(ctx, have_bundle ? &bundle : nullptr, &engine, scopes,
                    now_mono, pin);
    Emit(JsonValue(JsonValue::Object{
        {"fail_closed", JsonValue(out.fail_closed)},
        {"posture", JsonValue(JsonValue::Object{
                        {"chip", JsonValue(ChipName(out.posture.chip))},
                        {"mode", JsonValue(PostureModeName(out.posture.mode))},
                        {"reason", JsonValue(out.posture.reason)},
                    })},
        {"verdict", VerdictToJson(out.verdict)},
    }));
    return 0;
  }

  if (method == "apply") {
    if (!OnlyKeys(args, {"bundle", "state", "now_mono"}, &bad)) {
      EmitError("kMalformedInput", "unknown-field:" + bad);
      return 1;
    }
    NormalizedBundle bundle;
    int rc = LoadBundleArg(args, &bundle);
    if (rc != 0) return rc == 2 ? 0 : 1;
    const JsonValue* stv = args.find("state");
    if (stv == nullptr) {
      EmitError("kMalformedInput", "missing-state");
      return 1;
    }
    ApplyState state;
    std::string detail;
    JsonValue state_wrapper(JsonValue::Object{{"state", *stv}});
    if (!ParseApplyState(state_wrapper, &state, &detail)) {
      EmitError("kMalformedInput", detail);
      return 1;
    }
    if (StateInvariant inv = CheckInvariants(state, &detail);
        inv != StateInvariant::kOk) {
      EmitReject(detail);  // hot-pin-out and friends: refused, not repaired
      return 0;
    }
    const JsonValue* nm = args.find("now_mono");
    if (nm == nullptr || !nm->is_int() || nm->as_int() < 0) {
      EmitError("kMalformedInput", "missing-now-mono");
      return 1;
    }
    ApplyState next;
    ApplyResult ar = ApplyBundle(bundle, state, nm->as_int(), &next, &detail);
    if (ar != ApplyResult::kOk) {
      EmitReject(detail);
      return 0;
    }
    Emit(JsonValue(
        JsonValue::Object{{"state", ApplyStateToJson(next)}}));
    return 0;
  }

  // ---- P11-T4: the exception surface (scope.h toggle mechanics) ----------
  // State rides in the request (v1 host is stateless): the scope set is an
  // arg, the resulting set is the response. Per-site toggle =
  // exception-add/remove of the canonical scope_id "site-toggle:<site>"
  // (fixed ledger-friendly reason); dynamic rule add/remove =
  // exception-add/remove of rule_id/list_id-scoped scopes; expiry sweep is
  // the deterministic core job (SweepAsOf) as a method — the as-of is the
  // now_mono ARG, never a wall clock. Refusal split: scope-document parse
  // errors stay kMalformedInput (exit 1, T2 law untouched); CONTENT
  // conflicts with the existing set are kRejected (exit 0) — re-presenting
  // an equal offer is a refusal (equal-reoffer precedent).

  if (method == "exception-add") {
    if (!OnlyKeys(args, {"scopes", "scope"}, &bad)) {
      EmitError("kMalformedInput", "unknown-field:" + bad);
      return 1;
    }
    ScopeSet set;
    std::string detail;
    if (!ParseScopesArg(args, &set, &detail)) {
      EmitError("kMalformedInput", detail);
      return 1;
    }
    const JsonValue* sc = args.find("scope");
    if (sc == nullptr) {
      EmitError("kMalformedInput", "missing-scope");
      return 1;
    }
    JsonValue::Array one_arr;
    one_arr.push_back(*sc);
    JsonValue wrapper(JsonValue::Object{{"scopes", JsonValue(one_arr)}});
    ScopeSet one;
    ScopeResult sr = ParseScopeSet(wrapper, &one, &detail);
    if (sr != ScopeResult::kOk) {
      EmitError("kMalformedInput", detail);  // bad-scope-id / missing-reason /
      return 1;                              // field-not-string / bad-expiry
    }
    for (const auto& prev : set.scopes) {
      if (prev.scope_id == one.scopes[0].scope_id) {
        EmitReject("duplicate-scope-id:" + prev.scope_id);
        return 0;
      }
    }
    set.scopes.push_back(one.scopes[0]);
    Emit(JsonValue(JsonValue::Object{{"scopes", ScopesOut(set)}}));
    return 0;
  }

  if (method == "exception-remove") {
    if (!OnlyKeys(args, {"scopes", "scope_id"}, &bad)) {
      EmitError("kMalformedInput", "unknown-field:" + bad);
      return 1;
    }
    ScopeSet set;
    std::string detail;
    if (!ParseScopesArg(args, &set, &detail)) {
      EmitError("kMalformedInput", detail);
      return 1;
    }
    const JsonValue* idv = args.find("scope_id");
    if (idv == nullptr || !idv->is_string() || idv->as_string().empty()) {
      EmitError("kMalformedInput", "bad-scope-id");
      return 1;
    }
    ScopeSet kept;
    bool found = false;
    for (auto& s : set.scopes) {
      if (s.scope_id == idv->as_string()) {
        found = true;
        continue;
      }
      kept.scopes.push_back(std::move(s));
    }
    if (!found) {
      EmitReject("unknown-scope-id:" + idv->as_string());
      return 0;
    }
    Emit(JsonValue(JsonValue::Object{{"scopes", ScopesOut(kept)}}));
    return 0;
  }

  if (method == "exception-sweep") {
    if (!OnlyKeys(args, {"scopes", "now_mono"}, &bad)) {
      EmitError("kMalformedInput", "unknown-field:" + bad);
      return 1;
    }
    ScopeSet set;
    std::string detail;
    if (!ParseScopesArg(args, &set, &detail)) {
      EmitError("kMalformedInput", detail);
      return 1;
    }
    const JsonValue* nm = args.find("now_mono");
    if (nm == nullptr || !nm->is_int() || nm->as_int() < 0) {
      EmitError("kMalformedInput", "missing-now-mono");  // apply's token:
      return 1;                                          // REQUIRED and >= 0
    }
    SweepResult sr = SweepAsOf(set, nm->as_int());
    ScopeSet kept;
    for (auto& s : set.scopes) {
      bool expired = false;
      for (const auto& id : sr.expired_ids) {
        if (id == s.scope_id) expired = true;
      }
      if (!expired) kept.scopes.push_back(std::move(s));
    }
    JsonValue::Array swept;
    for (const auto& id : sr.expired_ids) swept.push_back(JsonValue(id));
    Emit(JsonValue(JsonValue::Object{{"scopes", ScopesOut(kept)},
                                     {"swept", JsonValue(swept)}}));
    return 0;
  }

  if (method == "site-toggle") {
    if (!OnlyKeys(args, {"scopes", "site", "on", "expiry_mono"}, &bad)) {
      EmitError("kMalformedInput", "unknown-field:" + bad);
      return 1;
    }
    ScopeSet set;
    std::string detail;
    if (!ParseScopesArg(args, &set, &detail)) {
      EmitError("kMalformedInput", detail);
      return 1;
    }
    const JsonValue* site = args.find("site");
    if (site == nullptr || !site->is_string() || site->as_string().empty()) {
      EmitError("kMalformedInput", "bad-site");
      return 1;
    }
    const JsonValue* on = args.find("on");
    if (on == nullptr || !on->is_bool()) {
      EmitError("kMalformedInput", "bad-toggle");
      return 1;
    }
    long long expiry = -1;
    if (const JsonValue* ex = args.find("expiry_mono")) {
      if (!ex->is_int() || ex->as_int() < -1) {
        EmitError("kMalformedInput", "bad-expiry");
        return 1;
      }
      expiry = ex->as_int();
    }
    const std::string sid = "site-toggle:" + site->as_string();
    size_t at = 0;
    bool found = false;
    for (size_t i = 0; i < set.scopes.size(); ++i) {
      if (set.scopes[i].scope_id == sid) {
        at = i;
        found = true;
        break;
      }
    }
    if (on->as_bool()) {
      if (found) {
        EmitReject("toggle-already-on:" + site->as_string());
        return 0;
      }
      ExceptionScope s;
      s.scope_id = sid;
      s.site = site->as_string();
      s.reason = "user-site-toggle";  // fixed: ledger rows carry it verbatim
      s.expiry_mono = expiry;
      set.scopes.push_back(std::move(s));
    } else {
      if (!found) {
        EmitReject("toggle-already-off:" + site->as_string());
        return 0;
      }
      set.scopes.erase(set.scopes.begin() + static_cast<long>(at));
    }
    Emit(JsonValue(JsonValue::Object{
        {"scopes", ScopesOut(set)},
        {"scope_id", JsonValue(sid)},
        {"toggled", JsonValue(on->as_bool() ? "on" : "off")}}));
    return 0;
  }

  EmitError("kUnknownMethod", method);
  return 1;
}
