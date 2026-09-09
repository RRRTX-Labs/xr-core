// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// settings_host — stdio JSON façade for the settings core (Plan P8-T1).
// Protocol doc: settings/host_protocol.md. Speaks the same canonical-JSON
// conventions as commands_host so both hosts are interchangeable for P13+:
// every result is ONE canonical JSON line (sorted keys, compact separators,
// non-ASCII \uXXXX); exit 0 = typed result, 1 = typed error, 2 = usage.
//
// Forms:
//   settings_host <method> ['<json-args>'] [options]
//   settings_host '<json-with-method>' [options]
//   settings_host [options]           (request JSON on stdin)
// Options:
//   --store-dir DIR   store dir for the counters ledger (empty/absent =>
//                     DISPOSABLE: counters in memory, zero bytes written)
//   --schema PATH     settings_schema_v1.json (default compiled-in path)
//   --state PATH      xr-settings-state-v1 doc (the PINNED P6 policy state
//                     the views display; absent => all-defaults, no active
//                     identity — deny-safe)
//   --flag xr_settings_v0=on|off   (default on)
//
// Security laws honored: settings display the policy truth they are GIVEN
// (state doc pinned per run — never a live Resolve()); set() refuses
// policy-owned rows with a typed reason (enterprise preemption, P6-T7); no
// sockets, no network, no clock finer than day (counters); flag off =>
// nothing renders/writes and every data method returns the typed flag-off
// refusal.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "settings/core/counters.h"
#include "settings/core/router.h"
#include "settings/core/search.h"
#include "settings/core/sections.h"
#include "settings/core/settings_schema.h"

#ifndef XR_DEFAULT_SETTINGS_SCHEMA
#define XR_DEFAULT_SETTINGS_SCHEMA "settings/core/settings_schema_v1.json"
#endif

namespace {

using xr::settings::CounterStore;
using xr::settings::JsonParseResult;
using xr::settings::JsonValue;
using xr::settings::ParseJson;
using xr::settings::PolicyState;
using xr::settings::SettingsSchema;

struct Context {
  SettingsSchema schema;
  PolicyState state;
  CounterStore counters;
  std::string flag = "on";   // xr_settings_v0
};

std::string ReadAll(const std::string& path, bool* ok) {
  *ok = false;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return {};
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  std::fclose(f);
  *ok = true;
  return data;
}

std::string ReadAllStdin() {
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) data.append(buf, n);
  return data;
}

JsonValue Err(const char* code, const std::string& detail) {
  JsonValue::Object o;
  o["error"] = JsonValue(std::string(code));
  if (!detail.empty()) o["detail"] = JsonValue(detail);
  return JsonValue(std::move(o));
}

// Canonical single-line emit (byte-stable across both backends).
void Emit(const JsonValue& v) {
  std::printf("%s\n", v.Canonical().c_str());
}

bool IsMethod(const std::string& s) {
  static const char* kMethods[] = {"flag-status", "sections",    "search",
                                   "get",         "set",        "router-resolve",
                                   "counters-dump", "schema-dump"};
  for (const char* m : kMethods) {
    if (s == m) return true;
  }
  return false;
}

std::string LoadContext(Context* ctx, const std::string& store_dir,
                        const std::string& schema_path,
                        const std::string& state_path, std::string* err) {
  bool ok = false;
  const std::string schema_text = ReadAll(schema_path, &ok);
  if (!ok) {
    *err = "schema file not readable: " + schema_path;
    return "kStoreError";
  }
  auto load = ctx->schema.Load(schema_text);
  if (!load.ok) {
    *err = "settings schema rejected (strict): " + load.error;
    return "kStoreError";
  }
  if (!state_path.empty()) {
    const std::string state_text = ReadAll(state_path, &ok);
    if (!ok) {
      *err = "state file not readable: " + state_path;
      return "kStoreError";
    }
    auto sr = ctx->state.Load(state_text);
    if (!sr.ok) {
      *err = "policy state rejected (strict): " + sr.error;
      return "kStoreError";
    }
  }
  ctx->counters = CounterStore(store_dir);
  auto cr = ctx->counters.Load();
  if (!cr.ok && !cr.error.empty()) {
    *err = cr.error;
    return "kStoreError";
  }
  return "";
}

// -- method handlers ------------------------------------------------------

JsonValue HandleFlagStatus(const Context& ctx, const JsonValue&) {
  JsonValue::Object o;
  o["xr_settings_v0"] = JsonValue(ctx.flag == "on" ? "on" : "off");
  return JsonValue(std::move(o));
}

