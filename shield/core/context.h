// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/context — the request context the decision core keys
// on (P11-T2): identity, site (origin), request class, first/third-party,
// tab type, workspace. The frozen mojom types (IdentityId, OriginKey,
// RequestClass — xr_types.mojom) are mirrored EXACTLY; tab_type and
// workspace have no frozen enum, so they are living shield vocabulary
// (shield/host_protocol.md), deny-on-unknown like everything else here.
//
// Law: strict parsing. Unknown field, unknown enum name, non-http(s)
// scheme, empty identity/origin => typed refusal. No guessing, no defaults
// that invent trust: absent OPTIONAL keys take the DOCUMENTED default and
// nothing else.
#pragma once

#include <string>
#include <string_view>

#include "common/core/json.h"

namespace xr::shield {

// Frozen (xr_types.mojom RequestClass) — names and order are the contract.
enum class RequestClass {
  kNavigation = 0,
  kSubresource = 1,
  kScript = 2,
  kPermission = 3,
  kStorage = 4,
  kNetwork = 5,
};

// Living shield vocabulary (documented in shield/host_protocol.md).
enum class TabType {
  kNormal = 0,
  kIncognito = 1,
  kWorkspace = 2,
};

struct IdentityId {
  std::string value;  // canonical "xr:<uuidv4>"; validated at the boundary
};

struct OriginKey {
  std::string scheme;              // "http" | "https" only (frozen fake law)
  std::string registrable_domain;  // eTLD+1, lowercase
};

// Minimal http(s) URL split — enough for matching, deliberately not a
// general URL parser (the engine never needs more; anything it cannot
// split cleanly is refused, never guessed at).
struct UrlParts {
  std::string scheme;  // lowercase, "http"|"https"
  std::string host;    // lowercase, no port, no userinfo
  std::string path;    // starts with '/' ("" -> "/")
  bool has_query = false;
  bool has_fragment = false;
};

struct RequestContext {
  IdentityId identity;
  OriginKey origin;
  std::string url;  // the raw target URL as received
  UrlParts parts;
  RequestClass request_class = RequestClass::kSubresource;
  bool first_party = false;
  TabType tab_type = TabType::kNormal;      // DOCUMENTED default
  std::string workspace;                    // "" = the default workspace
};

enum class ParseResult {
  kOk = 0,
  kMalformed,       // shape/type violation
  kUnknownField,    // strict: unknown key => refuse
  kUnknownValue,    // unknown enum name / bad scheme / empty required field
};

// Parse {identity, origin, url, request_class, first_party?, tab_type?,
// workspace?} — `first_party`, `tab_type`, `workspace` are OPTIONAL with the
// documented defaults above; every other key is REQUIRED; unknown keys are
// refused. `detail` receives the machine-readable refusal reason.
ParseResult ParseRequestContext(const common::JsonValue& args,
                                RequestContext* out, std::string* detail);

bool SplitUrl(std::string_view url, UrlParts* out);

const char* RequestClassName(RequestClass c);
bool RequestClassFromName(std::string_view name, RequestClass* out);
const char* TabTypeName(TabType t);
bool TabTypeFromName(std::string_view name, TabType* out);

// Canonical echo of a parsed context (byte-parity across backends: the
// golden vectors pin this shape).
common::JsonValue ContextToJson(const RequestContext& ctx);

}  // namespace xr::shield
