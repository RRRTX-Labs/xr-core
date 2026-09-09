// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Loader implementation (validate -> audit -> refuse). See loader.h for the
// hostile-input laws. Elapsed-time measurement uses std::chrono::steady_clock
// (std-only).
#include "themes/core/loader.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace xr::themes {
namespace {

using Clock = std::chrono::steady_clock;

// file-local helpers (declared before use in the anonymous namespace)
static std::string ToFixed2(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", v);
  return buf;
}

static std::string BuiltinNames(const std::vector<BuiltinTheme>& builtins) {
  std::string s;
  for (const auto& b : builtins) {
    if (!s.empty()) s += ", ";
    s += b.name;
  }
  return s;
}

std::string RefusalFromFindings(const std::vector<ContrastFinding>& fs) {
  std::string out;
  for (const auto& f : fs) {
    if (f.passed && !f.unnecessary) continue;
    if (!out.empty()) out += "; ";
    if (f.unnecessary)
      out += "waiver for passing pair " + f.token + "/" + f.pair +
             " (ratio " + ToFixed2(f.ratio) + " >= " + ToFixed2(f.required) +
             ") is unnecessary (waivers must be exact data)";
    else if (f.waiver_mismatch)
      out += "waiver best for " + f.token + "/" + f.pair +
             " does not match actual ratio " + ToFixed2(f.ratio);
    else
      out += "contrast " + f.token + "/" + f.pair + ": " + ToFixed2(f.ratio) +
             ":1 < required " + ToFixed2(f.required) + ":1" +
             (f.token.find("critical") != std::string::npos
                  ? " (security-critical pair)"
                  : "");
  }
  return out;
}

}  // namespace

LoaderState LoaderLoad(const std::string& tokens_json_text,
                       const std::string& system_mode) {
  LoaderState st;
  st.source = LoadTokenSource(tokens_json_text);
  if (!st.source.ok) {
    st.error = "tokens source rejected (strict): " + st.source.error;
    return st;
  }
  if (system_mode != "light" && system_mode != "dark" &&
      system_mode != "high-contrast") {
    st.error = "unknown system mode '" + system_mode + "' (light|dark|"
               "high-contrast)";
    return st;
  }
  st.mode = system_mode;
  st.applied = "system";
  // System resolves through the data map; if the resolution default is
  // missing the loader refuses (never a half state).
  auto it = st.source.system_modes.find(system_mode);
  const std::string& target = it != st.source.system_modes.end()
                                  ? it->second
                                  : st.source.system_default;
  const BuiltinTheme* bt = FindBuiltin(st.source.builtins, target);
  if (bt == nullptr) {
    st.error = "system mode '" + system_mode + "' cannot resolve";
    return st;
  }
  st.applied = bt->name;
  st.values = bt->values;
  st.ok = true;
  return st;
}

ApplyResult ApplyBuiltin(LoaderState* state, const std::string& name) {
  ApplyResult r;
  if (state == nullptr || !state->ok) {
    r.refusal = "loader is not loaded";
    return r;
  }
  auto start = Clock::now();
  TokenMap target;
  std::string applied_name = name;
  const BuiltinTheme* bt = nullptr;
  if (name == "system") {
    auto it = state->source.system_modes.find(state->mode);
    const std::string& t = it != state->source.system_modes.end()
                               ? it->second
                               : state->source.system_default;
    bt = FindBuiltin(state->source.builtins, t);
    applied_name = t;
  } else {
    bt = FindBuiltin(state->source.builtins, name);
  }
  if (bt == nullptr) {
    r.refusal = "unknown theme '" + name + "' (built-ins: " +
                BuiltinNames(state->source.builtins) + ")";
    return r;
  }
  target = bt->values;
  // Strict schema re-validation (defense in depth: data is trusted but the
  // gate stays armed).
  DocVerdict dv = ValidateThemeDoc(state->source.tokens, target);
  if (!dv.ok) {
    r.refusal = "built-in '" + name + "' failed schema validation: " +
                dv.problems[0].what;
    return r;
  }
  // Contrast audit with the built-in's waiver rows.
  r.findings = AuditTheme(target, state->source.tokens, bt->waivers);
  std::string refusal = RefusalFromFindings(r.findings);
  if (!refusal.empty()) {
    r.refusal = "theme '" + name + "' refused: " + refusal;
    return r;
  }
  state->applied = applied_name;
  state->values = target;
  r.ok = true;
  r.applied_theme = applied_name;
  r.applied_values = target;
  r.elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now() - start)
                     .count();
  return r;
}

