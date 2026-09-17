// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// See cosmetic_host.h for the protocol and the parity contract.

#include "renderer/cosmetic/host/cosmetic_host.h"

#include "common/core/json.h"
#include "renderer/cosmetic/core/pseudo.h"
#include "renderer/cosmetic/core/scope_key.h"
#include "renderer/cosmetic/core/selector.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using xr::common::JsonValue;
using xr::common::ParseJson;

namespace xr::cosmetic {
namespace {

constexpr const char* kSchemaId = "xr-cosmetic-blob-v1";
constexpr int64_t kSchemaVersion = 1;
constexpr size_t kMaxRules = 4096;
constexpr size_t kMaxStyleValueLen = 256;

// The closed refusal vocabulary. The parser names are copied from
// SelectorErrorName() in selector.cc — fakes/cosmetic.py carries the same set,
// and tools/cosmetic_vectors_check.py fails when the two disagree. A name that
// is invented here rather than taken from the core is exactly the drift the
// parity check exists to catch.
const std::set<std::string>& ParserReasons() {
  static const std::set<std::string> s = [] {
    std::set<std::string> out;
    // Enumerate the enum so the set cannot silently miss a value.
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

const std::set<std::string>& BlobReasons() {
  static const std::set<std::string> s = {
      "schema-mismatch", "scope-mismatch", "sha256-mismatch",
      "rule-too-large", "too-many-rules", "duplicate-rule-id",
      "unknown-refusal-reason", "no-executable-content"};
  return s;
}

bool IsKnownReason(const std::string& r) {
  return ParserReasons().count(r) > 0 || BlobReasons().count(r) > 0;
}

// The result shape mirrors fakes/cosmetic.py field-for-field. The fake wraps
// everything in {"ok": ...} because that is the frozen _base envelope; this
// host does the same so the two stdout strings are byte-identical.
JsonValue OkObject(int64_t applied, int64_t enabled, const char* refused,
                   int64_t rule_index, int64_t producer_refusals,
                   int64_t page_modifying) {
  JsonValue::Object inner;
  inner["applied"] = JsonValue(applied);
  inner["enabled"] = JsonValue(enabled);
  if (refused == nullptr) {
    inner["refused"] = JsonValue(nullptr);
  } else {
    inner["refused"] = JsonValue(std::string(refused));
  }
  inner["rule_index"] = JsonValue(rule_index);
  inner["producer_refusals"] = JsonValue(producer_refusals);
  inner["page_modifying"] = JsonValue(page_modifying);
  return JsonValue(JsonValue::Object{{"ok", JsonValue(std::move(inner))}});
}

JsonValue Refused(const char* reason, int64_t rule_index) {
  return OkObject(0, 0, reason, rule_index, 0, 0);
}

JsonValue MalformedError() {
  return JsonValue(JsonValue::Object{{"error", JsonValue("kMalformedInput")}});
}

bool ContainsNoCase(const std::string& haystack, const std::string& needle) {
  if (needle.empty() || haystack.size() < needle.size()) return false;
  for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    bool ok = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
          std::tolower(static_cast<unsigned char>(needle[j]))) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

// Mirrors validate_rule() in fakes/cosmetic.py. The style-map checks are here
// rather than in the parser because they are a blob-level law: a declaration
// cannot be smuggled through a field that is supposed to carry only
// property/value pairs.
const char* ValidateStyle(const JsonValue& rule) {
  const JsonValue* style = rule.find("style");
  if (style == nullptr || !style->is_object() || style->as_object().empty()) {
    return "no-executable-content";
  }
  for (const auto& [prop, value] : style->as_object()) {
    if (!value.is_string()) return "no-executable-content";
    const std::string& v = value.as_string();
    if (v.size() > kMaxStyleValueLen) return "rule-too-large";
    if (v.find('!') != std::string::npos || ContainsNoCase(v, "url(")) {
      return "no-executable-content";
    }
    (void)prop;
  }
  return nullptr;
}

JsonValue ValidateBlob(const JsonValue& blob, const JsonValue* frame_scope) {
  if (!blob.is_object()) return MalformedError();

  const JsonValue* schema = blob.find("schema");
  if (schema == nullptr || !schema->is_string() ||
      schema->as_string() != kSchemaId) {
    return Refused("schema-mismatch", -1);
  }
  const JsonValue* version = blob.find("schema_version");
  if (version == nullptr || !version->is_int() ||
      version->as_int() != kSchemaVersion) {
    return Refused("schema-mismatch", -1);
  }

  const JsonValue* scope = blob.find("scope");
  if (scope == nullptr || !scope->is_object()) return MalformedError();
  const JsonValue* idc = scope->find("identity_class");
  if (idc == nullptr || !idc->is_string() ||
      (idc->as_string() != "anonymous" && idc->as_string() != "authenticated" &&
       idc->as_string() != "enterprise")) {
    return Refused("scope-mismatch", -1);
  }
  const JsonValue* site = scope->find("site");
  if (site == nullptr || !site->is_string() || site->as_string().empty()) {
    return Refused("scope-mismatch", -1);
  }
  if (frame_scope != nullptr && *frame_scope != *scope) {
    // A blob for one site must not apply in another. The embedder is not
    // consulted anywhere in this comparison — see core/scope_key.h.
    return Refused("scope-mismatch", -1);
  }

  const JsonValue* rules = blob.find("rules");
  if (rules == nullptr || !rules->is_array()) return MalformedError();
  if (rules->as_array().size() > kMaxRules) {
    return Refused("too-many-rules", -1);
  }

  // The producer's own refusals must name reasons we recognize. An unknown
  // reason means producer and consumer disagree about the contract, and
  // guessing is how a validator becomes permissive.
  const JsonValue* refusals = blob.find("refusals");
  int64_t producer_refusals = 0;
  if (refusals != nullptr) {
    if (!refusals->is_array()) return MalformedError();
    for (const JsonValue& entry : refusals->as_array()) {
      if (!entry.is_object()) return MalformedError();
      const JsonValue* reason = entry.find("reason");
      if (reason == nullptr || !reason->is_string() ||
          !IsKnownReason(reason->as_string())) {
        const JsonValue* idx = entry.find("rule_index");
        int64_t at = (idx && idx->is_int()) ? idx->as_int() : -1;
        return Refused("unknown-refusal-reason", at);
      }
    }
    producer_refusals = static_cast<int64_t>(refusals->as_array().size());
  }

  std::set<std::string> seen_ids;
  int64_t enabled = 0;
  int64_t page_modifying = 0;
  int64_t index = 0;
  for (const JsonValue& rule : rules->as_array()) {
    if (!rule.is_object()) return Refused("rule-too-large", index);
    const JsonValue* id = rule.find("id");
    if (id == nullptr || !id->is_string() || id->as_string().empty()) {
      return Refused("duplicate-rule-id", index);
    }
    if (!seen_ids.insert(id->as_string()).second) {
      return Refused("duplicate-rule-id", index);
    }
    const JsonValue* action = rule.find("action");
    if (action == nullptr || !action->is_string() ||
        (action->as_string() != "hide" && action->as_string() != "remove" &&
         action->as_string() != "style")) {
      return Refused("unknown-refusal-reason", index);
    }
    const JsonValue* sel = rule.find("selector");
    if (sel == nullptr || !sel->is_string()) {
      return Refused("empty-selector", index);
    }
    // THE PARSE IS THE VALIDATION. Not a regex, not a length check: the same
    // parser the renderer will use, so a rule this host accepts is a rule the
    // renderer can compile.
    Selector parsed;
    SelectorError e = ParseSelector(sel->as_string(), &parsed);
    if (e != SelectorError::kOk) {
      return Refused(SelectorErrorName(e), index);
    }
    if (action->as_string() == "style") {
      const char* r = ValidateStyle(rule);
      if (r != nullptr) return Refused(r, index);
    }
    if (action->as_string() == "remove") ++page_modifying;
    const JsonValue* excepts = rule.find("exception_sites");
    if (excepts != nullptr) {
      if (!excepts->is_array()) return MalformedError();
      for (const JsonValue& s : excepts->as_array()) {
        if (!s.is_string() || s.as_string().empty()) {
          return Refused("scope-mismatch", index);
        }
      }
    }
    const JsonValue* en = rule.find("enabled");
    if (en == nullptr || !en->is_bool() || en->as_bool()) ++enabled;
    ++index;
  }

  return OkObject(index, enabled, nullptr, -1, producer_refusals,
                  page_modifying);
}

std::string ReadStdin() {
  std::string out;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) out.append(buf, n);
  return out;
}

}  // namespace

std::string ValidateBlobJson(const std::string& request_json,
                             bool have_frame_scope, int* exit_code) {
  *exit_code = 0;
  auto parsed = ParseJson(request_json);
  if (!parsed.ok) {
    *exit_code = 1;
    return MalformedError().Canonical();
  }
  const JsonValue* frame_scope = nullptr;
  JsonValue frame_scope_value;
  if (have_frame_scope) {
    // The frame's own scope, supplied by the CALLER. Never derived from the
    // blob itself, and never from an embedder. This mirrors the fake's --scope,
    // which uses the same fixed value so the two backends agree.
    frame_scope_value = JsonValue(JsonValue::Object{
        {"identity_class", JsonValue("anonymous")},
        {"site", JsonValue("example.test")},
    });
    frame_scope = &frame_scope_value;
  }
  return ValidateBlob(parsed.value, frame_scope).Canonical();
}

int HostMain(int argc, char** argv) {
  bool have_frame_scope = false;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--scope") {
      have_frame_scope = true;
    } else if (a.rfind("--", 0) == 0) {
      std::fprintf(stderr, "usage error: unknown option %s\n", a.c_str());
      return 2;
    } else {
      positional.push_back(a);
    }
  }
  if (positional.size() > 1) {
    std::fprintf(stderr, "usage: cosmetic_host [--scope] < blob.json\n");
    return 2;
  }

  std::string raw;
  if (positional.size() == 1) {
    raw = positional[0];
  } else {
    raw = ReadStdin();
  }

  int exit_code = 0;
  std::string out = ValidateBlobJson(raw, have_frame_scope, &exit_code);
  std::printf("%s\n", out.c_str());
  return exit_code;
}

}  // namespace xr::cosmetic

int main(int argc, char** argv) { return xr::cosmetic::HostMain(argc, argv); }
