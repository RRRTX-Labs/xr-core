// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — request parsing into ResolveRequest (see resolve.h):
// the frozen fake's validation order (version gate first, then identity /
// origin / class / trust), P6 layer entry validation (invalid entries are
// dropped and REPORTED, never widened), and the default fixture identity
// set that keeps vector parity on bare requests.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <type_traits>

#include "policy/core/resolve.h"

namespace xr::policy {



namespace {

bool GetStringField(const JsonValue& v, const char* key, std::string* out) {
  const JsonValue* f = v.find(key);
  if (f == nullptr || !f->is_string()) return false;
  *out = f->as_string();
  return true;
}

bool ValidScheme(const std::string& s) { return s == "http" || s == "https"; }
bool ValidRequestClass(const std::string& s) {
  return s == "kNavigation" || s == "kSubresource" || s == "kScript" ||
         s == "kPermission" || s == "kStorage" || s == "kNetwork";
}

// The frozen fake's fixture identities (parity default when a request carries
// no explicit identity layer; production supplies identity-list-v1).
std::vector<IdentityInfo> DefaultIdentities() {
  return {
      {"xr:00000000-0000-4000-8000-000000000001", false, false},   // standard
      {"xr:00000000-0000-4000-8000-000000000002", false, true},    // fortress
      {"xr:00000000-0000-4000-8000-0000000000ef", true, false},    // ephemeral
  };
}

bool ParseIdentityInfo(const JsonValue& v, IdentityInfo* out) {
  if (!v.is_object()) return false;
  if (!GetStringField(v, "value", &out->value) || out->value.empty()) return false;
  const JsonValue* storage = v.find("storage");
  if (storage != nullptr) {
    if (!storage->is_string()) return false;
    out->ephemeral = storage->as_string() == "in_memory";
  }
  const JsonValue* kind = v.find("kind");
  if (kind != nullptr) {
    if (!kind->is_string()) return false;
    out->fortress = kind->as_string() == "fortress";
  }
  return true;
}

bool ParseBinding(const JsonValue& v, TrustBinding* out) {
  if (!v.is_object()) return false;
  if (!GetStringField(v, "domain", &out->domain) || out->domain.empty()) return false;
  const JsonValue* id = v.find("identity");
  if (id != nullptr && !id->is_string()) return false;
  if (id != nullptr) out->identity = id->as_string();
  if (!GetStringField(v, "trust", &out->trust)) return false;
  if (TrustTierIndex(out->trust) < 0) return false;
  const JsonValue* ca = v.find("created_at");
  if (ca != nullptr && !ca->is_int()) return false;
  if (ca != nullptr) out->created_at = ca->as_int();
  return true;
}

bool ParseException(const JsonValue& v, ExceptionEntry* out) {
  if (!v.is_object()) return false;
  if (!GetStringField(v, "id", &out->id) || out->id.empty()) return false;
  if (!GetStringField(v, "domain", &out->domain) || out->domain.empty()) return false;
  const JsonValue* id = v.find("identity");
  if (id != nullptr && !id->is_string()) return false;
  if (id != nullptr) out->identity = id->as_string();
  if (!GetStringField(v, "scope", &out->scope)) return false;
  if (out->scope != "once" && out->scope != "session" && out->scope != "7d" &&
      out->scope != "permanent") {
    return false;
  }
  if (!GetStringField(v, "trust", &out->trust)) return false;
  if (TrustTierIndex(out->trust) < 0) return false;
  auto int_field = [&](const char* k, int64_t* dst) {
    const JsonValue* f = v.find(k);
    if (f == nullptr) return true;  // absent ok (scope rules enforce later)
    if (!f->is_int()) return false;
    *dst = f->as_int();
    return true;
  };
  if (!int_field("created_at", &out->created_at)) return false;
  if (!int_field("expires_at", &out->expires_at)) return false;
  if (!int_field("remaining_uses", &out->remaining_uses)) return false;
  const JsonValue* sid = v.find("session_id");
  if (sid != nullptr && !sid->is_string()) return false;
  if (sid != nullptr) out->session_id = sid->as_string();
  const JsonValue* gb = v.find("granted_by");
  if (gb != nullptr && !gb->is_string()) return false;
  if (gb != nullptr) out->granted_by = gb->as_string();
  // Scope-required fields: 7d must carry a future-looking expiry; session
  // must carry a session id; once must carry remaining_uses. Missing =>
  // structurally invalid => dropped (never widened).
  if (out->scope == "7d" && !v.find("expires_at")) return false;
  if (out->scope == "session" && !v.find("session_id")) return false;
  if (out->scope == "once" && !v.find("remaining_uses")) return false;
  return true;
}

}  // namespace

bool ParseEnterpriseFields(const JsonValue& v, EnterprisePolicy* out) {
  if (!v.is_object()) return false;
  out->present = true;
  const JsonValue* f = v.find("trust_floor");
  if (f != nullptr) {
    if (!f->is_string()) return false;
    out->trust_floor = f->as_string();
    if (!out->trust_floor.empty() && TrustTierIndex(out->trust_floor) < 0) return false;
  }
  if (!GetStringField(v, "force_fingerprint", &out->force_fingerprint) &&
      v.find("force_fingerprint") != nullptr) {
    return false;
  }
  if (!out->force_fingerprint.empty() && !FingerprintModeFromString(out->force_fingerprint)) {
    return false;
  }
  if (!GetStringField(v, "force_route", &out->force_route) &&
      v.find("force_route") != nullptr) {
    return false;
  }
  if (!out->force_route.empty() && !RouteClassFromString(out->force_route)) return false;
  auto bool_field = [&](const char* k, bool* dst) {
    const JsonValue* b = v.find(k);
    if (b == nullptr) return true;
    if (!b->is_bool()) return false;
    *dst = b->as_bool();
    return true;
  };
  if (!bool_field("force_letterbox", &out->force_letterbox)) return false;
  if (!bool_field("force_block_third_party", &out->force_block_third_party)) return false;
  return true;
}

namespace {

bool ParseExtension(const JsonValue& v, ExtensionHint* out) {
  if (!v.is_object()) return false;
  out->present = true;
  if (!GetStringField(v, "id", &out->id) || out->id.empty()) return false;
  const JsonValue* caps = v.find("capabilities");
  if (caps != nullptr) {
    if (!caps->is_array()) return false;
    for (const auto& c : caps->as_array()) {
      if (!c.is_string()) return false;
      out->capabilities.push_back(c.as_string());
    }
  }
  return true;
}

}  // namespace

RequestParse ParseResolveRequest(const JsonValue& v) {
  RequestParse rp;
  ResolveRequest& req = rp.request;
  if (!v.is_object()) {
    req.has_request = false;
    return rp;
  }
  req.has_request = true;

  // Version gate FIRST (fake order: mismatch beats malformed identity).
  const JsonValue* cv = v.find("contract_version");
  if (cv != nullptr) {
    if (!cv->is_int() || cv->as_int() != kContractVersion) {
      req.version_mismatch = true;
      return rp;
    }
  }

  // P6 identity layer; when the request carries none, default to the frozen
  // fake's fixture set (parity requirement: vectors drive bare requests).
  if (const JsonValue* ids = v.find("identities"); ids != nullptr) {
    if (ids->is_array()) {
      for (const auto& e : ids->as_array()) {
        IdentityInfo info;
        if (ParseIdentityInfo(e, &info)) req.identities.push_back(std::move(info));
        else rp.dropped.push_back("identities: invalid entry dropped");
      }
    } else {
      rp.dropped.push_back("identities: not an array; layer ignored");
    }
  }
  if (req.identities.empty()) {
    req.identities = DefaultIdentities();
  }

  // Identity: must be an object with a known `value`.
  const JsonValue* identity = v.find("identity");
  std::string identity_value;
  bool identity_ok = false;
  if (identity != nullptr && identity->is_object()) {
    if (GetStringField(*identity, "value", &identity_value)) {
      for (const auto& info : req.identities) {
        if (info.value == identity_value) { identity_ok = true; break; }
      }
    }
  }

  if (const JsonValue* bs = v.find("bindings"); bs != nullptr) {
    if (bs->is_array()) {
      for (const auto& e : bs->as_array()) {
        TrustBinding b;
        if (ParseBinding(e, &b)) req.bindings.push_back(std::move(b));
        else rp.dropped.push_back("bindings: invalid entry dropped");
      }
    } else {
      rp.dropped.push_back("bindings: not an array; layer ignored");
    }
  }
  if (const JsonValue* es = v.find("exceptions"); es != nullptr) {
    if (es->is_array()) {
      for (const auto& e : es->as_array()) {
        ExceptionEntry x;
        if (ParseException(e, &x)) req.exceptions.push_back(std::move(x));
        else rp.dropped.push_back("exceptions: invalid entry dropped");
      }
    } else {
      rp.dropped.push_back("exceptions: not an array; layer ignored");
    }
  }
  if (const JsonValue* ent = v.find("enterprise"); ent != nullptr && ent->is_object()) {
    EnterprisePolicy e;
    if (ParseEnterpriseFields(*ent, &e)) req.enterprise = e;
    else rp.dropped.push_back("enterprise: invalid layer ignored");
  } else if (v.find("enterprise") != nullptr) {
    rp.dropped.push_back("enterprise: not an object; layer ignored");
  }
  if (const JsonValue* ext = v.find("extension"); ext != nullptr && ext->is_object()) {
    ExtensionHint e;
    if (ParseExtension(*ext, &e)) req.extension = e;
    else rp.dropped.push_back("extension: invalid hint ignored");
  } else if (v.find("extension") != nullptr) {
    rp.dropped.push_back("extension: not an object; hint ignored");
  }
  if (const JsonValue* now = v.find("now_ms"); now != nullptr) {
    if (now->is_int()) req.now_ms = now->as_int();
    else rp.dropped.push_back("now_ms: not an integer; treated as 0");
  }
  if (const JsonValue* sid = v.find("session_id"); sid != nullptr) {
    if (sid->is_string()) req.session_id = sid->as_string();
    else rp.dropped.push_back("session_id: not a string; treated as empty");
  }

  // Origin / class / trust (fake order).
  const JsonValue* origin = v.find("origin");
  bool origin_ok = false;
  std::string domain;
  if (origin != nullptr && origin->is_object()) {
    std::string scheme;
    bool scheme_ok = GetStringField(*origin, "scheme", &scheme) && ValidScheme(scheme);
    bool domain_ok = GetStringField(*origin, "registrable_domain", &domain) && !domain.empty();
    origin_ok = scheme_ok && domain_ok;
  }
  std::string request_class;
  bool class_ok = GetStringField(v, "request_class", &request_class) &&
                  ValidRequestClass(request_class);
  // TrustContext? nullability: explicit JSON null == absent (mojom optional
  // semantics; the fake's request.get() treats them identically => Standard).
  const JsonValue* trust = v.find("trust_context");
  bool has_trust = trust != nullptr && !trust->is_null();
  bool trust_ok = true;
  std::string trust_value;
  if (has_trust) {
    trust_ok = trust->is_string() && TrustTierIndex(trust->as_string()) >= 0;
    if (trust_ok) trust_value = trust->as_string();
  }

  req.identity = identity_value;
  req.registrable_domain = domain;
  req.request_class = request_class;
  req.has_trust = has_trust;
  req.trust = trust_value;
  req.core_valid = identity_ok && origin_ok && class_ok && trust_ok;
  return rp;
}

}  // namespace xr::policy
