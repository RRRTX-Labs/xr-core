// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — PolicyStore implementation (see store.h).
#include "policy/core/store.h"

#include <cstdio>

namespace xr::policy {

namespace {

bool IsTrustTier(const std::string& s) {
  return s == "kStandard" || s == "kShield" || s == "kFortress";
}
bool IsScope(const std::string& s) {
  return s == "once" || s == "session" || s == "7d" || s == "permanent";
}

bool ObjFieldString(const JsonValue& o, const char* k, std::string* out, std::string* err,
                    const std::string& where) {
  const JsonValue* f = o.find(k);
  if (f == nullptr || !f->is_string()) {
    *err = where + ": '" + k + "' must be a string";
    return false;
  }
  *out = f->as_string();
  return true;
}

}  // namespace

const char* ToString(StoreError e) {
  switch (e) {
    case StoreError::kNone: return "kNone";
    case StoreError::kIoError: return "kIoError";
    case StoreError::kMalformedInput: return "kMalformedInput";
    case StoreError::kUnknownSchema: return "kUnknownSchema";
    case StoreError::kInvalidEntry: return "kInvalidEntry";
  }
  return "kMalformedInput";
}

bool ValidateIdentityList(const JsonValue& data, std::string* error) {
  if (!data.is_object()) { *error = "identity-list: data must be an object"; return false; }
  const JsonValue* ids = data.find("identities");
  if (ids == nullptr || !ids->is_array()) {
    *error = "identity-list: 'identities' array required";
    return false;
  }
  if (data.as_object().size() != 1) {
    *error = "identity-list: unexpected fields in data";
    return false;
  }
  for (const auto& e : ids->as_array()) {
    if (!e.is_object()) { *error = "identity-list: entry not an object"; return false; }
    std::string value;
    if (!ObjFieldString(e, "value", &value, error, "identity-list")) return false;
    if (value.empty() || value.rfind("xr:", 0) != 0) {
      *error = "identity-list: 'value' must be an xr: id";
      return false;
    }
    const JsonValue* storage = e.find("storage");
    if (storage != nullptr) {
      if (!storage->is_string() ||
          (storage->as_string() != "persistent" && storage->as_string() != "in_memory")) {
        *error = "identity-list: 'storage' must be persistent|in_memory";
        return false;
      }
    }
    const JsonValue* kind = e.find("kind");
    if (kind != nullptr) {
      if (!kind->is_string() ||
          (kind->as_string() != "standard" && kind->as_string() != "fortress")) {
        *error = "identity-list: 'kind' must be standard|fortress";
        return false;
      }
    }
  }
  return true;
}

bool ValidateTrustBindings(const JsonValue& data, std::string* error) {
  if (!data.is_object()) { *error = "trust-bindings: data must be an object"; return false; }
  const JsonValue* bs = data.find("bindings");
  if (bs == nullptr || !bs->is_array()) {
    *error = "trust-bindings: 'bindings' array required";
    return false;
  }
  if (data.as_object().size() != 1) {
    *error = "trust-bindings: unexpected fields in data";
    return false;
  }
  for (const auto& e : bs->as_array()) {
    if (!e.is_object()) { *error = "trust-bindings: entry not an object"; return false; }
    std::string domain, trust;
    if (!ObjFieldString(e, "domain", &domain, error, "trust-bindings")) return false;
    if (domain.empty()) { *error = "trust-bindings: 'domain' empty"; return false; }
    const JsonValue* id = e.find("identity");
    if (id != nullptr && !id->is_string()) {
      *error = "trust-bindings: 'identity' must be a string";
      return false;
    }
    if (!ObjFieldString(e, "trust", &trust, error, "trust-bindings")) return false;
    if (!IsTrustTier(trust)) { *error = "trust-bindings: bad 'trust' tier"; return false; }
  }
  return true;
}

bool ValidateExceptions(const JsonValue& data, int version, std::string* error) {
  if (!data.is_object()) { *error = "exceptions: data must be an object"; return false; }
  const JsonValue* xs = data.find("exceptions");
  if (xs == nullptr || !xs->is_array()) {
    *error = "exceptions: 'exceptions' array required";
    return false;
  }
  if (data.as_object().size() != 1) {
    *error = "exceptions: unexpected fields in data";
    return false;
  }
  for (const auto& e : xs->as_array()) {
    if (!e.is_object()) { *error = "exceptions: entry not an object"; return false; }
    std::string id, domain, scope, trust;
    if (!ObjFieldString(e, "id", &id, error, "exceptions")) return false;
    if (id.empty()) { *error = "exceptions: 'id' empty"; return false; }
    if (!ObjFieldString(e, "domain", &domain, error, "exceptions")) return false;
    if (domain.empty()) { *error = "exceptions: 'domain' empty"; return false; }
    const JsonValue* ident = e.find("identity");
    if (ident != nullptr && !ident->is_string()) {
      *error = "exceptions: 'identity' must be a string";
      return false;
    }
    if (!ObjFieldString(e, "scope", &scope, error, "exceptions")) return false;
    if (!IsScope(scope)) { *error = "exceptions: bad 'scope'"; return false; }
    if (!ObjFieldString(e, "trust", &trust, error, "exceptions")) return false;
    if (!IsTrustTier(trust)) { *error = "exceptions: bad 'trust' tier"; return false; }
    // Expiry mechanics: 7d entries MUST carry expires_at (never "forever"
    // by omission); once MUST carry remaining_uses; session MUST carry
    // session_id. permanent MUST carry expires_at == 0 (no time-based
    // expiry — its lifetime is the Settings entry itself).
    auto int_field = [&](const char* k, int64_t* dst) {
      const JsonValue* f = e.find(k);
      if (f == nullptr || !f->is_int()) return false;
      *dst = f->as_int();
      return true;
    };
    int64_t expires_at = 0, remaining = 0;
    if (!int_field("expires_at", &expires_at)) {
      *error = "exceptions: 'expires_at' integer required";
      return false;
    }
    if (!int_field("remaining_uses", &remaining)) {
      *error = "exceptions: 'remaining_uses' integer required";
      return false;
    }
    if (scope == "7d" && expires_at <= 0) {
      *error = "exceptions: 7d scope requires a positive expires_at";
      return false;
    }
    if (scope == "once" && remaining <= 0) {
      *error = "exceptions: once scope requires positive remaining_uses";
      return false;
    }
    const JsonValue* sid = e.find("session_id");
    if (scope == "session") {
      if (sid == nullptr || !sid->is_string() || sid->as_string().empty()) {
        *error = "exceptions: session scope requires session_id";
        return false;
      }
    }
    if (version >= 2) {
      std::string granted_by;
      if (!ObjFieldString(e, "granted_by", &granted_by, error, "exceptions")) return false;
      if (granted_by != "in_flow" && granted_by != "settings") {
        *error = "exceptions: 'granted_by' must be in_flow|settings";
        return false;
      }
      // THE LAW (Plan §1.5 / §11.9): in-flow grants are never permanent.
      if (scope == "permanent" && granted_by != "settings") {
        *error = "exceptions: permanent scope requires granted_by=settings "
                 "(permanent exceptions live solely in Settings)";
        return false;
      }
    }
  }
  return true;
}

JsonValue StoreDoc::ToJson() const {
  JsonValue::Object o;
  o.emplace("schema", JsonValue(schema));
  o.emplace("schema_version", JsonValue(static_cast<int64_t>(schema_version)));
  o.emplace("created_at", JsonValue(created_at));
  o.emplace("data", data);
  return JsonValue(std::move(o));
}

std::map<std::string, int> PolicyStore::DefaultKnownVersions() {
  return {{"xr-identity-list", 1}, {"xr-trust-bindings", 1}, {"xr-exceptions", 2}};
}

PolicyStore::PolicyStore(std::map<std::string, int> known) : known_(std::move(known)) {}

int PolicyStore::KnownVersion(const std::string& schema) const {
  auto it = known_.find(schema);
  return it == known_.end() ? -1 : it->second;
}

StoreLoadResult PolicyStore::Load(const std::string& path) const {
  StoreLoadResult r;
  r.path = path;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    r.error = StoreError::kIoError;
    r.error_detail = "cannot open file";
    return r;
  }
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  std::fclose(f);
  auto parsed = ParseJson(data);
  if (!parsed.ok) {
    r.error = StoreError::kMalformedInput;
    r.error_detail = "JSON parse: " + parsed.error;
    return r;
  }
  if (!parsed.value.is_object()) {
    r.error = StoreError::kMalformedInput;
    r.error_detail = "root not an object";
    return r;
  }
  const JsonValue* schema = parsed.value.find("schema");
  const JsonValue* sv = parsed.value.find("schema_version");
  const JsonValue* ca = parsed.value.find("created_at");
  const JsonValue* d = parsed.value.find("data");
  if (schema == nullptr || !schema->is_string() || sv == nullptr || !sv->is_int() ||
      ca == nullptr || !ca->is_int() || d == nullptr || !d->is_object()) {
    r.error = StoreError::kMalformedInput;
    r.error_detail = "envelope requires schema/schema_version/created_at/data";
    return r;
  }
  if (parsed.value.as_object().size() != 4) {
    r.error = StoreError::kMalformedInput;
    r.error_detail = "envelope has unexpected fields";
    return r;
  }
  int known = KnownVersion(schema->as_string());
  if (known < 0) {
    r.error = StoreError::kUnknownSchema;
    r.error_detail = "unknown schema id: " + schema->as_string();
    return r;
  }
  r.doc.schema = schema->as_string();
  r.doc.schema_version = static_cast<int>(sv->as_int());
  r.doc.created_at = ca->as_int();
  r.doc.data = *d;
  if (r.doc.schema_version > known) {
    // Downgrade path: newer doc, older binary. Data-preserving no-op: keep
    // raw, never rewrite, never enforce unknown shapes (fail closed).
    r.doc.future_version = true;
    r.ok = true;
    return r;
  }
  if (r.doc.schema_version < 1) {
    r.error = StoreError::kMalformedInput;
    r.error_detail = "schema_version must be >= 1";
    return r;
  }
  std::string err;
  bool valid = false;
  if (r.doc.schema == "xr-identity-list") valid = ValidateIdentityList(r.doc.data, &err);
  else if (r.doc.schema == "xr-trust-bindings") valid = ValidateTrustBindings(r.doc.data, &err);
  else if (r.doc.schema == "xr-exceptions") valid = ValidateExceptions(r.doc.data, r.doc.schema_version, &err);
  if (!valid) {
    r.error = StoreError::kInvalidEntry;
    r.error_detail = err;
    return r;
  }
  r.ok = true;
  return r;
}

