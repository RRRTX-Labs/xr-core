// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/context — strict parse of the request context (see
// context.h). Every refusal here is typed and machine-readable: the host
// turns ParseResult into its canonical typed-error/reject lines, and the
// Python fake mirrors this file's decisions byte-for-byte (golden vectors).
#include "shield/core/context.h"

#include <algorithm>

namespace xr::shield {

using common::JsonValue;

namespace {

bool IsHttpScheme(std::string_view s) { return s == "http" || s == "https"; }

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

// One required string field.
bool NeedString(const JsonValue& obj, const char* key, std::string* out,
                ParseResult* rc, std::string* detail) {
  const JsonValue* v = obj.find(key);
  if (v == nullptr) {
    *rc = ParseResult::kMalformed;
    *detail = std::string("missing-required-field:") + key;
    return false;
  }
  if (!v->is_string()) {
    *rc = ParseResult::kMalformed;
    *detail = std::string("field-not-string:") + key;
    return false;
  }
  *out = v->as_string();
  return true;
}

}  // namespace

bool SplitUrl(std::string_view url, UrlParts* out) {
  *out = UrlParts{};
  size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos || scheme_end == 0) return false;
  std::string scheme = Lower(std::string(url.substr(0, scheme_end)));
  if (!IsHttpScheme(scheme)) return false;
  std::string_view rest = url.substr(scheme_end + 3);
  if (rest.empty()) return false;
  // userinfo is refused, not parsed (credentials in a blocked-URL context
  // are a leak vector; the engine never needs them).
  if (rest.find('@') != std::string_view::npos) return false;
  // the authority ends at the FIRST of '/' '?' '#' — a query/fragment may
  // legally start without a path ("https://e.com?a=1"), and its bytes must
  // never leak into the host
  size_t path_start = rest.find('/');
  size_t host_end = rest.find_first_of("/?#");
  if (host_end == std::string_view::npos) host_end = rest.size();
  std::string_view host_port = rest.substr(0, host_end);
  if (host_port.empty()) return false;
  std::string host = Lower(std::string(host_port));
  size_t colon = host.find(':');
  if (colon != std::string::npos) {
    std::string_view port = host_port.substr(colon + 1);
    if (port.empty() ||
        port.find_first_not_of("0123456789") != std::string_view::npos)
      return false;
    host = host.substr(0, colon);
  }
  if (host.empty()) return false;
  // the path starts at the first '/' — but only when that '/' precedes any
  // '?'/'#' (otherwise there is no path and the canonical form is "/")
  std::string_view tail =
      (path_start == std::string_view::npos || path_start > host_end)
          ? std::string_view("/")
          : rest.substr(path_start);
  std::string path(tail);
  bool has_query = false, has_fragment = false;
  size_t cut = path.size();
  size_t q = path.find('?');
  size_t f = path.find('#');
  has_query = q != std::string::npos;
  has_fragment = f != std::string::npos;
  if (has_query) cut = std::min(cut, q);
  if (has_fragment) cut = std::min(cut, f);
  path = path.substr(0, cut);
  if (path.empty() || path[0] != '/') path = "/" + path;
  out->scheme = scheme;
  out->host = host;
  out->path = path;
  out->has_query = has_query;
  out->has_fragment = has_fragment;
  return true;
}

const char* RequestClassName(RequestClass c) {
  switch (c) {
    case RequestClass::kNavigation: return "kNavigation";
    case RequestClass::kSubresource: return "kSubresource";
    case RequestClass::kScript: return "kScript";
    case RequestClass::kPermission: return "kPermission";
    case RequestClass::kStorage: return "kStorage";
    case RequestClass::kNetwork: return "kNetwork";
  }
  return "kSubresource";  // unreachable; keeps -Wreturn-type honest
}

bool RequestClassFromName(std::string_view name, RequestClass* out) {
  if (name == "kNavigation") { *out = RequestClass::kNavigation; return true; }
  if (name == "kSubresource") { *out = RequestClass::kSubresource; return true; }
  if (name == "kScript") { *out = RequestClass::kScript; return true; }
  if (name == "kPermission") { *out = RequestClass::kPermission; return true; }
  if (name == "kStorage") { *out = RequestClass::kStorage; return true; }
  if (name == "kNetwork") { *out = RequestClass::kNetwork; return true; }
  return false;
}

const char* TabTypeName(TabType t) {
  switch (t) {
    case TabType::kNormal: return "normal";
    case TabType::kIncognito: return "incognito";
    case TabType::kWorkspace: return "workspace";
  }
  return "normal";
}

bool TabTypeFromName(std::string_view name, TabType* out) {
  if (name == "normal") { *out = TabType::kNormal; return true; }
  if (name == "incognito") { *out = TabType::kIncognito; return true; }
  if (name == "workspace") { *out = TabType::kWorkspace; return true; }
  return false;
}

