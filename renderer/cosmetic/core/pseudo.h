// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/pseudo — the `#?#` PROCEDURAL pseudo-class
// table (P12-T1). Each entry carries a capability flag and a SUPPORT flag, and
// every entry cites the line in the vendored tree that decides whether the
// engine can express it. A pseudo-class the vendored engine cannot represent
// is REFUSED here; the refusal list is that citation's shadow.
//
// Citations are to third_party/rust/adblock/0.13.3 (the sealed pin; the path
// prefix is elided below and spelled out per entry):
//
//   src/filters/cosmetic.rs:1164-1185  enum NonTSPseudoClass — the engine's
//     own procedural-operator set: HasText, MatchesAttr, MatchesCss,
//     MatchesCssBefore, MatchesCssAfter, MatchesPath, MinTextLength, Upward,
//     Xpath, and AnythingElse (a native CSS pseudo-class that is not a
//     procedural operator).
//   src/filters/cosmetic.rs:978-980    the ABP aliases it accepts:
//     `-abp-has` -> `has`, `-abp-contains` -> `has-text`,
//     `contains` -> `has-text`.
//   src/filters/cosmetic.rs:726-738    is_procedural_operator — the exact set
//     it treats as procedural (the nine above).
//   src/filters/cosmetic.rs:749-752    ProceduralFilterWithMultipleSelectors:
//     a procedural filter may not be part of a selector list. Our parser
//     already refuses selector lists wholesale (kTrailingComma), which is
//     strictly narrower.
//
// What we do NOT admit, and why:
//   * `:xpath(...)` is in the engine's set but is refused here. It is not a CSS
//     selector, so it cannot be expressed through the CSSOM path this design
//     commits to (arch §6: style declarations only through the cited CSSOM
//     path, never by writing HTML), and it gives a rule a second, unauditable
//     selector language.
//   * `:matches-css{,-before,-after}(...)` are refused. They require reading
//     COMPUTED style at match time, which means per-node getComputedStyle on
//     every mutation — the DOM-poll cost the plan budgets separately, and a
//     capability we cannot bound here. Recorded as a divergence, not silently
//     dropped.
//   * Native CSS pseudo-classes are admitted only from kAdmittedNative below.
//     The engine passes anything else through as AnythingElse
//     (cosmetic.rs:1184, :963-966), which is precisely why an allowlist is
//     needed on our side: "the engine accepts it" is not the same as "we
//     understand it".
#pragma once

#include <string>
#include <vector>

namespace xr::cosmetic {

// What implementing a pseudo-class requires of the renderer. This is the
// capability note the registry check demands, expressed as data.
enum class PseudoCapability {
  kNone = 0,           // pure selector semantics; no extra work
  kTextContent,        // must read textContent (has-text, min-text-length)
  kSubtreeQuery,       // must run a nested selector query (has, not, is)
  kAttributeMatch,     // must read attributes (matches-attr)
  kDocumentUrl,        // must consult the document URL (matches-path)
  kAncestorWalk,       // must walk ancestors (upward, nth-ancestor)
  kDomRemoval,         // page-modifying: removes nodes
  kMainWorld,          // FORBIDDEN: requires a main-world handle (never set)
};

const char* PseudoCapabilityName(PseudoCapability c);

// Whether this build admits the pseudo-class. `support` is what makes the
// "engine can express it" vs "we allow it" distinction machine-checkable.
enum class PseudoSupport {
  kAdmitted = 0,       // parses, compiles, ships
  kRefusedNoCssom,     // engine has it; not expressible via the CSSOM path
  kRefusedUnbounded,   // engine has it; cost we cannot bound in v1
  kRefusedNotInEngine, // not in the vendored set at all
};

const char* PseudoSupportName(PseudoSupport s);

struct PseudoInfo {
  const char* name;              // lowercased, no leading ':'
  PseudoCapability capability;
  PseudoSupport support;
  bool takes_arg;                // functional form `:name(arg)`
  bool arg_is_selector;          // the arg must itself parse as a Selector
  const char* citation;          // path:line in the vendored tree (or why not)
  const char* note;
};

// The table. Order is not significant; lookups are by name.
const std::vector<PseudoInfo>& PseudoTable();

// Look up a pseudo-class by lowercased name. Returns nullptr when the name is
// not in the table at all (which the caller must refuse — deny by default).
const PseudoInfo* LookupPseudo(const std::string& name);

// The ABP alias map the vendored engine honours (cosmetic.rs:978-980). Applied
// BEFORE the table lookup so `:-abp-has(...)` and `:has(...)` compile to the
// same thing instead of drifting. Returns the canonical name, or "" when the
// input is not a known alias.
const char* CanonicalizePseudoAlias(const std::string& name);

// Native CSS pseudo-classes we admit (the AnythingElse class the engine passes
// through unvalidated). Deliberately short.
bool IsAdmittedNativePseudo(const std::string& name);

// True when the pseudo-class requires a page-modifying capability. Used by the
// event schema and the Observatory categorization, which must label such rules
// honestly (plan UX: soft-wall scriptlets listed as page-modifying).
bool IsPageModifying(const PseudoInfo& info);

}  // namespace xr::cosmetic
