// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — PolicyResolverService implementation (see service.h).
#include "policy/core/service.h"

#include <sstream>

namespace xr::policy {

namespace {

std::string StateKeyOf(const SnapshotEntry& e) {
  return e.identity + "|" + e.site + "|" + e.trust;
}

// Extracts the request's effective tier for cache/state keying. After a
// successful resolve we KNOW the tier; recomputing it here for the key
// would duplicate resolver logic — instead the service keys on the resolved
// OUTPUT by re-deriving from the request via a resolve of its own. To keep
// one brain, we key state by the request's *trust input* (explicit trust or
// the sentinel "auto"), and store the resolved policy; cache invalidation
// covers layer changes.
std::string RequestTrustKey(const ResolveRequest& req) {
  return req.has_trust ? req.trust : std::string("auto");
}

}  // namespace

PolicyResolverService::PolicyResolverService(std::string store_dir)
    : store_dir_(std::move(store_dir)), store_(PolicyStore::DefaultKnownVersions()),
      cache_(1024) {
  LoadStores();
}

void PolicyResolverService::LoadStores() {
  if (store_dir_.empty()) return;
  has_store_ = true;
  auto load = [&](const char* file, StoreLoadResult* slot) {
    std::string path = store_dir_ + "/" + file;
    *slot = store_.Load(path);
    if (!slot->ok) {
      if (slot->error == StoreError::kIoError) {
        // Absent file: fine — no layer (fail closed, nothing widened).
        return;
      }
      diag_.ledger_rows.push_back("store: " + std::string(file) + " rejected (" +
                                  ToString(slot->error) + ": " + slot->error_detail + ")");
      return;
    }
    if (slot->doc.future_version) {
      diag_.ledger_rows.push_back("store: " + std::string(file) +
                                  " future schema_version — data-preserving no-op (not enforced, not rewritten)");
      return;
    }
    // Forward migration to latest known (data upgrade, deterministic).
    auto m = store_.MigrateToLatest(slot->doc);
    if (!m.ok) {
      diag_.ledger_rows.push_back("store: " + std::string(file) + " migration failed: " + m.error);
      slot->ok = false;
      return;
    }
    if (m.changed) {
      slot->doc = m.doc;  // used in-memory; persisted only on explicit Save
      diag_.ledger_rows.push_back("store: " + std::string(file) + " migrated forward to v" +
                                  std::to_string(m.doc.schema_version) + " (in-memory; rewrite on next Save)");
    }
  };
  load("identity-list-v1.json", &diag_.identity_list);
  load("trust-bindings-v1.json", &diag_.trust_bindings);
  load("exceptions-v1.json", &diag_.exceptions);

  diag_.managed = LoadManagedPolicy(store_dir_ + "/managed-policy.json",
                                    store_dir_ + "/managed-policy.pub");
  for (const auto& row : diag_.managed.ledger_rows) diag_.ledger_rows.push_back(row);
}

JsonValue PolicyResolverService::MergeStoreLayers(const JsonValue& request) const {
  // Request-carried fields WIN over store layers (explicit > ambient).
  if (!request.is_object()) return request;
  JsonValue::Object merged = request.as_object();
  auto add_under = [&](const char* layer, const JsonValue* store_data, const char* array_key) {
    if (store_data == nullptr || !store_data->is_object()) return;
    const JsonValue* arr = store_data->find(array_key);
    if (arr == nullptr || !arr->is_array()) return;
    if (merged.find(layer) != merged.end()) return;  // request wins
    merged.emplace(layer, *arr);
  };
  if (diag_.identity_list.ok && !diag_.identity_list.doc.future_version) {
    const JsonValue* ids = diag_.identity_list.doc.data.find("identities");
    if (ids != nullptr && ids->is_array() && merged.find("identities") == merged.end()) {
      // Translate identity-list-v1 entries to resolver IdentityInfo shape.
      JsonValue::Array shim;
      for (const auto& e : ids->as_array()) {
        if (!e.is_object()) continue;
        JsonValue::Object o;
        if (const JsonValue* v = e.find("value"); v != nullptr && v->is_string())
          o.emplace("value", *v);
        if (const JsonValue* s = e.find("storage"); s != nullptr && s->is_string())
          o.emplace("storage", *s);
        if (const JsonValue* k = e.find("kind"); k != nullptr && k->is_string())
          o.emplace("kind", *k);
        shim.push_back(JsonValue(std::move(o)));
      }
      merged.emplace("identities", JsonValue(std::move(shim)));
    }
  }
  if (diag_.trust_bindings.ok && !diag_.trust_bindings.doc.future_version) {
    add_under("bindings", &diag_.trust_bindings.doc.data, "bindings");
  }
  if (diag_.exceptions.ok && !diag_.exceptions.doc.future_version) {
    add_under("exceptions", &diag_.exceptions.doc.data, "exceptions");
  }
  if (diag_.managed.enforced && merged.find("enterprise") == merged.end()) {
    JsonValue::Object ent;
    ent.emplace("present", JsonValue(true));
    if (!diag_.managed.policy.trust_floor.empty())
      ent.emplace("trust_floor", JsonValue(diag_.managed.policy.trust_floor));
    if (!diag_.managed.policy.force_fingerprint.empty())
      ent.emplace("force_fingerprint", JsonValue(diag_.managed.policy.force_fingerprint));
    if (!diag_.managed.policy.force_route.empty())
      ent.emplace("force_route", JsonValue(diag_.managed.policy.force_route));
    if (diag_.managed.policy.force_letterbox)
      ent.emplace("force_letterbox", JsonValue(true));
    if (diag_.managed.policy.force_block_third_party)
      ent.emplace("force_block_third_party", JsonValue(true));
    merged.emplace("enterprise", JsonValue(std::move(ent)));
  }
  return JsonValue(std::move(merged));
}

JsonValue PolicyResolverService::ResolveJson(const JsonValue& request) {
  JsonValue effective = has_store_ ? MergeStoreLayers(request) : request;
  RequestParse rp = ParseResolveRequest(effective);
  for (const auto& d : rp.dropped) diag_.ledger_rows.push_back("request: " + d);
  ResolveOutput out = Resolve(rp.request);

  if (out.ok) {
    // Cache + state accumulation (keyed by request trust input).
    const std::string trust_key = RequestTrustKey(rp.request);
    PolicyCacheKey ck{rp.request.identity, rp.request.registrable_domain, trust_key};
    auto policy = std::make_shared<const EffectivePolicy>(out.policy);
    cache_.Put(ck, policy);
    SnapshotEntry e;
    e.identity = rp.request.identity;
    e.site = rp.request.registrable_domain;
    e.trust = trust_key;
    e.policy = out.policy;
    std::lock_guard<std::mutex> lock(mu_);
    state_[StateKeyOf(e)] = std::move(e);
  }
  return ResolveOutputToJson(out);
}

std::string PolicyResolverService::ResolveText(const std::string& request_json) {
  auto parsed = ParseJson(request_json);
  if (!parsed.ok) {
    // Malformed wire input: total + deny-safe (fake protocol parity).
    ResolveOutput deny;
    deny.ok = true;
    deny.policy = EffectivePolicy();
    return ResolveOutputToJson(deny).Canonical();
  }
  return ResolveJson(parsed.value).Canonical();
}

SnapshotEncodeResult PolicyResolverService::SnapshotFull() {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SnapshotEntry> entries;
  for (const auto& [_, e] : state_) entries.push_back(e);
  ++seq_;
  auto r = EncodeFullSnapshot(seq_, std::move(entries));
  if (r.ok) {
    last_snapshot_seq_ = seq_;
    last_snapshot_state_ = DecodeFullSnapshot(r.blob).entries;
  }
  return r;
}

SnapshotEncodeResult PolicyResolverService::SnapshotDiff() {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SnapshotEntry> entries;
  for (const auto& [_, e] : state_) entries.push_back(e);
  ++seq_;
  auto r = EncodeDiffSnapshot(seq_, last_snapshot_seq_, last_snapshot_state_, entries);
  if (r.ok) {
    last_snapshot_seq_ = seq_;
    last_snapshot_state_ = entries;
  }
  return r;
}

JsonValue PolicyResolverService::DumpJson(bool include_managed) const {
  JsonValue::Object root;
  JsonValue::Object cache;
  PolicyCache::Stats s = cache_.GetStats();
  cache.emplace("hits", JsonValue(static_cast<int64_t>(s.hits)));
  cache.emplace("misses", JsonValue(static_cast<int64_t>(s.misses)));
  cache.emplace("evictions", JsonValue(static_cast<int64_t>(s.evictions)));
  cache.emplace("invalidations_all", JsonValue(static_cast<int64_t>(s.invalidations_all)));
  cache.emplace("invalidations_identity", JsonValue(static_cast<int64_t>(s.invalidations_identity)));
  cache.emplace("size", JsonValue(static_cast<int64_t>(s.size)));
  cache.emplace("global_generation", JsonValue(static_cast<int64_t>(s.global_generation)));
  root.emplace("cache", JsonValue(std::move(cache)));

  JsonValue::Object store;
  auto doc_summary = [](const StoreLoadResult& lr) {
    JsonValue::Object o;
    o.emplace("ok", JsonValue(lr.ok));
    if (lr.ok) {
      o.emplace("schema", JsonValue(lr.doc.schema));
      o.emplace("schema_version", JsonValue(static_cast<int64_t>(lr.doc.schema_version)));
      o.emplace("future_version", JsonValue(lr.doc.future_version));
    } else {
      o.emplace("error", JsonValue(std::string(ToString(lr.error))));
      o.emplace("detail", JsonValue(lr.error_detail));
    }
    return o;
  };
  store.emplace("identity_list", JsonValue(doc_summary(diag_.identity_list)));
  store.emplace("trust_bindings", JsonValue(doc_summary(diag_.trust_bindings)));
  store.emplace("exceptions", JsonValue(doc_summary(diag_.exceptions)));
  root.emplace("store", JsonValue(std::move(store)));

  if (include_managed) {
    JsonValue::Object m;
    m.emplace("status", JsonValue(std::string(ToString(diag_.managed.status))));
    m.emplace("enforced", JsonValue(diag_.managed.enforced));
    if (diag_.managed.enforced) {
      JsonValue::Object e;
      e.emplace("trust_floor", JsonValue(diag_.managed.policy.trust_floor));
      e.emplace("force_fingerprint", JsonValue(diag_.managed.policy.force_fingerprint));
      e.emplace("force_route", JsonValue(diag_.managed.policy.force_route));
      e.emplace("force_letterbox", JsonValue(diag_.managed.policy.force_letterbox));
      e.emplace("force_block_third_party", JsonValue(diag_.managed.policy.force_block_third_party));
      m.emplace("enterprise", JsonValue(std::move(e)));
    }
    m.emplace("detail", JsonValue(diag_.managed.detail));
    JsonValue::Array rows;
    for (const auto& row : diag_.managed.ledger_rows) rows.push_back(JsonValue(row));
    m.emplace("ledger", JsonValue(std::move(rows)));
    root.emplace("managed", JsonValue(std::move(m)));
  }

  JsonValue::Array ledger;
  for (const auto& row : diag_.ledger_rows) ledger.push_back(JsonValue(row));
  root.emplace("ledger", JsonValue(std::move(ledger)));

  {
    std::lock_guard<std::mutex> lock(mu_);
    root.emplace("snapshot_seq", JsonValue(static_cast<int64_t>(seq_)));
    root.emplace("state_entries", JsonValue(static_cast<int64_t>(state_.size())));
  }
  return JsonValue(std::move(root));
}

std::string PolicyResolverService::Dump(bool include_managed) const {
  JsonValue j = DumpJson(include_managed);
  std::ostringstream out;
  out << "== xr policy service ==\n";
  if (const JsonValue* it = j.find("cache"); it != nullptr) {
    out << "cache: hits=" << it->find("hits")->as_int()
        << " misses=" << it->find("misses")->as_int()
        << " size=" << it->find("size")->as_int()
        << " gen=" << it->find("global_generation")->as_int() << "\n";
  }
  if (const JsonValue* v = j.find("snapshot_seq"); v != nullptr)
    out << "snapshot_seq: " << v->as_int() << "\n";
  if (const JsonValue* v = j.find("state_entries"); v != nullptr)
    out << "state_entries: " << v->as_int() << "\n";
  if (include_managed) {
    if (const JsonValue* m = j.find("managed"); m != nullptr) {
      out << "managed: status=" << m->find("status")->as_string()
          << " enforced=" << (m->find("enforced")->as_bool() ? "true" : "false") << "\n";
      if (const JsonValue* lg = m->find("ledger"); lg != nullptr) {
        for (const auto& row : lg->as_array()) out << "  ledger: " << row.as_string() << "\n";
      }
    }
  }
  if (const JsonValue* ledger = j.find("ledger"); ledger != nullptr) {
    for (const auto& row : ledger->as_array()) out << "ledger: " << row.as_string() << "\n";
  }
  return out.str();
}

}  // namespace xr::policy
