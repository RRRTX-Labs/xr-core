// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// See cosmetic_host.h for the protocol and the parity contract.

#include "renderer/cosmetic/host/cosmetic_host.h"

#include "common/core/json.h"
#include "renderer/cosmetic/core/blob.h"
#include "renderer/cosmetic/core/degrade.h"
#include "renderer/cosmetic/core/pseudo.h"
#include "renderer/cosmetic/core/scope_key.h"
#include "renderer/cosmetic/core/selector.h"
#include "renderer/cosmetic/core/style.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using xr::common::JsonValue;
using xr::common::ParseJson;

namespace xr::cosmetic {
namespace {

// The feature flag. Default OFF, and the off state is asserted to be identical
// to today's product: no call site active, no observer installed.
constexpr bool kCosmeticFlagDefault = false;
constexpr bool kScriptletsFlagDefault = false;

// Typed rejection (exit 0): a legitimate refused outcome, not a protocol error.
// Mirrors the shield_host/update_host living-surface shape.
JsonValue Reject(const std::string& reason, const std::string& detail = "") {
  JsonValue::Object o;
  o["error"] = JsonValue("kRejected");
  o["reason"] = JsonValue(reason);
  if (!detail.empty()) o["detail"] = JsonValue(detail);
  return JsonValue(std::move(o));
}

// Typed error (exit 1): the request itself was unusable.
JsonValue Error(const char* code, const std::string& detail) {
  JsonValue::Object o;
  o["detail"] = JsonValue(detail);
  o["error"] = JsonValue(std::string(code));
  return JsonValue(std::move(o));
}

bool GetObject(const JsonValue& req, const char* key, const JsonValue** out) {
  const JsonValue* v = req.find(key);
  if (v == nullptr || !v->is_object()) return false;
  *out = v;
  return true;
}

bool GetString(const JsonValue& o, const char* key, std::string* out) {
  const JsonValue* v = o.find(key);
  if (v == nullptr || !v->is_string()) return false;
  *out = v->as_string();
  return true;
}

std::string ReadStdin() {
  std::string out;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) out.append(buf, n);
  return out;
}

// --- flag-status -------------------------------------------------------------
// The kill-switch state. The debug page reads this to print the scriptlet
// registry as "inert: flag off" verbatim when the flag is off.
JsonValue FlagStatus(bool cosmetic, bool scriptlets) {
  JsonValue::Object o;
  o["xr_shield_cosmetic_v1"] = JsonValue(cosmetic ? "on" : "off");
  o["xr_shield_scriptlets"] = JsonValue(scriptlets ? "on" : "off");
  // The verbatim string the debug page must show when scriptlets are off. It is
  // produced here rather than in the UI so the two cannot drift.
  o["scriptlet_registry_state"] =
      JsonValue(scriptlets ? "active" : "inert: flag off");
  return JsonValue(std::move(o));
}

// --- selector-parse ----------------------------------------------------------
JsonValue SelectorParse(const std::string& text) {
  Selector s;
  SelectorError e = ParseSelector(text, &s);
  if (e != SelectorError::kOk) return Reject(SelectorErrorName(e));
  JsonValue::Object o;
  o["canonical"] = JsonValue(CanonicalizeSelector(s));
  o["compounds"] = JsonValue(static_cast<int64_t>(s.compounds.size()));
  JsonValue::Array pseudos;
  for (const Compound& c : s.compounds) {
    for (const PseudoClass& p : c.pseudos) pseudos.push_back(JsonValue(p.name));
  }
  o["pseudos"] = JsonValue(std::move(pseudos));
  return JsonValue(std::move(o));
}

// --- scope-key ---------------------------------------------------------------
JsonValue ScopeKeyMethod(const JsonValue& in) {
  ScopeInput si;
  if (!GetString(in, "frame_site", &si.frame_site)) {
    return Error("kMalformedInput", "frame_site must be a string");
  }
  if (!GetString(in, "frame_identity", &si.frame_identity)) {
    return Error("kMalformedInput", "frame_identity must be a string");
  }
  std::string trust;
  if (GetString(in, "trust", &trust)) {
    if (trust == "anonymous") si.trust = IdentityTrust::kAnonymous;
    else if (trust == "authenticated") si.trust = IdentityTrust::kAuthenticated;
    else if (trust == "enterprise") si.trust = IdentityTrust::kEnterprise;
    else return Error("kMalformedInput", "trust is not a known identity class");
  }
  std::string nav;
  if (GetString(in, "navigation", &nav)) {
    if (nav == "initial") si.navigation = NavigationClass::kInitial;
    else if (nav == "cross-document") si.navigation = NavigationClass::kCrossDocument;
    else if (nav == "same-document") si.navigation = NavigationClass::kSameDocument;
    else return Error("kMalformedInput", "navigation is not a known class");
  }
  if (!GetString(in, "url_class", &si.document_url_class)) {
    return Error("kMalformedInput", "url_class must be a string");
  }
  // The embedder is NEVER an input. A caller that supplies one is refused, so
  // the "key on the top-level site only" optimization is structurally
  // impossible rather than merely discouraged.
  std::string embedder;
  if (GetString(in, "embedder_site", &embedder) && !embedder.empty()) {
    return Reject("embedder-refused",
                  "the embedder site is not an input to the scope key");
  }
  ScopeKey key;
  ScopeError e = DeriveScopeKey(si, "", &key);
  if (e != ScopeError::kOk) return Reject(ScopeErrorName(e));
  JsonValue::Object o;
  o["hex"] = JsonValue(key.hex);
  o["partition"] = JsonValue(key.partition);
  return JsonValue(std::move(o));
}

// --- blob-build / blob-check / key-set ---------------------------------------
// Builds a blob from a rule list and signs it with the shared digest. The
// producer path: what list compilation calls.
JsonValue BlobBuild(const JsonValue& in) {
  CosmeticBlob b;
  if (!GetString(in, "blob_id", &b.blob_id) || b.blob_id.empty()) {
    return Error("kMalformedInput", "blob_id must be a non-empty string");
  }
  const JsonValue* epoch = in.find("generated_epoch");
  if (epoch == nullptr || !epoch->is_int()) {
    return Error("kMalformedInput", "generated_epoch must be an integer");
  }
  b.generated_epoch = epoch->as_int();
  const JsonValue* scope;
  if (!GetObject(in, "scope", &scope)) {
    return Error("kMalformedInput", "scope must be an object");
  }
  if (!GetString(*scope, "site", &b.scope.site) ||
      !GetString(*scope, "identity_class", &b.scope.identity_class)) {
    return Error("kMalformedInput", "scope needs site and identity_class");
  }
  const JsonValue* rl = in.find("rules");
  if (rl == nullptr || !rl->is_array()) {
    return Error("kMalformedInput", "rules must be an array");
  }
  int64_t index = 0;
  for (const JsonValue& rj : rl->as_array()) {
    if (!rj.is_object()) {
      return Reject("rule-rejected", "rule " + std::to_string(index) +
                                         " is not an object");
    }
    BlobRule r;
    if (!GetString(rj, "id", &r.id) || !GetString(rj, "selector", &r.selector) ||
        !GetString(rj, "action", &r.action)) {
      return Reject("rule-rejected", "rule " + std::to_string(index) +
                                         " needs id, selector and action");
    }
    const JsonValue* st = rj.find("style");
    if (st != nullptr) {
      if (!st->is_object()) {
        return Reject("rule-rejected", "style must be an object");
      }
      for (const auto& [k, v] : st->as_object()) {
        if (!v.is_string()) {
          return Reject("rule-rejected", "style values must be strings");
        }
        r.style.emplace_back(k, v.as_string());
      }
    }
    const JsonValue* ex = rj.find("exception_sites");
    if (ex != nullptr) {
      if (!ex->is_array()) {
        return Reject("rule-rejected", "exception_sites must be an array");
      }
      for (const JsonValue& s : ex->as_array()) {
        if (!s.is_string()) {
          return Reject("rule-rejected", "exception_sites must be strings");
        }
        r.exception_sites.push_back(s.as_string());
      }
    }
    const JsonValue* en = rj.find("enabled");
    r.enabled = (en == nullptr) ? true : (en->is_bool() && en->as_bool());
    b.rules.push_back(std::move(r));
    ++index;
  }
  b.sha256 = BlobDigest(b);
  // Return the canonical blob text so the producer can store or sign it. The
  // digest is included, so this is the exact byte string blob-check will verify.
  JsonValue::Object o;
  o["blob"] = JsonValue(BlobToCanonicalJson(b));
  o["bytes"] = JsonValue(static_cast<int64_t>(BlobToCanonicalJson(b).size()));
  o["sha256"] = JsonValue(b.sha256);
  return JsonValue(std::move(o));
}

// Verifies a blob and compiles its key set. The consumer path: what the
// renderer calls at document-start. `frame_scope`, when present, is the FRAME's
// own scope, supplied by the caller — never derived from the blob and never
// from an embedder.
JsonValue BlobCheck(const JsonValue& in) {
  const JsonValue* bt = in.find("blob");
  if (bt == nullptr || !bt->is_string()) {
    return Error("kMalformedInput", "blob must be a string");
  }
  BlobScope frame;
  bool have_frame = false;
  const JsonValue* fs;
  if (GetObject(in, "frame_scope", &fs)) {
    if (!GetString(*fs, "site", &frame.site) ||
        !GetString(*fs, "identity_class", &frame.identity_class)) {
      return Error("kMalformedInput",
                   "frame_scope needs site and identity_class");
    }
    have_frame = true;
  }
  BlobResult res;
  BlobError e = ParseBlob(bt->as_string(), have_frame ? &frame : nullptr, &res);
  if (e != BlobError::kOk) {
    return Reject(res.reason.empty() ? BlobErrorName(e) : res.reason,
                  res.failed_index >= 0
                      ? "rule " + std::to_string(res.failed_index)
                      : "");
  }
  JsonValue::Object o;
  o["blob_id"] = JsonValue(res.blob.blob_id);
  o["compiled_selectors"] =
      JsonValue(static_cast<int64_t>(res.key_set.compiled_selectors));
  o["duplicates_removed"] =
      JsonValue(static_cast<int64_t>(res.key_set.duplicates_removed));
  o["bytes"] = JsonValue(static_cast<int64_t>(res.key_set.bytes));
  int64_t page_modifying = 0;
  for (const CompiledRule& r : res.key_set.rules) {
    if (ActionIsPageModifying(r.action)) ++page_modifying;
  }
  o["page_modifying"] = JsonValue(page_modifying);
  o["producer_refusals"] =
      JsonValue(static_cast<int64_t>(res.blob.refusals.size()));
  o["sha256"] = JsonValue(res.blob.sha256);
  return JsonValue(std::move(o));
}

// Compiles a key set from a rule list without a blob envelope. Used by the
// perf benchmark, which measures compilation cost and must not have to build
// and sign a blob to do it.
JsonValue KeySetMethod(const JsonValue& in) {
  const JsonValue* rl = in.find("rules");
  if (rl == nullptr || !rl->is_array()) {
    return Error("kMalformedInput", "rules must be an array");
  }
  std::vector<InputRule> inputs;
  int64_t index = 0;
  for (const JsonValue& rj : rl->as_array()) {
    if (!rj.is_object()) {
      return Reject("rule-rejected", "rule " + std::to_string(index) +
                                         " is not an object");
    }
    InputRule r;
    if (!GetString(rj, "id", &r.id) || !GetString(rj, "selector", &r.selector) ||
        !GetString(rj, "action", &r.action)) {
      return Reject("rule-rejected", "rule " + std::to_string(index) +
                                         " needs id, selector and action");
    }
    const JsonValue* st = rj.find("style");
    if (st != nullptr && st->is_object()) {
      for (const auto& [k, v] : st->as_object()) {
        if (v.is_string()) r.style.emplace_back(k, v.as_string());
      }
    }
    const JsonValue* ex = rj.find("exception_sites");
    if (ex != nullptr && ex->is_array()) {
      for (const JsonValue& s : ex->as_array()) {
        if (s.is_string()) r.exception_sites.push_back(s.as_string());
      }
    }
    const JsonValue* en = rj.find("enabled");
    r.enabled = (en == nullptr) ? true : (en->is_bool() && en->as_bool());
    inputs.push_back(std::move(r));
    ++index;
  }
  KeySetResult res;
  KeySetError e = CompileKeySet(inputs, &res);
  if (e != KeySetError::kOk) {
    return Reject(res.reason.empty() ? KeySetErrorName(e) : res.reason);
  }
  JsonValue::Object o;
  o["compiled_selectors"] =
      JsonValue(static_cast<int64_t>(res.compiled_selectors));
  o["duplicates_removed"] =
      JsonValue(static_cast<int64_t>(res.duplicates_removed));
  o["bytes"] = JsonValue(static_cast<int64_t>(res.bytes));
  return JsonValue(std::move(o));
}

// Reports what the degrade table says about a condition. Exposed so the debug
// page and the degrade tests read the SAME table the renderer uses, rather than
// restating it.
JsonValue DegradeApply(const JsonValue& in) {
  std::string name;
  if (!GetString(in, "condition", &name)) {
    return Error("kMalformedInput", "condition must be a string");
  }
  const DegradeCondition all[] = {
      DegradeCondition::kFlagOff, DegradeCondition::kScriptletsOff,
      DegradeCondition::kBlobInvalid, DegradeCondition::kBlobMissing,
      DegradeCondition::kScopeMismatch, DegradeCondition::kRuleUnparsable,
      DegradeCondition::kRuleUnknownPseudo, DegradeCondition::kRuleTooExpensive,
      DegradeCondition::kShieldsDown, DegradeCondition::kEmptyRuleSet,
      DegradeCondition::kDomMutationStorm, DegradeCondition::kEngineUnavailable,
      DegradeCondition::kMainWorldRequired, DegradeCondition::kGenericSetOnly};
  for (DegradeCondition c : all) {
    if (name == DegradeConditionName(c)) {
      const DegradeRow& row = LookupDegrade(c);
      JsonValue::Object o;
      o["condition"] = JsonValue(std::string(DegradeConditionName(c)));
      o["outcome"] = JsonValue(std::string(DegradeOutcomeName(row.outcome)));
      o["page_effect"] = JsonValue(std::string(row.page_effect));
      o["reported"] = JsonValue(row.reported);
      o["reason"] = JsonValue(std::string(row.reason ? row.reason : ""));
      return JsonValue(std::move(o));
    }
  }
  // An unknown condition is refused rather than defaulted, so a caller cannot
  // ask about a state the table does not cover and get a plausible answer.
  return Reject("unknown-degrade-condition", name);
}

// The debug page's cosmetic rows. Every value here is dev-build-only through
// the P11 --build-channel mechanism; with no rules and the flag off the counts
// are all zero, which is the "off state identical to today" assertion.
JsonValue PageStates(bool cosmetic, size_t rule_count) {
  JsonValue::Object o;
  o["cosmetic_enabled"] = JsonValue(cosmetic);
  o["observer_installed"] = JsonValue(ShouldInstallObserver(rule_count, cosmetic));
  o["generic_set_applies"] = JsonValue(GenericSetApplies(false, cosmetic));
  o["keyset_rules"] = JsonValue(static_cast<int64_t>(rule_count));
  o["refused_selectors"] = JsonValue(static_cast<int64_t>(0));
  o["refused_pseudos"] = JsonValue(static_cast<int64_t>(0));
  o["degrade_events"] = JsonValue(static_cast<int64_t>(0));
  o["blob_cache_entries"] = JsonValue(static_cast<int64_t>(0));
  // The blob cache occupancy is a network-service value this host does not own.
  // Reporting a number here would be inventing one, so it is explicitly NOT-RUN
  // until the cache side exists.
  o["blob_cache_occupancy"] = JsonValue("NOT-RUN (network-service side)");
  return JsonValue(std::move(o));
}

std::string Dispatch(const std::string& method, const JsonValue& req,
                     bool cosmetic, bool scriptlets, int* exit_code) {
  *exit_code = 0;
  if (method == "flag-status") return FlagStatus(cosmetic, scriptlets).Canonical();
  if (method == "selector-parse") {
    std::string sel;
    if (!GetString(req, "selector", &sel)) {
      *exit_code = 1;
      return Error("kMalformedInput", "selector must be a string").Canonical();
    }
    return SelectorParse(sel).Canonical();
  }
  if (method == "scope-key") return ScopeKeyMethod(req).Canonical();
  if (method == "blob-build") return BlobBuild(req).Canonical();
  if (method == "blob-check") return BlobCheck(req).Canonical();
  if (method == "key-set") return KeySetMethod(req).Canonical();
  if (method == "degrade-apply") return DegradeApply(req).Canonical();
  if (method == "page-states") {
    size_t rules = 0;
    const JsonValue* r = req.find("keyset_rules");
    if (r != nullptr && r->is_int() && r->as_int() > 0) {
      rules = static_cast<size_t>(r->as_int());
    }
    return PageStates(cosmetic, rules).Canonical();
  }
  // Unknown method is a typed error, never a silent no-op: a caller that
  // misspells a method must find out.
  *exit_code = 1;
  return Error("kUnknownMethod", method).Canonical();
}

}  // namespace

