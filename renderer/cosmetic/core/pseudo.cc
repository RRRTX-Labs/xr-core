// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// See pseudo.h for the table's citation basis.

#include "renderer/cosmetic/core/pseudo.h"

#include <cstring>

namespace xr::cosmetic {
namespace {

// The vendored tree prefix, spelled out once so every citation below is a
// resolvable path:line rather than a shorthand nobody can follow.
constexpr const char* kVend = "third_party/rust/adblock/0.13.3/";

}  // namespace

const char* PseudoCapabilityName(PseudoCapability c) {
  switch (c) {
    case PseudoCapability::kNone: return "none";
    case PseudoCapability::kTextContent: return "text-content";
    case PseudoCapability::kSubtreeQuery: return "subtree-query";
    case PseudoCapability::kAttributeMatch: return "attribute-match";
    case PseudoCapability::kDocumentUrl: return "document-url";
    case PseudoCapability::kAncestorWalk: return "ancestor-walk";
    case PseudoCapability::kDomRemoval: return "dom-removal";
    case PseudoCapability::kMainWorld: return "main-world";
  }
  return "unknown";
}

const char* PseudoSupportName(PseudoSupport s) {
  switch (s) {
    case PseudoSupport::kAdmitted: return "admitted";
    case PseudoSupport::kRefusedNoCssom: return "refused-no-cssom";
    case PseudoSupport::kRefusedUnbounded: return "refused-unbounded";
    case PseudoSupport::kRefusedNotInEngine: return "refused-not-in-engine";
  }
  return "unknown";
}

const std::vector<PseudoInfo>& PseudoTable() {
  static const std::vector<PseudoInfo> table = {
    // --- the engine's procedural set (cosmetic.rs:1164-1185) ---
    {"has-text", PseudoCapability::kTextContent, PseudoSupport::kAdmitted,
     true, false,
     "src/filters/cosmetic.rs:1166 (NonTSPseudoClass::HasText)",
     "uBO :has-text / ABP :-abp-contains. Reads textContent; the argument is a "
     "plain substring, not a selector, and is never interpolated into CSS."},
    {"matches-attr", PseudoCapability::kAttributeMatch,
     PseudoSupport::kRefusedUnbounded, true, false,
     "src/filters/cosmetic.rs:1168 (NonTSPseudoClass::MatchesAttr)",
     "REFUSED: the engine's argument is a REGEX over attribute values "
     "(cosmetic.rs:991). Shipping a regex evaluator into the renderer is a new "
     "attack surface and an unbounded per-node cost; recorded as a divergence "
     "rather than silently dropped."},
    {"matches-css", PseudoCapability::kNone, PseudoSupport::kRefusedUnbounded,
     true, false,
     "src/filters/cosmetic.rs:1170 (NonTSPseudoClass::MatchesCss)",
     "REFUSED: requires getComputedStyle per candidate node on every mutation. "
     "That is the DOM-poll cost the plan budgets separately, and it cannot be "
     "bounded in v1."},
    {"matches-css-before", PseudoCapability::kNone,
     PseudoSupport::kRefusedUnbounded, true, false,
     "src/filters/cosmetic.rs:1172 (MatchesCssBefore)",
     "REFUSED: as matches-css, on the ::before pseudo-element."},
    {"matches-css-after", PseudoCapability::kNone,
     PseudoSupport::kRefusedUnbounded, true, false,
     "src/filters/cosmetic.rs:1174 (MatchesCssAfter)",
     "REFUSED: as matches-css, on the ::after pseudo-element."},
    {"matches-path", PseudoCapability::kDocumentUrl,
     PseudoSupport::kAdmitted, true, false,
     "src/filters/cosmetic.rs:1176 (MatchesPath)",
     "The engine's form is a regex over the URL. We admit the CLASS but only "
     "with a prefix/equality argument: the compiler rewrites it to the "
     "document-url scope key (scope_key.h), so no regex runs in the renderer."},
    {"min-text-length", PseudoCapability::kTextContent,
     PseudoSupport::kAdmitted, true, false,
     "src/filters/cosmetic.rs:1178 (MinTextLength)",
     "Argument must parse as a non-negative integer; anything else is refused."},
    {"upward", PseudoCapability::kAncestorWalk, PseudoSupport::kAdmitted,
     true, false,
     "src/filters/cosmetic.rs:1180 (Upward)",
     "uBO :upward(N) or :upward(selector). N is bounded by kMaxAncestorWalk so "
     "a rule cannot walk to the root of a deep document for free."},
    {"xpath", PseudoCapability::kNone, PseudoSupport::kRefusedNoCssom,
     true, false,
     "src/filters/cosmetic.rs:1182 (Xpath)",
     "REFUSED: not a CSS selector, so it cannot ride the CSSOM path this "
     "design commits to, and it would give a rule a second unauditable "
     "selector language."},

    // --- ABP aliases, canonicalized before lookup (cosmetic.rs:978-980) ---
    {"has", PseudoCapability::kSubtreeQuery, PseudoSupport::kAdmitted,
     true, true,
     "src/filters/cosmetic.rs:978 (\"-abp-has\" => \"has\")",
     "Native CSS :has() plus the engine's -abp-has alias. The argument is a "
     "nested SELECTOR, so it is parsed by ParseSelector recursively and is "
     "subject to the same bounds and refusals."},
    {"not", PseudoCapability::kSubtreeQuery, PseudoSupport::kAdmitted,
     true, true,
     "native CSS (engine passes it through as AnythingElse, "
     "cosmetic.rs:1184/:963-966)",
     "Argument is a nested selector, parsed and bounded like any other."},
    {"is", PseudoCapability::kSubtreeQuery, PseudoSupport::kAdmitted,
     true, true,
     "native CSS (AnythingElse, cosmetic.rs:1184)",
     "Argument is a nested selector. A selector LIST inside :is() is refused "
     "because our parser refuses lists wholesale (kTrailingComma), which is "
     "narrower than the engine's ProceduralFilterWithMultipleSelectors "
     "(cosmetic.rs:749-752)."},

    // --- uBO procedural operators the vendored engine does NOT have ---
    {"nth-ancestor", PseudoCapability::kAncestorWalk,
     PseudoSupport::kRefusedNotInEngine, true, false,
     "NOT in src/filters/cosmetic.rs:1164-1185 — absent from the vendored set",
     "REFUSED: uBO-only. :upward(N) covers the same need and IS in the "
     "engine's set, so admitting a second spelling would create two paths to "
     "one behaviour."},
    {"if", PseudoCapability::kSubtreeQuery,
     PseudoSupport::kRefusedNotInEngine, true, false,
     "NOT in src/filters/cosmetic.rs:1164-1185",
     "REFUSED: uBO-only conditional; not representable by the vendored engine."},
    {"if-not", PseudoCapability::kSubtreeQuery,
     PseudoSupport::kRefusedNotInEngine, true, false,
     "NOT in src/filters/cosmetic.rs:1164-1185",
     "REFUSED: as :if."},
    {"remove", PseudoCapability::kDomRemoval, PseudoSupport::kAdmitted,
     false, false,
     "src/filters/cosmetic.rs:52-55, :357 (REMOVE_TOKEN \":remove()\")",
     "PAGE-MODIFYING: removes the matched nodes. Admitted as an ACTION (see "
     "style.h Action::kRemove), and labelled page-modifying in the event "
     "schema — an injected/removed element is not a blocked request."},
  };
  return table;
}

const PseudoInfo* LookupPseudo(const std::string& name) {
  // CanonicalizePseudoAlias signals "not an alias" with an EMPTY STRING, not a
  // null pointer — so `canon ? ... : name` took the empty branch for every
  // non-alias and looked up "", which matched nothing. Checking for empty is
  // the correct test; a bare null check made every lookup fail, which is the
  // safest possible bug but a bug all the same (no procedural pseudo-class
  // would ever be recognized, so every one would be refused as unknown).
  const char* canon = CanonicalizePseudoAlias(name);
  const std::string key = (canon && canon[0]) ? std::string(canon) : name;
  for (const PseudoInfo& info : PseudoTable()) {
    if (key == info.name) return &info;
  }
  return nullptr;
}

const char* CanonicalizePseudoAlias(const std::string& name) {
  // Exactly the three aliases at cosmetic.rs:978-980. No more: inventing an
  // alias the engine does not honour would make our compiler accept a rule the
  // engine then refuses.
  if (name == "-abp-has") return "has";
  if (name == "-abp-contains") return "has-text";
  if (name == "contains") return "has-text";
  return "";
}

bool IsAdmittedNativePseudo(const std::string& name) {
  // The engine passes ANY unknown pseudo-class through as AnythingElse
  // (cosmetic.rs:1184, and :963-966 returns Ok(AnythingElse(...)) for an
  // unrecognised name), so "the engine accepts it" proves nothing. This
  // allowlist is what makes the admitted set ours.
  static const char* const kAdmitted[] = {
    "first-child", "last-child", "only-child", "first-of-type", "last-of-type",
    "only-of-type", "nth-child", "nth-last-child", "nth-of-type",
    "nth-last-of-type", "empty", "root", "checked", "disabled", "enabled",
    "link", "visited", "hover", "focus", "target", "lang", "not", "is", "has",
  };
  for (const char* a : kAdmitted) {
    if (name == a) return true;
  }
  return false;
}

bool IsPageModifying(const PseudoInfo& info) {
  return info.capability == PseudoCapability::kDomRemoval ||
         info.capability == PseudoCapability::kMainWorld;
}

}  // namespace xr::cosmetic