JsonValue HandleSections(Context* ctx, const JsonValue&) {
  if (ctx->flag != "on") {
    return Err("kRejected", "feature flag xr_settings_v0 is off — nothing renders");
  }
  auto registry = xr::settings::BuildRegistry(ctx->schema, ctx->state);
  JsonValue::Array arr;
  for (const auto& s : registry) {
    JsonValue::Object o;
    o["id"] = JsonValue(s.id);
    o["title_id"] = JsonValue(s.title_id);
    o["anchor"] = JsonValue(s.anchor);
    o["available"] = JsonValue(s.available);
    o["availability"] = JsonValue(s.availability);
    if (!s.unavailable_reason.empty()) {
      o["unavailable_reason"] = JsonValue(s.unavailable_reason);
    }
    JsonValue::Array keys;
    for (const auto& k : s.setting_keys) keys.push_back(JsonValue(k));
    o["settings"] = JsonValue(std::move(keys));
    arr.push_back(JsonValue(std::move(o)));
  }
  JsonValue::Object o;
  o["sections"] = JsonValue(std::move(arr));
  o["count"] = JsonValue(static_cast<int64_t>(registry.size()));
  return JsonValue(std::move(o));
}

JsonValue HandleSearch(Context* ctx, const JsonValue& args) {
  const JsonValue* q = args.find("query");
  std::string query = (q != nullptr && q->is_string()) ? q->as_string() : "";
  JsonValue::Object o;
  if (ctx->flag != "on") {
    o["results"] = JsonValue(JsonValue::Array{});
    o["count"] = JsonValue(static_cast<int64_t>(0));
    return JsonValue(std::move(o));
  }
  auto results = xr::settings::MatchQuery(
      query, xr::settings::BuildIndex(ctx->schema));
  JsonValue::Array arr;
  for (const auto& r : results) {
    JsonValue::Object row;
    row["key"] = JsonValue(r.key);
    row["kind"] = JsonValue(r.kind == xr::settings::EntryKind::kSection
                                ? "section" : "setting");
    row["score"] = JsonValue(static_cast<int64_t>(r.score));
    arr.push_back(JsonValue(std::move(row)));
    if (r.kind == xr::settings::EntryKind::kSetting) {
      ctx->counters.AcceptQuery();
    }
  }
  o["results"] = JsonValue(std::move(arr));
  o["count"] = JsonValue(static_cast<int64_t>(results.size()));
  // Persist recorded events (durable ledger); disposable stores no-op, a
  // healthy ledger saves, failures are ignored here (never mask the result).
  ctx->counters.Save(nullptr);
  return JsonValue(std::move(o));
}

JsonValue HandleGet(Context* ctx, const JsonValue& args) {
  const JsonValue* key = args.find("key");
  const std::string k = (key != nullptr && key->is_string()) ? key->as_string() : "";
  if (k.empty()) return Err("kMalformedInput", "get: 'key' string required");
  if (ctx->flag != "on") {
    return Err("kRejected", "feature flag xr_settings_v0 is off");
  }
  const auto* def = ctx->schema.FindSetting(k);
  if (def == nullptr) {
    return Err("kRejected", "unknown setting key '" + k +
                                "' (strict: a setting without a schema entry does not ship)");
  }
  JsonValue::Object o;
  o["key"] = JsonValue(k);
  std::string source = "default";
  if (ctx->state.HasValue(k)) {
    auto v = ctx->state.GetValue(k);
    std::string verr;
    const std::string t = def->type;
    bool ok_type = false;
    if (t == "bool") ok_type = v.value.is_bool();
    else if (t == "enum" || t == "string") ok_type = v.value.is_string();
    else if (t == "int") ok_type = v.value.is_int();
    if (!ok_type) {
      return Err("kRejected", "policy state value for '" + k +
                                  "' mismatches declared type '" + t + "' (never guess)");
    }
    if (t == "enum") {
      bool in_options = false;
      for (const auto& opt : ctx->schema.EnumOptions(k)) {
        if (opt == v.value.as_string()) in_options = true;
      }
      if (!in_options) {
        return Err("kRejected", "policy state value for '" + k +
                                    "' not in declared enum options");
      }
    }
    o["value"] = v.value;
    source = v.source;
  } else {
    const JsonValue* dflt = ctx->schema.DefaultFor(k);
    o["value"] = dflt == nullptr ? JsonValue() : *dflt;
  }
  o["source"] = JsonValue(source);
  o["preempted"] = JsonValue(source == "enterprise");
  o["writable"] = JsonValue(ctx->schema.WritableInV0(k));
  o["section"] = JsonValue(def->section);
  o["attention_tier"] = JsonValue(def->attention_tier);
  o["scope"] = JsonValue(def->scope);
  return JsonValue(std::move(o));
}

