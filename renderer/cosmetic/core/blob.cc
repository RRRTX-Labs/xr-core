// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// See blob.h for the three strictness laws.

#include "renderer/cosmetic/core/blob.h"

#include "common/core/sha256.h"

#include <set>

namespace xr::cosmetic {
namespace {

using xr::common::JsonValue;
using xr::common::ParseJson;

// The declared key sets. Anything outside these is kUnknownField, which is the
// law that stops a producer from shipping undeclared behaviour.
const std::set<std::string>& BlobKeys() {
  static const std::set<std::string> s = {
      "schema", "schema_version", "blob_id", "generated_epoch", "scope",
      "rules", "refusals", "sha256"};
  return s;
}

const std::set<std::string>& ScopeKeys() {
  static const std::set<std::string> s = {"site", "identity_class"};
  return s;
}

const std::set<std::string>& RuleKeys() {
  static const std::set<std::string> s = {
      "id", "selector", "action", "style", "enabled", "exception_sites"};
  return s;
}

const std::set<std::string>& RefusalKeys() {
  static const std::set<std::string> s = {"rule_index", "reason"};
  return s;
}

bool HasOnlyKnownKeys(const JsonValue& obj, const std::set<std::string>& known) {
  for (const auto& [k, v] : obj.as_object()) {
    (void)v;
    if (!known.count(k)) return false;
  }
  return true;
}

JsonValue RuleToJson(const BlobRule& r) {
  JsonValue::Object o;
  o["action"] = JsonValue(r.action);
  if (!r.exception_sites.empty()) {
    JsonValue::Array a;
    for (const std::string& s : r.exception_sites) a.push_back(JsonValue(s));
    o["exception_sites"] = JsonValue(std::move(a));
  }
  o["enabled"] = JsonValue(r.enabled);
  o["id"] = JsonValue(r.id);
  o["selector"] = JsonValue(r.selector);
  if (!r.style.empty()) {
    JsonValue::Object st;
    for (const auto& [k, v] : r.style) st[k] = JsonValue(v);
    o["style"] = JsonValue(std::move(st));
  }
  return JsonValue(std::move(o));
}

}  // namespace

const char* BlobErrorName(BlobError e) {
  switch (e) {
    case BlobError::kOk: return "ok";
    case BlobError::kMalformed: return "malformed-blob";
    case BlobError::kSchemaMismatch: return "schema-mismatch";
    case BlobError::kUnknownField: return "unknown-field";
    case BlobError::kScopeMismatch: return "scope-mismatch";
    case BlobError::kSha256Mismatch: return "sha256-mismatch";
    case BlobError::kTooManyRules: return "too-many-rules";
    case BlobError::kUnknownRefusalReason: return "unknown-refusal-reason";
    case BlobError::kDuplicateRuleId: return "duplicate-rule-id";
    case BlobError::kRuleRejected: return "rule-rejected";
    case BlobError::kEmptyBlobId: return "empty-blob-id";
  }
  return "unknown";
}

std::string BlobDigestInput(const CosmeticBlob& blob) {
  // Everything except `sha256`, canonicalized. The field order does not matter
  // because the canonicalizer sorts keys, which is what makes the digest
  // reproducible across the C++ producer and the Python one.
  JsonValue::Object o;
  o["blob_id"] = JsonValue(blob.blob_id);
  o["generated_epoch"] = JsonValue(blob.generated_epoch);
  {
    JsonValue::Object sc;
    sc["identity_class"] = JsonValue(blob.scope.identity_class);
    sc["site"] = JsonValue(blob.scope.site);
    o["scope"] = JsonValue(std::move(sc));
  }
  {
    JsonValue::Array rules;
    for (const BlobRule& r : blob.rules) rules.push_back(RuleToJson(r));
    o["rules"] = JsonValue(std::move(rules));
  }
  {
    JsonValue::Array refs;
    for (const BlobRefusal& f : blob.refusals) {
      JsonValue::Object fr;
      fr["reason"] = JsonValue(f.reason);
      fr["rule_index"] = JsonValue(f.rule_index);
      refs.push_back(JsonValue(std::move(fr)));
    }
    o["refusals"] = JsonValue(std::move(refs));
  }
  o["schema"] = JsonValue(std::string(kCosmeticSchemaId));
  o["schema_version"] = JsonValue(kCosmeticSchemaVersion);
  return JsonValue(std::move(o)).Canonical();
}

std::string BlobDigest(const CosmeticBlob& blob) {
  // The single shared SHA-256 in //xr/common (ADR-0043). A key-set hash that
  // differed from the bundle's would be a stop condition, not an optimization.
  return xr::common::Sha256Hex(BlobDigestInput(blob));
}

std::string BlobToCanonicalJson(const CosmeticBlob& blob) {
  JsonValue::Object o;
  o["blob_id"] = JsonValue(blob.blob_id);
  o["generated_epoch"] = JsonValue(blob.generated_epoch);
  {
    JsonValue::Object sc;
    sc["identity_class"] = JsonValue(blob.scope.identity_class);
    sc["site"] = JsonValue(blob.scope.site);
    o["scope"] = JsonValue(std::move(sc));
  }
  {
    JsonValue::Array rules;
    for (const BlobRule& r : blob.rules) rules.push_back(RuleToJson(r));
    o["rules"] = JsonValue(std::move(rules));
  }
  {
    JsonValue::Array refs;
    for (const BlobRefusal& f : blob.refusals) {
      JsonValue::Object fr;
      fr["reason"] = JsonValue(f.reason);
      fr["rule_index"] = JsonValue(f.rule_index);
      refs.push_back(JsonValue(std::move(fr)));
    }
    o["refusals"] = JsonValue(std::move(refs));
  }
  o["schema"] = JsonValue(std::string(kCosmeticSchemaId));
  o["schema_version"] = JsonValue(kCosmeticSchemaVersion);
  o["sha256"] = JsonValue(blob.sha256);
  return JsonValue(std::move(o)).Canonical();
}

BlobError ParseBlob(const std::string& json, const BlobScope* frame_scope,
                    BlobResult* out) {
  out->blob = CosmeticBlob();
  out->key_set = KeySetResult();
  out->error = BlobError::kOk;
  out->reason.clear();
  out->failed_index = -1;
  out->valid = false;

  auto refuse = [out](BlobError e, const std::string& reason,
                      int64_t at = -1) {
    out->error = e;
    out->reason = reason;
    out->failed_index = at;
    return e;
  };

  auto parsed = ParseJson(json);
  if (!parsed.ok || !parsed.value.is_object()) {
    return refuse(BlobError::kMalformed, BlobErrorName(BlobError::kMalformed));
  }
  const JsonValue& root = parsed.value;
  if (!HasOnlyKnownKeys(root, BlobKeys())) {
    return refuse(BlobError::kUnknownField,
                  BlobErrorName(BlobError::kUnknownField));
  }

  // Schema id and version are CONST: a mismatch is refused, never migrated.
  const JsonValue* schema = root.find("schema");
  if (schema == nullptr || !schema->is_string() ||
      schema->as_string() != kCosmeticSchemaId) {
    return refuse(BlobError::kSchemaMismatch,
                  BlobErrorName(BlobError::kSchemaMismatch));
  }
  const JsonValue* version = root.find("schema_version");
  if (version == nullptr || !version->is_int() ||
      version->as_int() != kCosmeticSchemaVersion) {
    return refuse(BlobError::kSchemaMismatch,
                  BlobErrorName(BlobError::kSchemaMismatch));
  }

  const JsonValue* blob_id = root.find("blob_id");
  if (blob_id == nullptr || !blob_id->is_string() ||
      blob_id->as_string().empty()) {
    return refuse(BlobError::kEmptyBlobId,
                  BlobErrorName(BlobError::kEmptyBlobId));
  }
  out->blob.blob_id = blob_id->as_string();

  const JsonValue* epoch = root.find("generated_epoch");
  if (epoch == nullptr || !epoch->is_int() || epoch->as_int() < 0) {
    return refuse(BlobError::kMalformed,
                  BlobErrorName(BlobError::kMalformed));
  }
  out->blob.generated_epoch = epoch->as_int();

  const JsonValue* scope = root.find("scope");
  if (scope == nullptr || !scope->is_object() ||
      !HasOnlyKnownKeys(*scope, ScopeKeys())) {
    return refuse(BlobError::kMalformed,
                  BlobErrorName(BlobError::kMalformed));
  }
  const JsonValue* site = scope->find("site");
  const JsonValue* idc = scope->find("identity_class");
  if (site == nullptr || !site->is_string() || site->as_string().empty() ||
      idc == nullptr || !idc->is_string() ||
      (idc->as_string() != "anonymous" && idc->as_string() != "authenticated" &&
       idc->as_string() != "enterprise")) {
    return refuse(BlobError::kScopeMismatch,
                  BlobErrorName(BlobError::kScopeMismatch));
  }
  out->blob.scope.site = site->as_string();
  out->blob.scope.identity_class = idc->as_string();

  if (frame_scope != nullptr &&
      (frame_scope->site != out->blob.scope.site ||
       frame_scope->identity_class != out->blob.scope.identity_class)) {
    // A blob for one site must not apply in another. The embedder is not an
    // input to this comparison anywhere — see core/scope_key.h.
    return refuse(BlobError::kScopeMismatch,
                  BlobErrorName(BlobError::kScopeMismatch));
  }

  const JsonValue* rules = root.find("rules");
  if (rules == nullptr || !rules->is_array()) {
    return refuse(BlobError::kMalformed,
                  BlobErrorName(BlobError::kMalformed));
  }
  if (rules->as_array().size() > kMaxKeySetRules) {
    return refuse(BlobError::kTooManyRules,
                  BlobErrorName(BlobError::kTooManyRules));
  }

  const JsonValue* refusals = root.find("refusals");
  if (refusals != nullptr) {
    if (!refusals->is_array()) {
      return refuse(BlobError::kMalformed,
                    BlobErrorName(BlobError::kMalformed));
    }
    for (const JsonValue& entry : refusals->as_array()) {
      if (!entry.is_object() || !HasOnlyKnownKeys(entry, RefusalKeys())) {
        return refuse(BlobError::kMalformed,
                      BlobErrorName(BlobError::kMalformed));
      }
      const JsonValue* reason = entry.find("reason");
      if (reason == nullptr || !reason->is_string() ||
          !KnownRefusalReasons().count(reason->as_string())) {
        const JsonValue* idx = entry.find("rule_index");
        return refuse(BlobError::kUnknownRefusalReason,
                      BlobErrorName(BlobError::kUnknownRefusalReason),
                      (idx && idx->is_int()) ? idx->as_int() : -1);
      }
      BlobRefusal f;
      f.reason = reason->as_string();
      const JsonValue* idx = entry.find("rule_index");
      f.rule_index = (idx && idx->is_int()) ? idx->as_int() : -1;
      out->blob.refusals.push_back(std::move(f));
    }
  }

  const JsonValue* digest = root.find("sha256");
  if (digest == nullptr || !digest->is_string() ||
      digest->as_string().size() != 64) {
    return refuse(BlobError::kMalformed,
                  BlobErrorName(BlobError::kMalformed));
  }
  out->blob.sha256 = digest->as_string();

  // Parse the rules BEFORE checking the digest, because a malformed rule is a
  // more useful diagnosis than a digest mismatch on bytes we could not read.
  std::set<std::string> seen_ids;
  int64_t index = 0;
  for (const JsonValue& rj : rules->as_array()) {
    if (!rj.is_object() || !HasOnlyKnownKeys(rj, RuleKeys())) {
      return refuse(BlobError::kMalformed,
                    BlobErrorName(BlobError::kMalformed), index);
    }
    BlobRule r;
    const JsonValue* id = rj.find("id");
    const JsonValue* sel = rj.find("selector");
    const JsonValue* act = rj.find("action");
    if (id == nullptr || !id->is_string() || id->as_string().empty() ||
        sel == nullptr || !sel->is_string() || act == nullptr ||
        !act->is_string()) {
      return refuse(BlobError::kMalformed,
                    BlobErrorName(BlobError::kMalformed), index);
    }
    r.id = id->as_string();
    r.selector = sel->as_string();
    r.action = act->as_string();
    if (!seen_ids.insert(r.id).second) {
      return refuse(BlobError::kDuplicateRuleId,
                    BlobErrorName(BlobError::kDuplicateRuleId), index);
    }
    const JsonValue* st = rj.find("style");
    if (st != nullptr) {
      if (!st->is_object()) {
        return refuse(BlobError::kMalformed,
                      BlobErrorName(BlobError::kMalformed), index);
      }
      for (const auto& [k, v] : st->as_object()) {
        if (!v.is_string()) {
          return refuse(BlobError::kMalformed,
                        BlobErrorName(BlobError::kMalformed), index);
        }
        r.style.emplace_back(k, v.as_string());
      }
    }
    const JsonValue* ex = rj.find("exception_sites");
    if (ex != nullptr) {
      if (!ex->is_array()) {
        return refuse(BlobError::kMalformed,
                      BlobErrorName(BlobError::kMalformed), index);
      }
      for (const JsonValue& s : ex->as_array()) {
        if (!s.is_string() || s.as_string().empty()) {
          return refuse(BlobError::kScopeMismatch,
                        BlobErrorName(BlobError::kScopeMismatch), index);
        }
        r.exception_sites.push_back(s.as_string());
      }
    }
    const JsonValue* en = rj.find("enabled");
    r.enabled = (en == nullptr) ? true : (en->is_bool() && en->as_bool());
    out->blob.rules.push_back(std::move(r));
    ++index;
  }

  // DIGEST VERIFIED BEFORE USE. There is no "verify and continue anyway" path.
  if (BlobDigest(out->blob) != out->blob.sha256) {
    return refuse(BlobError::kSha256Mismatch,
                  BlobErrorName(BlobError::kSha256Mismatch));
  }

  // Compile the key set exactly once, here. A caller never compiles a blob
  // itself, so "what will this site do?" has one answer.
  std::vector<InputRule> inputs;
  inputs.reserve(out->blob.rules.size());
  for (const BlobRule& r : out->blob.rules) {
    InputRule ir;
    ir.id = r.id;
    ir.selector = r.selector;
    ir.action = r.action;
    ir.style = r.style;
    ir.exception_sites = r.exception_sites;
    ir.enabled = r.enabled;
    inputs.push_back(std::move(ir));
  }
  KeySetError ke = CompileKeySet(inputs, &out->key_set);
  if (ke != KeySetError::kOk) {
    return refuse(BlobError::kRuleRejected, out->key_set.reason,
                  static_cast<int64_t>(out->key_set.failed_index));
  }

  out->valid = true;
  return BlobError::kOk;
}


// The closed refusal vocabulary, shared with the selector parser and with
// fakes/cosmetic.py. A producer naming a reason outside this set means producer
// and consumer disagree about the contract, and guessing is how a validator
// becomes permissive.
const std::set<std::string>& KnownRefusalReasons() {
  static const std::set<std::string> s = [] {
    std::set<std::string> out = {
        "schema-mismatch", "scope-mismatch", "sha256-mismatch",
        "rule-too-large", "too-many-rules", "duplicate-rule-id",
        "unknown-refusal-reason", "no-executable-content"};
    const SelectorError all[] = {
        SelectorError::kEmpty, SelectorError::kTooLong,
        SelectorError::kTooManyCompounds, SelectorError::kTooManyAttrSelectors,
        SelectorError::kTooManyPseudoArgs, SelectorError::kIdentTooLong,
        SelectorError::kUnbalancedParen, SelectorError::kUnbalancedBracket,
        SelectorError::kEmptyCompound, SelectorError::kLeadingCombinator,
        SelectorError::kTrailingCombinator, SelectorError::kDoubleCombinator,
        SelectorError::kTrailingComma, SelectorError::kDisallowedChar,
        SelectorError::kBangImportant, SelectorError::kAtRule,
        SelectorError::kCdataOrMarkup, SelectorError::kUrlFunction,
        SelectorError::kExpressionFunction, SelectorError::kUnknownPseudoClass,
        SelectorError::kDisallowedPseudoArg,
        SelectorError::kUniversalWithPseudo,
        SelectorError::kCommentUnterminated, SelectorError::kEscapeSequence};
    for (SelectorError e : all) out.insert(SelectorErrorName(e));
    return out;
  }();
  return s;
}

}  // namespace xr::cosmetic
