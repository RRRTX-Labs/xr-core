// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/bundle — strict parse + manifest binding (bundle.h).
// Every refusal token in this file is closed vocabulary: the Python fake
// mirrors it byte-for-byte and the golden vectors pin both.
#include "shield/core/bundle.h"

#include "common/core/sha256.h"

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

bool NeedStr(const JsonValue& obj, const char* key, std::string* out,
             std::string* detail) {
  const JsonValue* v = obj.find(key);
  if (v == nullptr || !v->is_string()) {
    *detail = std::string("missing-or-not-string:") + key;
    return false;
  }
  *out = v->as_string();
  return true;
}

bool ActionFromName(const std::string& s, Action* a) {
  if (s == "block") { *a = Action::kBlock; return true; }
  if (s == "allow") { *a = Action::kAllow; return true; }
  if (s == "redirect") { *a = Action::kRedirect; return true; }
  if (s == "replace") { *a = Action::kReplace; return true; }
  return false;
}

}  // namespace

bool ParseFilter(const std::string& filter, ParsedFilter* out,
                 std::string* detail) {
  *out = ParsedFilter{};
  if (filter.empty()) {
    *detail = "empty-filter";
    return false;
  }
  // refusals (closed vocabulary — the compiler's refusal table owns the
  // TEXT syntax; anything reaching the engine outside the v1 grammar is a
  // compile leak and is refused, never guessed at):
  if (filter[0] == '!') {
    *detail = "unsupported-directive:comment";
    return false;
  }
  if (filter.size() >= 2 && filter.front() == '/' && filter.back() == '/') {
    *detail = "unsupported-directive:regex";
    return false;
  }
  if (filter.find('$') != std::string::npos) {
    *detail = "unsupported-directive:options-in-filter";
    return false;
  }
  if (filter.find('#') != std::string::npos) {
    *detail = "unsupported-directive:cosmetic-hash";
    return false;
  }
  if (filter.find('@') != std::string::npos) {
    *detail = "unsupported-directive:at-syntax";
    return false;
  }
  std::string body = filter;
  if (body.rfind("||", 0) == 0) {
    out->domain_anchor = true;
    body = body.substr(2);
  } else if (!body.empty() && body[0] == '|') {
    out->left_anchor = true;
    body = body.substr(1);
  }
  if (!body.empty() && body.back() == '|') {
    out->right_anchor = true;
    body.pop_back();
  }
  if (body.find('|') != std::string::npos) {
    *detail = "unsupported-directive:interior-pipe";
    return false;
  }
  if (body.empty()) {
    *detail = "empty-filter";
    return false;
  }
  // split on '*' (interior empties collapse; edge empties are the anchors'
  // wildcards); '^' becomes the separator sentinel inside segments
  std::string cur;
  for (char c : body) {
    if (c == '*') {
      // push the pending literal; an EMPTY cur here means consecutive
      // wildcards — collapsed, except at the very front where the leading
      // empty segment marks the unanchored start
      if (!cur.empty() || out->segments.empty()) {
        out->segments.push_back(cur);
        cur.clear();
      }
      continue;
    }
    cur.push_back(c == '^' ? kSep : c);
  }
  out->segments.push_back(cur);
  // a middle empty segment would make the match ambiguous ("a**b" is
  // already collapsed above); an empty FIRST/LAST segment is a plain edge
  // wildcard and stays
  for (size_t i = 1; i + 1 < out->segments.size(); ++i) {
    if (out->segments[i].empty()) {
      *detail = "unsupported-directive:empty-segment";
      return false;
    }
  }
  return true;
}