JsonValue HandleSet(Context* ctx, const JsonValue& args) {
  const JsonValue* key = args.find("key");
  const std::string k = (key != nullptr && key->is_string()) ? key->as_string() : "";
  const JsonValue* value = args.find("value");
  if (k.empty() || value == nullptr) {
    return Err("kMalformedInput", "set: 'key' + 'value' required");
  }
  if (ctx->flag != "on") {
    return Err("kRejected", "feature flag xr_settings_v0 is off — nothing writes");
  }
  const auto* def = ctx->schema.FindSetting(k);
  if (def == nullptr) {
    return Err("kRejected", "unknown setting key '" + k + "' (strict)");
  }
  if (!ctx->schema.WritableInV0(k)) {
    // Enterprise/policy preemption semantics (P6-T7): a policy-owned row is
    // never silently overridden; v0 ships no writable toggles (skeleton).
    return Err("kRejected",
               "setting '" + k +
                   "' is surfaced-by-policy-only in v0 — no user write path "
                   "(writes land with the P13+ settings store); never silently override");
  }
  JsonValue::Object doc;
  doc[k] = *value;
  auto vr = ctx->schema.ValidateDoc(JsonValue(std::move(doc)));
  if (!vr.ok) return Err("kMalformedInput", vr.error);
  ctx->counters.SettingChanged(k);
  if (!ctx->counters.Save(nullptr)) {
    return Err("kIoError", "counters persist failed (setting change not lost in-memory)");
  }
  JsonValue::Object o;
  o["key"] = JsonValue(k);
  o["status"] = JsonValue(std::string("set"));
  o["persisted"] = JsonValue(!ctx->counters.in_memory());
  return JsonValue(std::move(o));
}

JsonValue HandleResolve(Context* ctx, const JsonValue& args) {
  const JsonValue* a = args.find("anchor");
  const std::string anchor =
      (a != nullptr && a->is_string()) ? a->as_string() : "";
  if (ctx->flag != "on") {
    return Err("kRejected", "feature flag xr_settings_v0 is off");
  }
  xr::settings::Router router(ctx->schema);
  auto r = router.Resolve(anchor);
  if (r.ok && (r.kind == xr::settings::ResolveKind::kSection ||
               r.kind == xr::settings::ResolveKind::kSetting)) {
    ctx->counters.OpenSection(r.section);  // section actually opened via link
    ctx->counters.Save(nullptr);  // durable (no-op when disposable)
  }
  JsonValue::Object o;
  o["ok"] = JsonValue(r.ok);
  const char* kind = "unknown";
  switch (r.kind) {
    case xr::settings::ResolveKind::kHome: kind = "home"; break;
    case xr::settings::ResolveKind::kSection: kind = "section"; break;
    case xr::settings::ResolveKind::kSetting: kind = "setting"; break;
    case xr::settings::ResolveKind::kUnknown: break;
  }
  o["kind"] = JsonValue(std::string(kind));
  if (!r.section.empty()) o["section"] = JsonValue(r.section);
  if (!r.setting.empty()) o["setting"] = JsonValue(r.setting);
  if (!r.canonical.empty()) o["canonical"] = JsonValue(r.canonical);
  if (!r.error.empty()) o["error"] = JsonValue(r.error);
  if (!r.suggestions.empty()) {
    JsonValue::Array sug;
    for (const auto& s : r.suggestions) sug.push_back(JsonValue(s));
    o["suggestions"] = JsonValue(std::move(sug));
  }
  return JsonValue(std::move(o));
}

JsonValue HandleCountersDump(Context* ctx, const JsonValue&) {
  if (ctx->flag != "on") {
    return Err("kRejected", "feature flag xr_settings_v0 is off — no counters written");
  }
  return ctx->counters.Dump();
}

JsonValue HandleSchemaDump(const Context& ctx, const JsonValue&) {
  if (ctx.flag != "on") {
    return Err("kRejected", "feature flag xr_settings_v0 is off");
  }
  // Re-emit the schema data doc exactly as loaded is not stored; instead
  // produce a typed summary over the schema registry (parity-safe).
  JsonValue::Object o;
  o["schema"] = JsonValue(std::string("xr-settings-schema"));
  o["schema_version"] = JsonValue(static_cast<int64_t>(ctx.schema.version()));
  o["anchor_root"] = JsonValue(ctx.schema.anchor_root());
  JsonValue::Array keys;
  for (const auto* s : ctx.schema.Settings()) keys.push_back(JsonValue(s->key));
  o["keys"] = JsonValue(std::move(keys));
  o["count"] = JsonValue(static_cast<int64_t>(ctx.schema.Count()));
  return JsonValue(std::move(o));
}

}  // namespace