int HostMain(int argc, char** argv) {
  bool cosmetic = kCosmeticFlagDefault;
  bool scriptlets = kScriptletsFlagDefault;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--flag" && i + 1 < argc) {
      std::string kv = argv[++i];
      auto eq = kv.find('=');
      if (eq == std::string::npos) {
        std::fprintf(stderr, "usage error: --flag k=v\n");
        return 2;
      }
      std::string key = kv.substr(0, eq);
      std::string val = kv.substr(eq + 1);
      bool on = (val == "on" || val == "true");
      if (key == "xr_shield_cosmetic_v1") cosmetic = on;
      else if (key == "xr_shield_scriptlets") scriptlets = on;
      else {
        std::fprintf(stderr, "usage error: unknown flag %s\n", key.c_str());
        return 2;
      }
    } else if (a.rfind("--", 0) == 0) {
      std::fprintf(stderr, "usage error: unknown option %s\n", a.c_str());
      return 2;
    } else {
      positional.push_back(a);
    }
  }
  if (positional.size() > 1) {
    std::fprintf(stderr,
                 "usage: cosmetic_host [--flag k=v ...] [request.json]\n");
    return 2;
  }
  std::string raw = positional.size() == 1 ? positional[0] : ReadStdin();
  auto parsed = ParseJson(raw);
  if (!parsed.ok || !parsed.value.is_object()) {
    std::printf("%s\n", Error("kMalformedInput", "request must be a JSON object")
                             .Canonical()
                             .c_str());
    return 1;
  }
  std::string method;
  if (!GetString(parsed.value, "method", &method)) {
    std::printf("%s\n",
                Error("kMalformedInput", "method must be a string").Canonical().c_str());
    return 1;
  }
  int exit_code = 0;
  std::string out = Dispatch(method, parsed.value, cosmetic, scriptlets,
                             &exit_code);
  std::printf("%s\n", out.c_str());
  return exit_code;
}

}  // namespace xr::cosmetic

int main(int argc, char** argv) { return xr::cosmetic::HostMain(argc, argv); }
