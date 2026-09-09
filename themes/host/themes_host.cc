// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// themes_host — stdio JSON façade for the themes core (Plan P8-T3/T4).
// Protocol doc: themes/host_protocol.md. Same canonical-JSON conventions as
// settings_host / commands_host: one canonical line per result (sorted keys,
// compact, non-ASCII \uXXXX); exit 0 = typed result, 1 = typed error,
// 2 = usage. Deterministic output only (timing lives in themes/bench, never
// in protocol output — the byte-parity gate must stay stable).
//
// Methods:
//   flag-status   -> {"xr_themes_v0":"on"} (symmetry; not flag-gated in P8)
//   list          -> built-ins + the system resolver + current + mode
//   current       -> applied name, resolved builtin + the full token map
//   apply {name}  -> validate -> audit -> REFUSE; ok => applied event
//   import {theme-doc} -> hostile-input custom theme (T4): size cap,
//        duplicate-key scan, strict parse, schema, audit, per-token deltas;
//        atomic refusal; session-scoped (never durable — recorded)
//   validate-doc {theme-doc} -> audit only, state untouched
//   system-mode {mode} -> light|dark|high-contrast; re-resolves when the
//        applied theme is the system resolver
//
// Applied-state durability: store-dir/xr-themes-state.json
// {schema,schema_version:1,applied,mode} written tmp->fsync->rename.
// Absent --store-dir => disposable (in-memory, zero bytes).
// "custom" is never written to the state file (session-scoped in v0).
#include <cstdio>
#include <cstring>
#include <map>
#include <unistd.h>
#include <string>
#include <vector>

#include "themes/core/loader.h"

#ifndef XR_DEFAULT_TOKENS
#define XR_DEFAULT_TOKENS "ui/themes/tokens.json"
#endif