int main(int argc, char** argv) {
  std::string store_dir;
  std::string schema_path = XR_DEFAULT_SETTINGS_SCHEMA;
  std::string state_path;
  std::string flag = "on";
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--store-dir" && i + 1 < argc) store_dir = argv[++i];
    else if (a == "--schema" && i + 1 < argc) schema_path = argv[++i];
    else if (a == "--state" && i + 1 < argc) state_path = argv[++i];
    else if (a == "--flag" && i + 1 < argc) {
      std::string kv = argv[++i];
      auto eq = kv.find('=');
      if (eq == std::string::npos) {
        std::fprintf(stderr, "usage error: --flag k=v\n");
        return 2;
      }
      if (kv.substr(0, eq) == "xr_settings_v0") flag = kv.substr(eq + 1);
      else {
        std::fprintf(stderr, "usage error: unknown flag %s\n", kv.substr(0, eq).c_str());
        return 2;
      }
    } else if (a.rfind("--", 0) == 0) {
      std::fprintf(stderr, "usage error: unknown option %s\n", a.c_str());
      return 2;
    } else {
      positional.push_back(a);
    }
  }
  if (flag != "on" && flag != "off") {
    std::fprintf(stderr, "usage error: xr_settings_v0 must be on|off\n");
    return 2;
  }

  // Resolve (method, args-JSON-text).
  std::string method;
  std::string args_json = "{}";
  if (positional.empty()) {
    args_json = ReadAllStdin();
    JsonParseResult p = ParseJson(args_json);
    if (!p.ok) {
      std::printf("{\"error\":\"kMalformedInput\",\"detail\":\"%s\"}\n",
                  p.error.c_str());
      return 1;
    }
    const JsonValue* m = p.value.find("method");
    const JsonValue* a = p.value.find("args");
    method = (m && m->is_string()) ? m->as_string() : "";
    args_json = a ? a->Canonical() : std::string("{}");
  } else if (IsMethod(positional[0])) {
    method = positional[0];
    if (positional.size() >= 2) args_json = positional[1];
  } else {
    args_json = positional[0];
    JsonParseResult p = ParseJson(args_json);
    if (!p.ok) {
      std::printf("{\"error\":\"kMalformedInput\",\"detail\":\"%s\"}\n",
                  p.error.c_str());
      return 1;
    }
    const JsonValue* m = p.value.find("method");
    const JsonValue* a = p.value.find("args");
    method = (m && m->is_string()) ? m->as_string() : "";
    args_json = a ? a->Canonical() : std::string("{}");
  }
  if (method.empty()) {
    std::fprintf(stderr,
                 "usage: settings_host <method> ['<json-args>'] [options]\n"
                 "       settings_host '<json-with-method>' [options]\n"
                 "       settings_host [options]   # request JSON on stdin\n"
                 "methods: flag-status sections search get set router-resolve\n"
                 "         counters-dump schema-dump\n"
                 "options: --store-dir DIR --schema PATH --state PATH\n"
                 "         --flag xr_settings_v0=on|off\n");
    return 2;
  }

  JsonParseResult ap = ParseJson(args_json);
  if (!ap.ok || !ap.value.is_object()) {
    std::printf("{\"error\":\"kMalformedInput\",\"detail\":\"args must be a JSON object\"}\n");
    return 1;
  }

  Context ctx;
  ctx.flag = flag;

  JsonValue result;
  if (method == "flag-status") {
    result = HandleFlagStatus(ctx, ap.value);
    Emit(result);
    return 0;
  }
  if (flag == "on") {
    std::string err;
    std::string code = LoadContext(&ctx, store_dir, schema_path, state_path, &err);
    if (!code.empty()) {
      Emit(Err(code.c_str(), err));
      return 1;
    }
  }
  if (method == "sections") result = HandleSections(&ctx, ap.value);
  if (method == "search") result = HandleSearch(&ctx, ap.value);
  if (method == "get") result = HandleGet(&ctx, ap.value);
  if (method == "set") result = HandleSet(&ctx, ap.value);
  if (method == "router-resolve") result = HandleResolve(&ctx, ap.value);
  if (method == "counters-dump") result = HandleCountersDump(&ctx, ap.value);
  if (method == "schema-dump") result = HandleSchemaDump(ctx, ap.value);
  if (result.is_null()) result = Err("kUnknownMethod", "unknown method '" + method + "'");
  Emit(result);
  return 0;
}
