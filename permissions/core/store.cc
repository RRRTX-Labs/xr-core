// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — overlay store load/serialize/project (see store.h). The
// validators are deliberately exhaustive: every key set is EXACT, every
// scope admits exactly its own fields, and any mismatch returns the
// fully-denying store.
#include "permissions/core/store.h"

#include <initializer_list>
#include <set>
#include <utility>

#include "permissions/core/envelope.h"
#include "permissions/core/json.h"

namespace xr::permissions {
namespace {

constexpr const char* kStoreContract = "permission-store";
constexpr int64_t kStoreVersion = 1;

// Exact key set: same size and every expected key present (map keys unique).
bool KeysExactly(const JsonValue& o, std::initializer_list<const char*> keys) {
  if (!o.is_object()) return false;
  const auto& m = o.as_object();
  if (m.size() != keys.size()) return false;
  for (const char* k : keys) {
    if (m.find(k) == m.end()) return false;
  }
  return true;
}

bool GetStr(const JsonValue& o, const char* k, std::string* out) {
  const JsonValue* f = o.find(k);
  if (f == nullptr || !f->is_string()) return false;
  *out = f->as_string();
  return true;
}

bool GetInt(const JsonValue& o, const char* k, int64_t* out) {
  const JsonValue* f = o.find(k);
  if (f == nullptr || !f->is_int()) return false;
  *out = f->as_int();
  return true;
}

bool GetBool(const JsonValue& o, const char* k, bool* out) {
  const JsonValue* f = o.find(k);
  if (f == nullptr || !f->is_bool()) return false;
  *out = f->as_bool();
  return true;
}

// One grant. Scope decides which fields may be non-trivial; the others must be
// their zero value, so a record cannot smuggle an unused field.
bool ParseGrant(const JsonValue& g, const std::string& identity_key,
                std::set<std::string>* ids, StoredGrant* out) {
  if (!KeysExactly(g, {"id", "identity", "domain", "capability", "scope", "created_at",
                       "expires_at", "session_id", "remaining_uses", "revoked",
                       "revoked_at"})) {
    return false;
  }
  std::string cap_name, scope_name;
  StoredGrant r;
  if (!GetStr(g, "id", &r.id) || r.id.empty()) return false;
  if (!ids->insert(r.id).second) return false;  // duplicate grant id => corrupt
  if (!GetStr(g, "identity", &r.identity) || r.identity != identity_key) return false;
  if (!GetStr(g, "domain", &r.domain) || r.domain.empty()) return false;
  if (!GetStr(g, "capability", &cap_name) || !ParseCapability(cap_name, &r.capability)) return false;
  if (!GetStr(g, "scope", &scope_name) || !ParseScope(scope_name, &r.scope)) return false;
  if (!GetInt(g, "created_at", &r.created_at)) return false;
  if (!GetInt(g, "expires_at", &r.expires_at)) return false;
  if (!GetStr(g, "session_id", &r.session_id)) return false;
  if (!GetInt(g, "remaining_uses", &r.remaining_uses)) return false;
  if (!GetBool(g, "revoked", &r.revoked)) return false;
  if (!GetInt(g, "revoked_at", &r.revoked_at)) return false;

  switch (r.scope) {
    case Scope::kOnce:
      // A once grant is a single use; 0 means spent (kept only as a tombstone).
      if (r.expires_at != 0 || !r.session_id.empty()) return false;
      if (r.remaining_uses < 0 || r.remaining_uses > 1) return false;
      break;
    case Scope::kSession:
      if (r.expires_at != 0 || r.remaining_uses != 0 || r.session_id.empty()) return false;
      break;
    case Scope::k7d:
      // The TTL is fixed by the scope; a longer lifetime cannot be encoded.
      if (r.remaining_uses != 0 || !r.session_id.empty()) return false;
      if (r.expires_at != r.created_at + kSevenDaysMillis) return false;
      break;
  }
  if (!r.revoked && r.revoked_at != 0) return false;
  *out = std::move(r);
  return true;
}

bool ParseIdentity(const JsonValue& rec, const std::string& key,
                   std::set<std::string>* ids, IdentityRecord* out) {
  if (!KeysExactly(rec, {"denied", "defaults", "extras", "grants"})) return false;
  IdentityRecord r;
  if (!GetBool(rec, "denied", &r.denied)) return false;

  const JsonValue* defaults = rec.find("defaults");
  if (defaults == nullptr || !defaults->is_object()) return false;
  for (const auto& [name, val] : defaults->as_object()) {
    Capability c;
    CapState st;
    if (!ParseCapability(name, &c) || !val.is_string() || !ParseCapState(val.as_string(), &st)) {
      return false;
    }
    r.defaults.emplace(c, st);
  }

  const JsonValue* extras = rec.find("extras");
  if (extras == nullptr || !extras->is_object()) return false;
  for (const auto& [name, val] : extras->as_object()) {
    CapState st;
    if (!IsExtraCapability(name) || !val.is_string() || !ParseExtraState(val.as_string(), &st)) {
      return false;  // includes kAllow: an extra can never be recorded as allow
    }
    r.extras.emplace(name, st);
  }

  const JsonValue* grants = rec.find("grants");
  if (grants == nullptr || !grants->is_array()) return false;
  for (const auto& g : grants->as_array()) {
    StoredGrant sg;
    if (!ParseGrant(g, key, ids, &sg)) return false;
    r.grants.push_back(std::move(sg));
  }
  *out = std::move(r);
  return true;
}

bool ParseStore(const JsonValue& root, Store* out) {
  if (!KeysExactly(root, {"contract", "contract_version", "identities", "next_seq"})) return false;
  std::string contract;
  int64_t version = 0, next_seq = 0;
  if (!GetStr(root, "contract", &contract) || contract != kStoreContract) return false;
  if (!GetInt(root, "contract_version", &version) || version != kStoreVersion) return false;
  if (!GetInt(root, "next_seq", &next_seq) || next_seq < 1) return false;

  const JsonValue* ids = root.find("identities");
  if (ids == nullptr || !ids->is_object()) return false;
  Store s;
  s.next_seq = static_cast<uint64_t>(next_seq);
  std::set<std::string> grant_ids;
  for (const auto& [key, rec] : ids->as_object()) {
    if (key.empty()) return false;
    IdentityRecord ir;
    if (!ParseIdentity(rec, key, &grant_ids, &ir)) return false;
    s.identities.emplace(key, std::move(ir));
  }
  *out = std::move(s);
  return true;
}

JsonValue GrantJson(const StoredGrant& g) {
  JsonValue::Object o;
  o.emplace("id", JsonValue(g.id));
  o.emplace("identity", JsonValue(g.identity));
  o.emplace("domain", JsonValue(g.domain));
  o.emplace("capability", JsonValue(CapabilityName(g.capability)));
  o.emplace("scope", JsonValue(ScopeName(g.scope)));
  o.emplace("created_at", JsonValue(static_cast<int64_t>(g.created_at)));
  o.emplace("expires_at", JsonValue(static_cast<int64_t>(g.expires_at)));
  o.emplace("session_id", JsonValue(g.session_id));
  o.emplace("remaining_uses", JsonValue(static_cast<int64_t>(g.remaining_uses)));
  o.emplace("revoked", JsonValue(g.revoked));
  o.emplace("revoked_at", JsonValue(static_cast<int64_t>(g.revoked_at)));
  return JsonValue(std::move(o));
}

}  // namespace

Store LoadStore(std::string_view text) {
  Store bad;
  bad.corrupt = true;
  JsonParseResult pr = ParseJson(text);
  if (!pr.ok) return bad;                               // truncated / garbage
  if (pr.value.Canonical() != text) return bad;         // not our bytes: dup keys, order, whitespace
  Store out;
  if (!ParseStore(pr.value, &out)) return bad;          // schema
  return out;
}

std::string SerializeStore(const Store& s) {
  // A corrupt store must stay corrupt on disk. Serializing it as an empty
  // (inert) store would repair it silently and revert every identity to the
  // tier table, which is a widening. This marker fails the strict schema, so
  // LoadStore reads it back as corrupt, and only an explicit reset clears it.
  if (s.corrupt) return "{\"contract\":\"permission-store\",\"corrupt\":true}";
  JsonValue::Object root;
  root.emplace("contract", JsonValue(kStoreContract));
  root.emplace("contract_version", JsonValue(static_cast<int64_t>(kStoreVersion)));
  root.emplace("next_seq", JsonValue(static_cast<int64_t>(s.next_seq)));
  JsonValue::Object ids;
  for (const auto& [key, rec] : s.identities) {
    JsonValue::Object defaults;
    for (const auto& [cap, st] : rec.defaults) {
      defaults.emplace(CapabilityName(cap), JsonValue(CapStateName(st)));
    }
    JsonValue::Object extras;
    for (const auto& [name, st] : rec.extras) {
      extras.emplace(name, JsonValue(CapStateName(st)));
    }
    JsonValue::Array grants;
    for (const auto& g : rec.grants) grants.push_back(GrantJson(g));
    JsonValue::Object one;
    one.emplace("denied", JsonValue(rec.denied));
    one.emplace("defaults", JsonValue(std::move(defaults)));
    one.emplace("extras", JsonValue(std::move(extras)));
    one.emplace("grants", JsonValue(std::move(grants)));
    ids.emplace(key, JsonValue(std::move(one)));
  }
  root.emplace("identities", JsonValue(std::move(ids)));
  return JsonValue(std::move(root)).Canonical();
}

std::string ProjectViewJson(const Store& s) {
  JsonValue::Object view;
  view.emplace("contract_version", JsonValue(static_cast<int64_t>(1)));
  if (s.corrupt) {
    view.emplace("corrupt", JsonValue(true));
    return JsonValue(std::move(view)).Canonical();
  }
  JsonValue::Array denied, defaults, grants;
  for (const auto& [identity, rec] : s.identities) {
    if (rec.denied) denied.push_back(JsonValue(identity));
    for (const auto& [cap, st] : rec.defaults) {
      JsonValue::Object d;
      d.emplace("identity", JsonValue(identity));
      d.emplace("capability", JsonValue(CapabilityName(cap)));
      d.emplace("state", JsonValue(CapStateName(st)));
      defaults.push_back(JsonValue(std::move(d)));
    }
    for (const auto& g : rec.grants) {
      if (g.revoked) continue;                                        // tombstoned
      if (g.scope == Scope::kOnce && g.remaining_uses <= 0) continue;  // spent
      JsonValue::Object o;
      o.emplace("identity", JsonValue(g.identity));
      o.emplace("domain", JsonValue(g.domain));
      o.emplace("capability", JsonValue(CapabilityName(g.capability)));
      o.emplace("scope", JsonValue(ScopeName(g.scope)));
      // Only the field its scope admits (the resolver's parser is strict).
      if (g.scope == Scope::kOnce) {
        o.emplace("remaining_uses", JsonValue(static_cast<int64_t>(g.remaining_uses)));
      } else if (g.scope == Scope::kSession) {
        o.emplace("session_id", JsonValue(g.session_id));
      } else {
        o.emplace("expires_at", JsonValue(static_cast<int64_t>(g.expires_at)));
      }
      grants.push_back(JsonValue(std::move(o)));
    }
  }
  view.emplace("denied_identities", JsonValue(std::move(denied)));
  view.emplace("defaults", JsonValue(std::move(defaults)));
  view.emplace("grants", JsonValue(std::move(grants)));
  return JsonValue(std::move(view)).Canonical();
}

}  // namespace xr::permissions
