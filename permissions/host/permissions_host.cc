// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the permissions host (P15-CLOSE C2, ADR-0051 DRAFT): the
// CLOCK-OWNING layer over the clock-free core (permissions/core/). It reads the
// clock exactly once per call (ObservedNowMs, below — the only clock read under
// permissions/), folds it through the core's monotone EffectiveNow against the
// persisted high-water mark, passes now_ms into permissions/core/ops, and writes
// the store tmp -> fsync -> rename. The core never reads a clock; the law that
// keeps it that way is tools/permission_write_path_check.py (clock-free row).
//
// Protocol: permissions/host_protocol.md (permissions-host-protocol v1). The
// Python fake (fakes/permissions.py) is byte-identical on the deterministic
// surface (--as-of), proven by tools/tests/test_p15c_permissions_host.py.
//
//   permissions_host [--store-dir DIR] [--as-of MS] '<json {method,args}>'
//   permissions_host [--store-dir DIR] [--as-of MS] <method> ['<json-args>']
//   permissions_host [--store-dir DIR] [--as-of MS]          (request on stdin)
//
// Absent --store-dir => disposable: the input store rides in args.store and
// nothing is written anywhere (zero bytes; the kill matrix proves it).
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "common/core/json.h"
#include "permissions/core/audit.h"
#include "permissions/core/ops.h"
#include "permissions/core/store.h"

namespace {

using xr::common::JsonParseResult;
using xr::common::JsonValue;
using xr::common::ParseJson;
namespace perm = xr::permissions;

constexpr const char* kStoreFile = "permissions-store.json";
constexpr const char* kClockFile = "permissions-clock.json";
constexpr size_t kMaxRequestBytes = 1 << 20;

// THE clock read. Nothing in permissions/core/ may do this.
int64_t ObservedNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int Emit(const JsonValue& v) {
  std::printf("%s\n", v.Canonical().c_str());
  return 0;
}

int EmitErr(const std::string& code, const std::string& detail) {
  JsonValue::Object o;
  o["error"] = JsonValue(code);
  o["detail"] = JsonValue(detail);
  Emit(JsonValue(std::move(o)));
  return 1;
}

int Usage(const char* why) {
  std::fprintf(stderr,
               "usage error: %s\n"
               "usage: permissions_host [--store-dir DIR] [--as-of MS] "
               "['<json {method,args}>' | <method> ['<json-args>']]\n",
               why);
  return 2;
}

bool ReadFile(const std::string& path, std::string* out, bool* exists) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    *exists = false;
    return true;
  }
  *exists = true;
  out->clear();
  char buf[8192];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->append(buf, n);
  const bool ok = std::ferror(f) == 0;
  std::fclose(f);
  return ok;
}

bool AtomicWrite(const std::string& path, const std::string& bytes) {
  const std::string tmp = path + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) return false;
  const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size() &&
                  std::fflush(f) == 0 && fsync(fileno(f)) == 0;
  std::fclose(f);
  if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

std::string ReadAllStdin() {
  std::string s;
  char buf[8192];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, stdin)) > 0) {
    s.append(buf, n);
    if (s.size() > kMaxRequestBytes) break;
  }
  return s;
}

// One argument rule: name, JSON type, required. Checked in a fixed order that
// fakes/permissions.py mirrors, so both backends refuse with the same detail.
enum class T { kStr, kBool };
struct Arg {
  const char* name;
  T type;
  bool required;
};

struct MethodSpec {
  const char* name;
  bool mutates;
  std::vector<Arg> args;
};

const std::vector<MethodSpec>& Specs() {
  static const std::vector<MethodSpec> kSpecs = {
      {"view", false, {}},
      {"set-default", true,
       {{"identity", T::kStr, true}, {"capability", T::kStr, true}, {"state", T::kStr, true}}},
      {"grant", true,
       {{"identity", T::kStr, true}, {"domain", T::kStr, true}, {"capability", T::kStr, true},
        {"scope", T::kStr, true}, {"session_id", T::kStr, false}}},
      {"set-denied", true, {{"identity", T::kStr, true}, {"denied", T::kBool, true}}},
      {"consume-once", true, {{"grant_id", T::kStr, true}}},
      {"sweep", true, {{"session_id", T::kStr, true}}},
      {"revoke-site", true, {{"identity", T::kStr, true}, {"domain", T::kStr, true}}},
      {"revoke-all", true, {{"identity", T::kStr, true}}},
  };
  return kSpecs;
}