ParseResult ParseRequestContext(const JsonValue& args, RequestContext* out,
                                std::string* detail) {
  *out = RequestContext{};
  if (!args.is_object()) {
    *detail = "context-not-object";
    return ParseResult::kMalformed;
  }
  // strict key set: refuse anything unknown (deny-on-unknown, house law)
  static const char* kAllowed[] = {"identity", "origin",     "url",
                                   "request_class", "first_party",
                                   "tab_type",    "workspace"};
  for (const auto& kv : args.as_object()) {
    bool known = false;
    for (const char* a : kAllowed) {
      if (kv.first == a) { known = true; break; }
    }
    if (!known) {
      *detail = "unknown-field:" + kv.first;
      return ParseResult::kUnknownField;
    }
  }
  ParseResult rc = ParseResult::kOk;
  // identity: {"value": "xr:..."} — the frozen struct shape
  const JsonValue* idv = args.find("identity");
  if (idv == nullptr || !idv->is_object()) {
    *detail = "identity-not-object";
    return ParseResult::kMalformed;
  }
  for (const auto& kv : idv->as_object()) {
    if (kv.first != "value") {
      *detail = "unknown-field:identity." + kv.first;
      return ParseResult::kUnknownField;
    }
  }
  if (!NeedString(*idv, "value", &out->identity.value, &rc, detail)) return rc;
  if (out->identity.value.empty()) {
    *detail = "empty-identity";
    return ParseResult::kUnknownValue;
  }
  // origin: {scheme, registrable_domain}
  const JsonValue* ov = args.find("origin");
  if (ov == nullptr || !ov->is_object()) {
    *detail = "origin-not-object";
    return ParseResult::kMalformed;
  }
  for (const auto& kv : ov->as_object()) {
    if (kv.first != "scheme" && kv.first != "registrable_domain") {
      *detail = "unknown-field:origin." + kv.first;
      return ParseResult::kUnknownField;
    }
  }
  if (!NeedString(*ov, "scheme", &out->origin.scheme, &rc, detail)) return rc;
  if (!NeedString(*ov, "registrable_domain", &out->origin.registrable_domain,
                  &rc, detail))
    return rc;
  if (!IsHttpScheme(out->origin.scheme)) {
    *detail = "unknown-origin-scheme";
    return ParseResult::kUnknownValue;  // frozen fake law: kUnknownOrigin
  }
  if (out->origin.registrable_domain.empty()) {
    *detail = "empty-registrable-domain";
    return ParseResult::kUnknownValue;
  }
  // url (required string, must split)
  if (!NeedString(args, "url", &out->url, &rc, detail)) return rc;
  if (!SplitUrl(out->url, &out->parts)) {
    *detail = "unparsable-url";
    return ParseResult::kUnknownValue;
  }
  // request_class (required, frozen enum names)
  std::string rc_name;
  if (!NeedString(args, "request_class", &rc_name, &rc, detail)) return rc;
  if (!RequestClassFromName(rc_name, &out->request_class)) {
    *detail = "unknown-request-class";
    return ParseResult::kUnknownValue;
  }
  // first_party (optional bool, default false)
  if (const JsonValue* fp = args.find("first_party")) {
    if (!fp->is_bool()) {
      *detail = "field-not-bool:first_party";
      return ParseResult::kMalformed;
    }
    out->first_party = fp->as_bool();
  }
  // tab_type (optional living enum, default normal)
  if (const JsonValue* tt = args.find("tab_type")) {
    if (!tt->is_string() || !TabTypeFromName(tt->as_string(), &out->tab_type)) {
      *detail = "unknown-tab-type";
      return ParseResult::kUnknownValue;
    }
  }
  // workspace (optional string, default "")
  if (const JsonValue* ws = args.find("workspace")) {
    if (!ws->is_string()) {
      *detail = "field-not-string:workspace";
      return ParseResult::kMalformed;
    }
    out->workspace = ws->as_string();
  }
  return ParseResult::kOk;
}

JsonValue ContextToJson(const RequestContext& ctx) {
  return JsonValue(JsonValue::Object{
      {"first_party", JsonValue(ctx.first_party)},
      {"identity", JsonValue(JsonValue::Object{
                       {"value", JsonValue(ctx.identity.value)}})},
      {"origin",
       JsonValue(JsonValue::Object{
           {"registrable_domain", JsonValue(ctx.origin.registrable_domain)},
           {"scheme", JsonValue(ctx.origin.scheme)}})},
      {"request_class", JsonValue(std::string(RequestClassName(
                            ctx.request_class)))},
      {"tab_type", JsonValue(std::string(TabTypeName(ctx.tab_type)))},
      {"url", JsonValue(ctx.url)},
      {"workspace", JsonValue(ctx.workspace)},
  });
}

}  // namespace xr::shield