BundleResult ParseBundle(const JsonValue& raw, NormalizedBundle* out,
                         std::string* detail) {
  *out = NormalizedBundle{};
  if (!raw.is_object()) {
    *detail = "bundle-not-object";
    return BundleResult::kMalformed;
  }
  static const char* kTop[] = {"schema", "schema_version", "name",
                               "bundle_version", "lists", "refusals"};
  if (!KnownKey(raw, kTop, 6, detail)) return BundleResult::kUnknownField;
  const JsonValue* schema = raw.find("schema");
  if (schema == nullptr || !schema->is_string() ||
      schema->as_string() != "xr-list-bundle") {
    *detail = "wrong-schema";
    return BundleResult::kMalformed;
  }
  const JsonValue* sv = raw.find("schema_version");
  if (sv == nullptr || !sv->is_int() || sv->as_int() != 1) {
    *detail = "wrong-schema-version";
    return BundleResult::kMalformed;
  }
  if (!NeedStr(raw, "name", &out->name, detail) || out->name.empty()) {
    if (out->name.empty()) *detail = "empty-name";
    return BundleResult::kMalformed;
  }
  const JsonValue* bv = raw.find("bundle_version");
  if (bv == nullptr || !bv->is_int() || bv->as_int() <= 0) {
    *detail = "non-positive-bundle-version";
    return BundleResult::kMalformed;
  }
  out->bundle_version = bv->as_int();
  const JsonValue* lists = raw.find("lists");
  if (lists == nullptr || !lists->is_array()) {
    *detail = "lists-not-array";
    return BundleResult::kMalformed;
  }
  static const char* kList[] = {"name", "attribution", "rules"};
  static const char* kRule[] = {"id", "kind", "filter", "action", "resource",
                                "domains", "exclude_domains"};
  for (const JsonValue& lv : lists->as_array()) {
    if (!lv.is_object() || !KnownKey(lv, kList, 3, detail))
      return lv.is_object() ? BundleResult::kUnknownField
                            : BundleResult::kMalformed;
    BundleList bl;
    if (!NeedStr(lv, "name", &bl.name, detail) || bl.name.empty()) {
      if (bl.name.empty()) *detail = "empty-list-name";
      return BundleResult::kMalformed;
    }
    if (!NeedStr(lv, "attribution", &bl.attribution, detail))
      return BundleResult::kMalformed;
    const JsonValue* rules = lv.find("rules");
    if (rules == nullptr || !rules->is_array()) {
      *detail = "rules-not-array";
      return BundleResult::kMalformed;
    }
    for (const JsonValue& rv : rules->as_array()) {
      if (!rv.is_object() || !KnownKey(rv, kRule, 7, detail))
        return rv.is_object() ? BundleResult::kUnknownField
                              : BundleResult::kMalformed;
      Rule r;
      if (!NeedStr(rv, "id", &r.id, detail) || r.id.empty()) {
        if (r.id.empty()) *detail = "empty-rule-id";
        return BundleResult::kMalformed;
      }
      if (!NeedStr(rv, "kind", &r.kind, detail)) return BundleResult::kMalformed;
      if (r.kind != "network" && r.kind != "cosmetic" &&
          r.kind != "redirect" && r.kind != "resource") {
        *detail = "unknown-rule-kind";
        return BundleResult::kUnknownField;
      }
      if (!NeedStr(rv, "filter", &r.filter, detail))
        return BundleResult::kMalformed;
      if (!ParseFilter(r.filter, &r.parsed, detail))
        return BundleResult::kUnsupportedDirective;
      std::string action;
      if (!NeedStr(rv, "action", &action, detail))
        return BundleResult::kMalformed;
      if (!ActionFromName(action, &r.action)) {
        *detail = "unknown-action";
        return BundleResult::kUnknownField;
      }
      const JsonValue* res = rv.find("resource");
      if (res != nullptr) {
        if (!res->is_string() || res->as_string().empty()) {
          *detail = "bad-resource";
          return BundleResult::kMalformed;
        }
        r.resource = res->as_string();
      }
      if ((r.action == Action::kRedirect || r.action == Action::kReplace) &&
          r.resource.empty()) {
        *detail = "redirect-without-resource";
        return BundleResult::kMalformed;
      }
      if (r.action != Action::kRedirect && r.action != Action::kReplace &&
          !r.resource.empty()) {
        *detail = "resource-without-redirect";
        return BundleResult::kMalformed;
      }
      for (const char* key : {"domains", "exclude_domains"}) {
        const JsonValue* dv = rv.find(key);
        if (dv == nullptr) continue;
        if (!dv->is_array()) {
          *detail = std::string("domains-not-array:") + key;
          return BundleResult::kMalformed;
        }
        std::vector<std::string>* target =
            key[0] == 'd' ? &r.domains : &r.exclude_domains;
        for (const JsonValue& d : dv->as_array()) {
          if (!d.is_string() || d.as_string().empty()) {
            *detail = std::string("bad-domain-entry:") + key;
            return BundleResult::kMalformed;
          }
          target->push_back(d.as_string());
        }
      }
      for (const auto& prev : bl.rules) {
        if (prev.id == r.id) {
          *detail = "duplicate-rule-id:" + r.id;
          return BundleResult::kMalformed;
        }
      }
      bl.rules.push_back(std::move(r));
    }
    out->lists.push_back(std::move(bl));
  }
  const JsonValue* refs = raw.find("refusals");
  if (refs != nullptr) {
    if (!refs->is_array()) {
      *detail = "refusals-not-array";
      return BundleResult::kMalformed;
    }
    static const char* kRef[] = {"directive", "reason", "count"};
    for (const JsonValue& fv : refs->as_array()) {
      if (!fv.is_object() || !KnownKey(fv, kRef, 3, detail))
        return fv.is_object() ? BundleResult::kUnknownField
                              : BundleResult::kMalformed;
      BundleRefusal br;
      if (!NeedStr(fv, "directive", &br.directive, detail) ||
          !NeedStr(fv, "reason", &br.reason, detail))
        return BundleResult::kMalformed;
      const JsonValue* c = fv.find("count");
      if (c == nullptr || !c->is_int() || c->as_int() < 0) {
        *detail = "bad-refusal-count";
        return BundleResult::kMalformed;
      }
      br.count = c->as_int();
      out->refusals.push_back(std::move(br));
    }
  }
  // digest over the canonical per-list bytes (the manifest binding target).
  // Canonical array bytes == "[" + ",".join(canonical list objects) + "]"
  // under the shared serializer — computed identically by the Python fake.
  std::string joined = "[";
  for (size_t i = 0; i < out->lists.size(); ++i) {
    if (i) joined += ",";
    joined += ListCanonicalBytes(out->lists[i]);
  }
  joined += "]";
  out->digest = common::Sha256Hex(joined);
  return BundleResult::kOk;
}