std::vector<ContrastFinding> AuditDoc(const LoaderState& state,
                                      const TokenMap& doc) {
  return AuditTheme(doc, state.source.tokens, {});
}

ApplyResult ImportTheme(LoaderState* state, const std::string& raw) {
  ApplyResult r;
  if (state == nullptr || !state->ok) {
    r.refusal = "loader is not loaded";
    return r;
  }
  auto start = Clock::now();
  // 1. size cap (T4 hard reject)
  if (raw.size() > kMaxThemeDocBytes) {
    r.refusal = "theme doc exceeds the 64 KiB cap (" +
                std::to_string(raw.size()) + " bytes) — refused";
    return r;
  }
  // 2. duplicate keys (raw scan — never keep-last on hostile input)
  std::string dup = FindDuplicateKey(raw);
  if (!dup.empty()) {
    r.refusal = "theme doc has duplicate key '" + dup +
                "' — refused (hostile input never last-wins)";
    return r;
  }
  // 3. strict parse (depth-capped 64, UTF-8-validated, trailing rejected)
  JsonParseResult pr = ParseJson(raw);
  if (!pr.ok) {
    r.refusal = "theme doc is not strict JSON: " + pr.error;
    return r;
  }
  if (!pr.value.is_object()) {
    r.refusal = "theme doc must be a flat JSON object of token values";
    return r;
  }
  // 4. flat map of token name -> JsonValue
  TokenMap doc;
  for (const auto& [k, v] : pr.value.as_object()) doc[k] = v;
  // 5. strict schema validation (unknown/non-token keys, types, reserved
  //    critical-red law, unsafe fonts)
  DocVerdict dv = ValidateThemeDoc(state->source.tokens, doc);
  if (!dv.ok) {
    r.refusal = "theme doc rejected (strict):";
    for (const auto& p : dv.problems)
      r.refusal += " [" + p.where + "] " + p.what + ";";
    return r;
  }
  // 6. contrast audit (custom docs may not carry waivers — v1: no waiver
  //    mechanism for imported themes; recorded: import must pass in full)
  r.findings = AuditDoc(*state, doc);
  std::string refusal = RefusalFromFindings(r.findings);
  if (!refusal.empty()) {
    r.refusal = "custom theme refused: " + refusal;
    return r;
  }
  // 7. deltas vs the currently applied theme (per token, old -> new)
  for (const auto& [k, v] : doc) {
    auto old = state->values.find(k);
    std::string o = old == state->values.end() ? "" : old->second.Canonical();
    std::string n = v.Canonical();
    if (o != n)
      r.deltas.push_back(k + ": " + o + " -> " + n);
  }
  // 8. apply atomically (refusal above leaves state untouched)
  state->applied = "custom";
  state->values = doc;
  r.ok = true;
  r.applied_theme = "custom";
  r.applied_values = doc;
  r.elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now() - start)
                     .count();
  return r;
}

std::string AppliedEvent::ToCanonicalJson() const {
  JsonValue::Object obj;
  obj["theme"] = JsonValue(theme);
  obj["kind"] = JsonValue(kind);
  JsonValue::Object vals;
  for (const auto& [k, v] : values) vals[k] = v;
  obj["values"] = JsonValue(std::move(vals));
  return JsonValue(std::move(obj)).Canonical();
}

}  // namespace xr::themes
