// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/scope — see scope.h. Refusal tokens are closed
// vocabulary, mirrored byte-for-byte by the Python fake.
#include "shield/core/scope.h"

namespace xr::shield {

using common::JsonValue;

namespace {

bool KnownKey(const JsonValue& obj, const char* const* allowed, size_t n,
              std::string* detail) {
  for (const auto& kv : obj.as_object()) {
    bool ok = false;
    for (size_t i = 0; i < n; ++i) {
      if (kv.first == allowed[i]) { ok = true; break; }
    }
    if (!ok) {
      *detail = "unknown-field:" + kv.first;
      return false;
    }
  }
  return true;
}

}  // namespace

ScopeResult ParseScopeSet(const JsonValue& args, ScopeSet* out,
                          std::string* detail) {
  *out = ScopeSet{};
  if (!args.is_object()) {
    *detail = "scopes-args-not-object";
    return ScopeResult::kMalformed;
  }
  static const char* kTop[] = {"scopes"};
  if (!KnownKey(args, kTop, 1, detail)) return ScopeResult::kUnknownField;
  const JsonValue* arr = args.find("scopes");
  if (arr == nullptr || !arr->is_array()) {
    *detail = "scopes-not-array";
    return ScopeResult::kMalformed;
  }
  static const char* kScope[] = {"scope_id", "identity", "site", "workspace",
                                 "rule_id", "list_id", "expiry_mono",
                                 "reason"};
  for (const JsonValue& sv : arr->as_array()) {
    if (!sv.is_object()) {
      *detail = "scope-not-object";
      return ScopeResult::kMalformed;
    }
    if (!KnownKey(sv, kScope, 8, detail)) return ScopeResult::kUnknownField;
    ExceptionScope s;
    const JsonValue* idv = sv.find("scope_id");
    if (idv == nullptr || !idv->is_string() || idv->as_string().empty()) {
      *detail = "bad-scope-id";
      return ScopeResult::kMalformed;
    }
    s.scope_id = idv->as_string();
    for (const auto& prev : out->scopes) {
      if (prev.scope_id == s.scope_id) {
        *detail = "duplicate-scope-id:" + s.scope_id;
        return ScopeResult::kDuplicateScopeId;
      }
    }
    for (const char* key : {"identity", "site", "workspace", "rule_id",
                            "list_id"}) {
      const JsonValue* v = sv.find(key);
      if (v == nullptr) continue;  // absent = any
      if (!v->is_string()) {
        *detail = std::string("field-not-string:") + key;
        return ScopeResult::kMalformed;
      }
      std::string* tgt =
          key[0] == 'i' ? &s.identity
          : key[0] == 's' && key[1] == 'i' ? &s.site
          : key[0] == 'w' ? &s.workspace
          : key[0] == 'r' ? &s.rule_id : &s.list_id;
      *tgt = v->as_string();
    }
    const JsonValue* ex = sv.find("expiry_mono");
    if (ex != nullptr) {
      if (!ex->is_int() || ex->as_int() < -1) {
        *detail = "bad-expiry";
        return ScopeResult::kMalformed;
      }
      s.expiry_mono = ex->as_int();
    }
    const JsonValue* rs = sv.find("reason");
    if (rs == nullptr || !rs->is_string() || rs->as_string().empty()) {
      *detail = "missing-reason:" + s.scope_id;
      return ScopeResult::kMissingReason;
    }
    s.reason = rs->as_string();
    out->scopes.push_back(std::move(s));
  }
  return ScopeResult::kOk;
}

bool Covers(const ExceptionScope& scope, const RequestContext& ctx,
            const EngineHit& hit, long long now_mono) {
  if (scope.expiry_mono >= 0 && now_mono >= scope.expiry_mono) return false;
  if (!scope.identity.empty() && scope.identity != ctx.identity.value)
    return false;  // the coupling law: identity A ≠ identity B
  if (!scope.site.empty() && scope.site != ctx.origin.registrable_domain)
    return false;
  if (!scope.workspace.empty() && scope.workspace != ctx.workspace)
    return false;
  if (!scope.rule_id.empty() && scope.rule_id != hit.rule_id) return false;
  if (!scope.list_id.empty() && scope.list_id != hit.list_id) return false;
  return true;
}

SweepResult SweepAsOf(const ScopeSet& scopes, long long now_mono) {
  SweepResult r;
  for (const auto& s : scopes.scopes) {
    if (s.expiry_mono >= 0 && now_mono >= s.expiry_mono)
      r.expired_ids.push_back(s.scope_id);
    else
      r.active_ids.push_back(s.scope_id);
  }
  return r;
}

JsonValue ScopeToJson(const ExceptionScope& s) {
  JsonValue::Object o{
      {"expiry_mono", JsonValue(static_cast<int>(s.expiry_mono))},
      {"identity", JsonValue(s.identity)},
      {"list_id", JsonValue(s.list_id)},
      {"reason", JsonValue(s.reason)},
      {"rule_id", JsonValue(s.rule_id)},
      {"scope_id", JsonValue(s.scope_id)},
      {"site", JsonValue(s.site)},
      {"workspace", JsonValue(s.workspace)},
  };
  return JsonValue(o);
}

JsonValue ScopeSetToJson(const ScopeSet& set) {
  JsonValue::Array a;
  for (const auto& s : set.scopes) a.push_back(ScopeToJson(s));
  return JsonValue(JsonValue::Object{{"scopes", JsonValue(a)}});
}

}  // namespace xr::shield