namespace {

using xr::themes::ApplyResult;
using xr::themes::BuiltinTheme;
using xr::themes::FindBuiltin;
using xr::themes::JsonParseResult;
using xr::themes::JsonValue;
using xr::themes::LoaderState;
using xr::themes::ParseJson;
using xr::themes::TokenMap;

constexpr const char* kStateSchema = "xr-themes-state";
constexpr int kStateVersion = 1;
constexpr const char* kStateFile = "themes-state.json";

struct Context {
  LoaderState loader;   // resolved values + mode (loader.applied = builtin)
  std::string applied;  // "system" | builtin name | "custom" (session)
  std::string store_dir;
};

std::string ReadAllFile(const std::string& path, bool* ok) {
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

void Emit(const JsonValue& v) { std::printf("%s\n", v.Canonical().c_str()); }

bool IsMethod(const std::string& s) {
  static const char* kMethods[] = {"flag-status", "list", "current",
                                   "apply",       "import", "validate-doc",
                                   "system-mode"};
  for (const char* m : kMethods) {
    if (s == m) return true;
  }
  return false;
}

std::string StatePath(const Context& ctx) {
  return ctx.store_dir.empty() ? "" : ctx.store_dir + "/" + kStateFile;
}

// Applies the stored session state onto the loader. Returns a human note on
// deny-preserve conditions (state kept, not rewritten), else "".
std::string RestoreState(Context* ctx) {
  const std::string path = StatePath(*ctx);
  if (path.empty()) {
    ctx->applied = "system";  // default session: follow the system resolver
    return "";
  }
  bool ok = false;
  const std::string text = ReadAllFile(path, &ok);
  if (!ok) {
    ctx->applied = "system";
    return "";
  }
  JsonParseResult pr = ParseJson(text);
  if (!pr.ok || !pr.value.is_object()) {
    ctx->applied = "system";
    return "on-disk themes state is unreadable — preserved, not rewritten";
  }
  const JsonValue* sv = pr.value.find("schema_version");
  const JsonValue* app = pr.value.find("applied");
  const JsonValue* mode = pr.value.find("mode");
  if (!sv || !sv->is_int() || sv->as_int() != kStateVersion || !app ||
      !app->is_string() || !mode || !mode->is_string()) {
    ctx->applied = "system";
    return "themes state schema mismatch — preserved, not rewritten";
  }
  if (mode->as_string() != "light" && mode->as_string() != "dark" &&
      mode->as_string() != "high-contrast") {
    ctx->applied = "system";
    return "themes state mode invalid — preserved, not rewritten";
  }
  ctx->loader.mode = mode->as_string();
  ctx->applied = app->as_string();
  if (ctx->applied == "custom") {
    // Custom docs are session-scoped (never durable); a restart resolves
    // through the system resolver rather than pretending half-state.
    ctx->applied = "system";
  }
  ApplyResult r = xr::themes::ApplyBuiltin(&ctx->loader, ctx->applied);
  if (!r.ok) {
    ctx->applied = "system";
    ApplyResult d = xr::themes::ApplyBuiltin(&ctx->loader, "system");
    if (!d.ok) {
      ctx->loader.ok = false;
      ctx->loader.error = "state restore failed: " + d.refusal;
    }
    return "stored theme '" + app->as_string() + "' refused on restore: " +
           r.refusal;
  }
  return "";
}

std::string PersistState(const Context& ctx, std::string* error) {
  if (ctx.store_dir.empty()) return "";  // disposable: never writes
  const std::string name = (ctx.applied == "custom") ? "system" : ctx.applied;
  JsonValue::Object o;
  o["schema"] = JsonValue(std::string(kStateSchema));
  o["schema_version"] = JsonValue(static_cast<int64_t>(kStateVersion));
  o["applied"] = JsonValue(name);
  o["mode"] = JsonValue(ctx.loader.mode);
  const std::string json = JsonValue(std::move(o)).Canonical() + "\n";
  const std::string tmp = StatePath(ctx) + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    *error = "cannot open tmp themes state for write";
    return "kIoError";
  }
  const bool ok = std::fwrite(json.data(), 1, json.size(), f) == json.size() &&
                  std::fflush(f) == 0 && fsync(fileno(f)) == 0;
  if (!ok) {
    std::fclose(f);
    std::remove(tmp.c_str());
    *error = "tmp write/fsync failed";
    return "kIoError";
  }
  std::fclose(f);
  if (std::rename(tmp.c_str(), StatePath(ctx).c_str()) != 0) {
    std::remove(tmp.c_str());
    *error = "atomic rename failed";
    return "kIoError";
  }
  return "";
}

JsonValue ValuesJson(const TokenMap& values) {
  JsonValue::Object o;
  for (const auto& [k, v] : values) o[k] = v;
  return JsonValue(std::move(o));
}

JsonValue ListJson(const Context& ctx) {
  JsonValue::Array arr;
  for (const auto& b : ctx.loader.source.builtins) {
    JsonValue::Object o;
    o["name"] = JsonValue(b.name);
    o["kind"] = JsonValue("builtin");
    o["waivers"] = JsonValue(static_cast<int64_t>(b.waivers.size()));
    arr.push_back(JsonValue(std::move(o)));
  }
  JsonValue::Object sys;
  sys["name"] = JsonValue("system");
  sys["kind"] = JsonValue("resolver");
  sys["default"] = JsonValue(ctx.loader.source.system_default);
  arr.push_back(JsonValue(std::move(sys)));
  JsonValue::Object out;
  out["themes"] = JsonValue(std::move(arr));
  out["count"] =
      JsonValue(static_cast<int64_t>(ctx.loader.source.builtins.size() + 1));
  out["current"] = JsonValue(ctx.applied);
  out["resolved"] = JsonValue(ctx.loader.applied);
  out["mode"] = JsonValue(ctx.loader.mode);
  return JsonValue(std::move(out));
}

JsonValue EventJson(const Context& ctx) {
  JsonValue::Object o;
  o["ok"] = JsonValue(true);
  o["applied"] = JsonValue(ctx.applied);
  o["resolved"] = JsonValue(ctx.loader.applied);
  o["mode"] = JsonValue(ctx.loader.mode);
  o["values"] = ValuesJson(ctx.loader.values);
  return JsonValue(std::move(o));
}

JsonValue ApplyResultJson(const ApplyResult& r, const Context& ctx,
                          const std::string& applied_name) {
  JsonValue::Object o;
  o["ok"] = JsonValue(true);
  o["applied"] = JsonValue(applied_name);
  o["resolved"] = JsonValue(r.applied_theme);
  o["mode"] = JsonValue(ctx.loader.mode);
  o["values"] = ValuesJson(r.applied_values);
  return JsonValue(std::move(o));
}

}  // namespace

