// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the THEME DOC model (P8-T3) + the token-set registry derived
// from ui/themes/tokens.json (the SINGLE token source, contract
// theme-tokens-v1: declarative JSON only, fixed token set per version,
// unknown tokens rejected). A theme doc is a FLAT map of v1 token names to
// typed values. Strictness laws (frozen contract + security): unknown token
// => reject (never ignore), unknown doc keys => reject, non-token keys
// (script/css/import/url) => reject by key and by value scan, value must
// match the declared type exactly, colors are #rrggbb/#rrggbbaa hex only,
// dimensions are sane ints, fonts are system-font stacks (no url(), no
// code, no remote references — the bundled-font list is EMPTY in v0 and
// the format must not grow a font field later without a schema amendment;
// P30 territory, recorded).
//
// Duplicate keys are REJECTED at the raw-text layer (a strict parse keeps
// last — theme import is hostile input, so "keep last" would be a silent
// overwrite; the loader scans the raw text first, T4).
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "themes/core/json.h"

namespace xr::themes {

// One declared token (from tokens.json meta).
struct TokenDef {
  std::string name;
  std::string type;  // color | dimension | font
  bool security_critical = false;
  bool reserved = false;
  std::vector<std::string> pairing;  // audit pairs (foreground/background)
  std::string usage;
};

// A loaded theme VALUE map: token name -> JsonValue (validated, typed).
using TokenMap = std::map<std::string, JsonValue>;

// The built-in theme registry from tokens.json (single source). Built-ins
// ship as DATA inside tokens.json themes.* — the core never hardcodes a
// palette. "system" is a RESOLVER (kind), not a palette of its own: it maps
// to light/dark/high-contrast by mode (system_resolution data) — the
// NativeTheme observation that feeds the mode is browser-side (HG), the
// core consumes the mode as data.
struct BuiltinTheme {
  std::string name;
  TokenMap values;
  std::vector<std::string> waivers;  // canonical JSON rows, one per waiver
};

struct TokenSource {
  std::vector<TokenDef> tokens;              // declaration order
  std::vector<BuiltinTheme> builtins;        // declaration order
  std::string system_default;                // e.g. "light"
  std::map<std::string, std::string> system_modes;  // mode -> builtin
  std::string error;  // non-empty when Load failed (typed, human-readable)
  bool ok = false;
};

// Loads + strictly validates the token source doc (ui/themes/tokens.json
// shape: tokens meta + themes.* full maps + system_resolution).
TokenSource LoadTokenSource(const std::string& json_text);

// ---- theme doc validation (hostile import) ----

struct DocProblem {
  std::string where;   // e.g. "token 'surface'"
  std::string what;    // typed, human-readable reason
};

struct DocVerdict {
  bool ok = false;
  std::vector<DocProblem> problems;
};

// Rejects duplicate keys in raw JSON text (hostile-import law: duplicates
// never silently last-win). Returns the first duplicate key or "".
std::string FindDuplicateKey(const std::string& raw);

// Strict-schema validation of a flat theme doc against the token set.
// `doc` must already be parsed (parsing = depth-capped + UTF-8-validated +
// trailing-rejected by the JSON model). Rejects: unknown token names,
// non-token keys (script/css/import/...), type mismatches, unsafe fonts
// (url(/remote/embedded), huge ints (int32 window), NaN-ish numbers cannot
// exist in strict JSON and are refused at parse), colors outside hex.
// The reserved critical-red law is ENFORCED here too: a doc mapping
// critical-red outside the canonical alarming family is refused.
DocVerdict ValidateThemeDoc(const std::vector<TokenDef>& tokens,
                            const TokenMap& doc);

// Convenience: doc file size cap (T4 hard-reject: >64 KiB refused).
constexpr size_t kMaxThemeDocBytes = 64 * 1024;

// The v1 token set, exported for tests/host diagnostics.
const TokenDef* FindToken(const std::vector<TokenDef>& tokens,
                          const std::string& name);
const BuiltinTheme* FindBuiltin(const std::vector<BuiltinTheme>& builtins,
                                const std::string& name);

}  // namespace xr::themes
