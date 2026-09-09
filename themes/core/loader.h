// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the theme LOADER (P8-T3/T4): validate -> audit -> REFUSE.
// A theme that fails strict schema validation OR the contrast audit NEVER
// applies; there is no "best-effort merge", no partial state. The refusal
// reason is a typed, human-readable string the host surfaces in the UI.
//
// Hostile input from the first commit (T4): the loader is the security
// boundary for custom-theme import. Hard rejects (each with a negative
// test + fuzz invariant): size > 64 KiB; any url( / remote font reference;
// any key outside the fixed v1 token set; any non-token key
// (script/css/import/...); values not matching the declared type; any
// nesting beyond the parser depth cap (64); duplicate keys (raw-text scan,
// never keep-last); NaN-ish numbers / huge ints (strict JSON parser only
// emits int64; ints outside the sane int32 window are refused by type
// validation); invalid UTF-8 (parser-validated). The invariant the fuzzer
// checks is "never apply a theme the audit rejects".
#pragma once

#include <string>
#include <vector>

#include "themes/core/contrast.h"
#include "themes/core/theme.h"

namespace xr::themes {

// Result of one apply/import attempt.
struct ApplyResult {
  bool ok = false;
  std::string refusal;          // typed, human-readable (empty on success)
  std::vector<ContrastFinding> findings;  // full audit (even when refused)
  std::string applied_theme;    // canonical theme name (built-in) on success
  TokenMap applied_values;      // the exact map that was applied
  // Import-only: per-token deltas vs the currently applied theme,
  // "token: old -> new" in canonical order (data-level; no markup render).
  std::vector<std::string> deltas;
  int64_t elapsed_us = 0;       // apply budget measurement (<=100 ms target)
};

// The applied-theme re-emit payload: canonical JSON with the full token map
// + the resolved kind. Both consumers (generated accessors in C++, CSS
// custom-property update in the view layer) are driven from this one
// canonical map — no second source of truth.
struct AppliedEvent {
  std::string theme;      // resolved theme name
  std::string kind;       // "builtin" | "custom"
  TokenMap values;        // full resolved map
  std::string ToCanonicalJson() const;
};

struct LoaderState {
  TokenSource source;      // loaded token set + built-ins
  std::string applied;     // currently applied theme name ("" = none yet)
  std::string mode;        // system mode: light|dark|high-contrast
  TokenMap values;         // currently applied values
  std::string error;       // typed load error (empty when ok)
  bool ok = false;
};

// Loads the token source text (tokens.json). `mode` seeds the system-mode
// resolution (data input; the NativeTheme observation is browser-side HG).
LoaderState LoaderLoad(const std::string& tokens_json_text,
                       const std::string& system_mode);

// Applies a BUILT-IN theme by name (incl. "system" resolver). Validate ->
// audit -> refuse. Measures elapsed us against the <=100 ms budget.
ApplyResult ApplyBuiltin(LoaderState* state, const std::string& name);

// Imports + applies a CUSTOM theme doc (hostile input; T4). raw doc bytes
// are size-capped + duplicate-key scanned + strict-parsed + schema-checked
// + contrast-audited; refusal is atomic (nothing changes on refuse).
ApplyResult ImportTheme(LoaderState* state, const std::string& raw_doc_bytes);

// Audit-only entry (host `validate-doc`): never changes state.
std::vector<ContrastFinding> AuditDoc(const LoaderState& state,
                                      const TokenMap& doc);

}  // namespace xr::themes