bool PolicyStore::Save(const std::string& path, const StoreDoc& doc, std::string* error) const {
  std::string blob = doc.ToJson().Canonical();
  std::string tmp = path + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    *error = "cannot open temp file " + tmp;
    return false;
  }
  size_t wrote = std::fwrite(blob.data(), 1, blob.size(), f);
  std::fclose(f);
  if (wrote != blob.size()) {
    *error = "short write";
    return false;
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    *error = "rename failed";
    return false;
  }
  return true;
}

PolicyStore::MigrateResult PolicyStore::MigrateToLatest(const StoreDoc& doc) const {
  MigrateResult r;
  int known = KnownVersion(doc.schema);
  if (known < 0) {
    r.error = "unknown schema";
    return r;
  }
  r.doc = doc;
  if (doc.future_version || doc.schema_version >= known) {
    // Future docs: never rewritten. Current docs: nothing to do.
    r.ok = true;
    return r;
  }
  // Forward chain: v(n) -> v(n+1) -> ... -> known.
  while (r.doc.schema_version < known) {
    if (r.doc.schema == "xr-exceptions" && r.doc.schema_version == 1) {
      // v1 -> v2: add granted_by. In-flow scopes (once/session/7d) map to
      // "in_flow"; permanent maps to "settings" (Settings-pointer
      // semantics — a permanent entry's lifetime IS the Settings entry).
      JsonValue::Object data = r.doc.data.as_object();
      auto it = data.find("exceptions");
      if (it == data.end() || !it->second.is_array()) {
        r.error = "exceptions v1 data malformed";
        return r;
      }
      JsonValue::Array xs = it->second.as_array();
      for (auto& x : xs) {
        if (!x.is_object()) { r.error = "exceptions v1 entry malformed"; return r; }
        JsonValue::Object xo = x.as_object();
        std::string scope = xo.count("scope") && xo["scope"].is_string()
                                ? xo["scope"].as_string() : std::string();
        xo.emplace("granted_by",
                   JsonValue(std::string(scope == "permanent" ? "settings" : "in_flow")));
        x = JsonValue(std::move(xo));
      }
      data["exceptions"] = JsonValue(std::move(xs));
      r.doc.data = JsonValue(std::move(data));
      r.doc.schema_version = 2;
      r.changed = true;
      continue;
    }
    r.error = "no migration path from schema_version " + std::to_string(r.doc.schema_version);
    return r;
  }
  std::string err;
  if (r.doc.schema == "xr-exceptions" && !ValidateExceptions(r.doc.data, r.doc.schema_version, &err)) {
    r.error = "migrated doc failed validation: " + err;
    return r;
  }
  r.ok = true;
  return r;
}

}  // namespace xr::policy