std::string ListCanonicalBytes(const BundleList& list) {
  JsonValue::Array rules;
  for (const Rule& r : list.rules) {
    JsonValue::Object ro{
        {"action", JsonValue(std::string(
                       r.action == Action::kBlock ? "block"
                       : r.action == Action::kAllow ? "allow"
                       : r.action == Action::kRedirect ? "redirect"
                                                       : "replace"))},
        {"filter", JsonValue(r.filter)},
        {"id", JsonValue(r.id)},
        {"kind", JsonValue(r.kind)},
    };
    if (!r.resource.empty()) ro["resource"] = JsonValue(r.resource);
    if (!r.domains.empty()) {
      JsonValue::Array a;
      for (const auto& d : r.domains) a.push_back(JsonValue(d));
      ro["domains"] = JsonValue(a);
    }
    if (!r.exclude_domains.empty()) {
      JsonValue::Array a;
      for (const auto& d : r.exclude_domains) a.push_back(JsonValue(d));
      ro["exclude_domains"] = JsonValue(a);
    }
    rules.push_back(JsonValue(ro));
  }
  JsonValue o(JsonValue::Object{
      {"attribution", JsonValue(list.attribution)},
      {"name", JsonValue(list.name)},
      {"rules", JsonValue(rules)},
  });
  return o.Canonical();
}

BundleResult CheckAgainstManifest(const NormalizedBundle& bundle,
                                  const std::vector<ManifestListEntry>& man,
                                  std::string* detail) {
  if (man.size() != bundle.lists.size()) {
    *detail = "manifest-list-count";
    return BundleResult::kManifestMismatch;
  }
  for (size_t i = 0; i < man.size(); ++i) {
    const BundleList& bl = bundle.lists[i];
    if (man[i].name != bl.name) {
      *detail = "manifest-list-name:" + man[i].name;
      return BundleResult::kManifestMismatch;
    }
    if (man[i].rules != static_cast<long long>(bl.rules.size())) {
      *detail = "manifest-rule-count:" + bl.name;
      return BundleResult::kManifestMismatch;
    }
    std::string got = common::Sha256Hex(ListCanonicalBytes(bl));
    if (man[i].sha256 != got) {
      *detail = "manifest-sha256:" + bl.name;
      return BundleResult::kManifestMismatch;
    }
  }
  return BundleResult::kOk;
}

JsonValue BundleSummaryJson(const NormalizedBundle& bundle) {
  JsonValue::Array lists;
  for (const auto& l : bundle.lists) {
    lists.push_back(JsonValue(JsonValue::Object{
        {"name", JsonValue(l.name)},
        {"rules", JsonValue(static_cast<int>(l.rules.size()))},
    }));
  }
  JsonValue::Array refs;
  for (const auto& r : bundle.refusals) {
    refs.push_back(JsonValue(JsonValue::Object{
        {"count", JsonValue(static_cast<int>(r.count))},
        {"directive", JsonValue(r.directive)},
        {"reason", JsonValue(r.reason)},
    }));
  }
  return JsonValue(JsonValue::Object{
      {"bundle_version", JsonValue(static_cast<int>(bundle.bundle_version))},
      {"digest", JsonValue(bundle.digest)},
      {"lists", JsonValue(lists)},
      {"name", JsonValue(bundle.name)},
      {"refusals", JsonValue(refs)},
  });
}

}  // namespace xr::shield