int main(int argc, char** argv) {
  std::string store_dir, tokens_path = XR_DEFAULT_TOKENS;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--store-dir" && i + 1 < argc) {
      store_dir = argv[++i];
    } else if (a == "--tokens" && i + 1 < argc) {
      tokens_path = argv[++i];
    } else if (a == "--flag" && i + 1 < argc) {
      ++i;  // accepted for symmetry; theme hosting is not flag-gated in v0
    } else if (a == "-h" || a == "--help") {
      std::fprintf(stderr,
                   "usage: themes_host <method> ['<json-args>'] [options]\n"
                   "       themes_host '<json-with-method>' [options]\n"
                   "       themes_host [options]   # request on stdin\n"
                   "options: --store-dir DIR  --tokens PATH\n");
      return 2;
    } else {
      positional.push_back(a);
    }
  }
  std::string method, args_text;
  if (positional.empty()) {
    const std::string req = ReadAllStdin();
    JsonParseResult pr = ParseJson(req);
    if (!pr.ok || !pr.value.is_object()) {
      Emit(Err("kMalformedInput", "stdin request must be a JSON object"));
      return 1;
    }
    const JsonValue* m = pr.value.find("method");
    const JsonValue* a = pr.value.find("args");
    if (!m || !m->is_string()) {
      Emit(Err("kMalformedInput", "request missing 'method'"));
      return 1;
    }
    method = m->as_string();
    args_text = a ? a->Canonical() : "{}";
  } else if (IsMethod(positional[0])) {
    method = positional[0];
    args_text = positional.size() >= 2 ? positional[1] : "{}";
    if (positional.size() > 2) {
      std::fprintf(stderr, "usage error: extra arguments\n");
      return 2;
    }
  } else {
    const std::string& req = positional[0];
    JsonParseResult pr = ParseJson(req);
    if (!pr.ok || !pr.value.is_object()) {
      Emit(Err("kMalformedInput", "json-with-method must be an object"));
      return 1;
    }
    const JsonValue* m = pr.value.find("method");
    const JsonValue* a = pr.value.find("args");
    if (!m || !m->is_string()) {
      Emit(Err("kMalformedInput", "json-with-method missing 'method'"));
      return 1;
    }
    method = m->as_string();
    args_text = a ? a->Canonical() : "{}";
  }
  if (!IsMethod(method)) {
    Emit(Err("kUnknownMethod", "unknown method '" + method + "'"));
    return 1;
  }
  JsonParseResult args_pr = ParseJson(args_text);
  if (!args_pr.ok || !args_pr.value.is_object()) {
    Emit(Err("kMalformedInput", "args must be a JSON object"));
    return 1;
  }
  const JsonValue& args = args_pr.value;

  bool ok = false;
  const std::string tokens_text = ReadAllFile(tokens_path, &ok);
  if (!ok) {
    Emit(Err("kStoreError", "tokens source not readable: " + tokens_path));
    return 1;
  }
  Context ctx;
  ctx.store_dir = store_dir;
  ctx.loader = xr::themes::LoaderLoad(tokens_text, "light");
  if (!ctx.loader.ok) {
    Emit(Err("kStoreError", ctx.loader.error));
    return 1;
  }
  const std::string note = RestoreState(&ctx);  // sets ctx.applied default
  if (!ctx.loader.ok) {
    Emit(Err("kStoreError", ctx.loader.error));
    return 1;
  }
  if (method == "flag-status") {
    JsonValue::Object o;
    o["xr_themes_v0"] = JsonValue(std::string("on"));
    Emit(JsonValue(std::move(o)));
    return 0;
  }
  if (method == "list") {
    Emit(ListJson(ctx));
    return 0;
  }
  if (method == "current") {
    Emit(EventJson(ctx));
    return 0;
  }
  if (method == "apply") {
    const JsonValue* name = args.find("name");
    if (!name || !name->is_string()) {
      Emit(Err("kMalformedInput", "apply: 'name' string required"));
      return 1;
    }
    const std::string requested = name->as_string();
    ApplyResult r = xr::themes::ApplyBuiltin(&ctx.loader, requested);
    if (!r.ok) {
      Emit(Err("kRejected", r.refusal));
      return 1;
    }
    ctx.applied = requested;
    ctx.loader.applied = r.applied_theme;
    ctx.loader.values = r.applied_values;
    std::string serr;
    const std::string scode = PersistState(ctx, &serr);
    if (!scode.empty()) {
      Emit(Err(scode.c_str(), serr));
      return 1;
    }
    Emit(ApplyResultJson(r, ctx, ctx.applied));
    return 0;
  }
  if (method == "system-mode") {
    const JsonValue* mode = args.find("mode");
    if (!mode || !mode->is_string()) {
      Emit(Err("kMalformedInput", "system-mode: 'mode' string required"));
      return 1;
    }
    const std::string m = mode->as_string();
    if (m != "light" && m != "dark" && m != "high-contrast") {
      Emit(Err("kMalformedInput", "system-mode: light|dark|high-contrast"));
      return 1;
    }
    ctx.loader.mode = m;
    if (ctx.applied == "system" || ctx.applied.empty()) {
      ApplyResult r = xr::themes::ApplyBuiltin(&ctx.loader, "system");
      if (!r.ok) {
        Emit(Err("kRejected", r.refusal));
        return 1;
      }
      ctx.loader.applied = r.applied_theme;
      ctx.loader.values = r.applied_values;
      ctx.applied = "system";
    }
    std::string serr;
    const std::string scode = PersistState(ctx, &serr);
    if (!scode.empty()) {
      Emit(Err(scode.c_str(), serr));
      return 1;
    }
    Emit(EventJson(ctx));
    return 0;
  }
  if (method == "import" || method == "validate-doc") {
    const JsonValue* docv = args.find("theme-doc");
    if (!docv || !docv->is_string()) {
      Emit(Err("kMalformedInput",
               std::string(method) + ": 'theme-doc' string required"));
      return 1;
    }
    const std::string raw = docv->as_string();
    LoaderState scratch = ctx.loader;  // atomic: nothing moves on refusal
    ApplyResult r = xr::themes::ImportTheme(&scratch, raw);
    if (!r.ok) {
      Emit(Err("kRejected", r.refusal));
      return 1;
    }
    if (method == "validate-doc") {
      // Audit-only: report the deltas vs the CURRENT applied map; state is
      // untouched (scratch was discarded).
      JsonValue::Array darr;
      for (const auto& d : r.deltas) darr.push_back(JsonValue(d));
      JsonValue::Object o;
      o["ok"] = JsonValue(true);
      o["deltas"] = JsonValue(std::move(darr));
      o["delta_count"] = JsonValue(static_cast<int64_t>(r.deltas.size()));
      Emit(JsonValue(std::move(o)));
      return 0;
    }
    // import: session-scoped custom apply. The state file keeps the last
    // built-in (never "custom"): a restart resolves through the resolver.
    ctx.loader = scratch;
    ctx.applied = "custom";
    JsonValue::Object res = ApplyResultJson(r, ctx, "custom").as_object();
    JsonValue::Array darr;
    for (const auto& d : r.deltas) darr.push_back(JsonValue(d));
    res["deltas"] = JsonValue(std::move(darr));
    res["delta_count"] = JsonValue(static_cast<int64_t>(r.deltas.size()));
    Emit(JsonValue(std::move(res)));
    return 0;
  }
  std::fprintf(stderr, "unreachable\n");
  return 2;
}