const MethodSpec* FindSpec(const std::string& method) {
  // The dispatch literals host_protocol_check.py reads (one per method).
  if (method == "view" || method == "set-default" || method == "grant" ||
      method == "set-denied" || method == "consume-once" || method == "sweep" ||
      method == "revoke-site" || method == "revoke-all") {
    for (const auto& s : Specs()) {
      if (method == s.name) return &s;
    }
  }
  return nullptr;
}

// Returns "" when the args satisfy the spec, else the refusal detail.
std::string CheckArgs(const MethodSpec& spec, const JsonValue& args, bool disposable) {
  for (const auto& [key, val] : args.as_object()) {  // sorted walk
    if (key == "store") {
      if (!disposable) return "arg 'store' is disposable-mode only";
      if (!val.is_string()) return "arg 'store' must be a string";
      continue;
    }
    bool known = false;
    for (const auto& a : spec.args) known = known || key == a.name;
    if (!known) return "unknown arg '" + key + "'";
  }
  for (const auto& a : spec.args) {
    const JsonValue* v = args.find(a.name);
    if (v == nullptr) {
      if (a.required) return std::string("missing arg '") + a.name + "'";
      continue;
    }
    const bool ok = a.type == T::kStr ? v->is_string() : v->is_bool();
    if (!ok) {
      return std::string("arg '") + a.name + "' must be a " +
             (a.type == T::kStr ? "string" : "boolean");
    }
  }
  return "";
}

std::string Str(const JsonValue& args, const char* key) {
  const JsonValue* v = args.find(key);
  return v == nullptr ? std::string() : v->as_string();
}

perm::OpOutcome Apply(const std::string& m, const perm::Store& in, const JsonValue& args,
                      int64_t now) {
  if (m == "set-default") {
    return perm::SetDefault(in, Str(args, "identity"), Str(args, "capability"),
                            Str(args, "state"), now);
  }
  if (m == "grant") {
    perm::GrantRequest req;
    req.identity = Str(args, "identity");
    req.domain = Str(args, "domain");
    req.capability = Str(args, "capability");
    req.scope = Str(args, "scope");
    req.session_id = Str(args, "session_id");
    return perm::GrantTemp(in, req, now);
  }
  if (m == "set-denied") {
    return perm::SetDenied(in, Str(args, "identity"), args.find("denied")->as_bool(), now);
  }
  if (m == "consume-once") return perm::ConsumeOnce(in, Str(args, "grant_id"), now);
  if (m == "sweep") return perm::SweepExpired(in, now, Str(args, "session_id"));
  if (m == "revoke-site") {
    return perm::RevokeSite(in, Str(args, "identity"), Str(args, "domain"), now);
  }
  return perm::RevokeAll(in, Str(args, "identity"), now);  // "revoke-all"
}

int Run(const std::string& method, const JsonValue& args, const std::string& store_dir,
        bool have_as_of, int64_t as_of) {
  const MethodSpec* spec = FindSpec(method);
  if (spec == nullptr) return EmitErr("kUnknownMethod", "unknown method '" + method + "'");
  const bool disposable = store_dir.empty();
  const std::string bad = CheckArgs(*spec, args, disposable);
  if (!bad.empty()) return EmitErr("kMalformedInput", bad);

  // Load the store (and, in store-dir mode, the clock high-water mark).
  std::string store_text;
  bool store_exists = false;
  int64_t last_seen = 0;
  if (disposable) {
    const JsonValue* s = args.find("store");
    store_exists = s != nullptr;
    if (store_exists) store_text = s->as_string();
  } else {
    if (!ReadFile(store_dir + "/" + kStoreFile, &store_text, &store_exists)) {
      return EmitErr("kIoError", "cannot read the store file");
    }
    std::string clock_text;
    bool clock_exists = false;
    if (!ReadFile(store_dir + "/" + kClockFile, &clock_text, &clock_exists)) {
      return EmitErr("kIoError", "cannot read the clock file");
    }
    if (clock_exists) {
      JsonParseResult cp = ParseJson(clock_text);
      const JsonValue* ls = cp.ok && cp.value.is_object() ? cp.value.find("last_seen_ms") : nullptr;
      if (ls == nullptr || !ls->is_int() || ls->as_int() < 0 ||
          cp.value.as_object().size() != 1 || cp.value.Canonical() != clock_text) {
        return EmitErr("kFailClosed", "corrupt clock file: refused, left untouched");
      }
      last_seen = ls->as_int();
    }
  }
  const perm::Store in = store_exists ? perm::LoadStore(store_text) : perm::Store{};

  if (!spec->mutates) {  // "view": the resolver projection, no clock needed
    JsonParseResult vp = ParseJson(perm::ProjectViewJson(in));
    JsonValue::Object ok;
    ok["view"] = vp.value;
    JsonValue::Object o;
    o["ok"] = JsonValue(std::move(ok));
    return Emit(JsonValue(std::move(o)));
  }

  const int64_t now = perm::EffectiveNow(last_seen, have_as_of ? as_of : ObservedNowMs());
  perm::OpOutcome out = Apply(method, in, args, now);
  if (!out.ok) return EmitErr("kRefused", out.error);

  const std::string new_text = perm::SerializeStore(out.store);
  if (!disposable) {
    if (!store_exists || new_text != store_text) {
      if (!AtomicWrite(store_dir + "/" + kStoreFile, new_text)) {
        return EmitErr("kIoError", "atomic store write failed");
      }
    }
    if (now > last_seen) {
      JsonValue::Object c;
      c["last_seen_ms"] = JsonValue(now);
      if (!AtomicWrite(store_dir + "/" + kClockFile, JsonValue(std::move(c)).Canonical())) {
        return EmitErr("kIoError", "atomic clock write failed");
      }
    }
  }
  JsonValue::Array rows;
  for (const auto& r : out.rows) rows.push_back(ParseJson(perm::AuditRowJson(r)).value);
  JsonValue::Object ok;
  ok["now_ms"] = JsonValue(now);
  ok["rows"] = JsonValue(std::move(rows));
  ok["store"] = JsonValue(new_text);
  JsonValue::Object o;
  o["ok"] = JsonValue(std::move(ok));
  return Emit(JsonValue(std::move(o)));
}

bool ParseMillis(const std::string& s, int64_t* out) {
  if (s.empty() || s.size() > 18) return false;
  int64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  *out = v;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string store_dir;
  bool have_as_of = false;
  int64_t as_of = 0;
  std::vector<std::string> pos;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--store-dir" || a == "--as-of") {
      if (i + 1 >= argc) return Usage("option needs a value");
      const std::string v = argv[++i];
      if (a == "--store-dir") {
        if (v.empty()) return Usage("--store-dir must be non-empty");
        store_dir = v;
      } else {
        if (!ParseMillis(v, &as_of)) return Usage("--as-of must be a non-negative integer (ms)");
        have_as_of = true;
      }
    } else if (a.rfind("--", 0) == 0) {
      return Usage("unknown option");
    } else {
      pos.push_back(a);
    }
  }
  if (pos.size() > 2) return Usage("too many arguments");

  std::string method;
  JsonValue args = JsonValue(JsonValue::Object{});
  if (pos.empty() || pos[0].rfind("{", 0) == 0) {
    if (pos.size() == 2) return Usage("a JSON request takes no second argument");
    const std::string req = pos.empty() ? ReadAllStdin() : pos[0];
    if (req.size() > kMaxRequestBytes) return EmitErr("kMalformedInput", "request exceeds 1 MiB");
    JsonParseResult pr = ParseJson(req);
    if (!pr.ok || !pr.value.is_object()) {
      return EmitErr("kMalformedInput", "request must be a JSON object");
    }
    const JsonValue* m = pr.value.find("method");
    if (m == nullptr || !m->is_string()) return EmitErr("kMalformedInput", "request missing 'method'");
    method = m->as_string();
    const JsonValue* a = pr.value.find("args");
    if (a != nullptr) args = *a;
  } else {
    method = pos[0];
    if (pos.size() == 2) {
      if (pos[1].size() > kMaxRequestBytes) return EmitErr("kMalformedInput", "request exceeds 1 MiB");
      JsonParseResult ar = ParseJson(pos[1]);
      if (!ar.ok) return EmitErr("kMalformedInput", "args must be a JSON object");
      args = ar.value;
    }
  }
  if (!args.is_object()) return EmitErr("kMalformedInput", "args must be a JSON object");
  return Run(method, args, store_dir, have_as_of, as_of);
}
