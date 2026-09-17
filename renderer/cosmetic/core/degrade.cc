// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// See degrade.h for why this is one table and not five call sites.

#include "renderer/cosmetic/core/degrade.h"

namespace xr::cosmetic {

const char* DegradeConditionName(DegradeCondition c) {
  switch (c) {
    case DegradeCondition::kFlagOff: return "flag-off";
    case DegradeCondition::kScriptletsOff: return "scriptlets-off";
    case DegradeCondition::kBlobInvalid: return "blob-invalid";
    case DegradeCondition::kBlobMissing: return "blob-missing";
    case DegradeCondition::kScopeMismatch: return "scope-mismatch";
    case DegradeCondition::kRuleUnparsable: return "rule-unparsable";
    case DegradeCondition::kRuleUnknownPseudo: return "rule-unknown-pseudo";
    case DegradeCondition::kRuleTooExpensive: return "rule-too-expensive";
    case DegradeCondition::kShieldsDown: return "shields-down";
    case DegradeCondition::kEmptyRuleSet: return "empty-rule-set";
    case DegradeCondition::kDomMutationStorm: return "dom-mutation-storm";
    case DegradeCondition::kEngineUnavailable: return "engine-unavailable";
    case DegradeCondition::kMainWorldRequired: return "main-world-required";
    case DegradeCondition::kGenericSetOnly: return "generic-set-only";
  }
  return "unknown";
}

const char* DegradeOutcomeName(DegradeOutcome o) {
  switch (o) {
    case DegradeOutcome::kNoWork: return "no-work";
    case DegradeOutcome::kPageUnstyled: return "page-unstyled";
    case DegradeOutcome::kGenericSetOnly: return "generic-set-only";
    case DegradeOutcome::kDropRule: return "drop-rule";
    case DegradeOutcome::kDropBlob: return "drop-blob";
    case DegradeOutcome::kInert: return "inert";
    case DegradeOutcome::kThrottle: return "throttle";
  }
  return "unknown";
}

const std::vector<DegradeRow>& DegradeTable() {
  static const std::vector<DegradeRow> table = {
    {DegradeCondition::kFlagOff, DegradeOutcome::kInert,
     "The page renders exactly as it does today: no style is emitted, no "
     "observer is installed, no call site is active.",
     false, ""},

    {DegradeCondition::kScriptletsOff, DegradeOutcome::kInert,
     "Cosmetic hiding still applies; scriptlets do nothing. The debug page "
     "reports the registry as 'inert: flag off' verbatim.",
     false, ""},

    {DegradeCondition::kBlobInvalid, DegradeOutcome::kDropBlob,
     "The whole blob is dropped and the page renders unstyled. NOT the "
     "half-styled state that dropping only the bad rule would produce.",
     true, "blob-invalid"},

    {DegradeCondition::kBlobMissing, DegradeOutcome::kGenericSetOnly,
     "The always-on generic hide set still applies; site-specific rules do "
     "not. The page renders with only the conservative junk selectors hidden.",
     true, "blob-missing"},

    {DegradeCondition::kScopeMismatch, DegradeOutcome::kDropBlob,
     "The blob is not applied at all. A blob for site A in a frame on site B "
     "would leak one identity's rule set into another, so this is a hard drop "
     "and not a partial apply.",
     true, "scope-mismatch"},

    {DegradeCondition::kRuleUnparsable, DegradeOutcome::kDropBlob,
     "One unparsable rule rejects the whole blob. The alternative — dropping "
     "the bad rule and applying the rest — would let a list author learn which "
     "payloads survived, which is a probing channel.",
     true, "rule-unparsable"},

    {DegradeCondition::kRuleUnknownPseudo, DegradeOutcome::kDropBlob,
     "As above. A pseudo-class outside the allowlist means the producer and "
     "the renderer disagree about the contract.",
     true, "unknown-pseudo-class"},

    {DegradeCondition::kRuleTooExpensive, DegradeOutcome::kDropRule,
     "THIS is the one condition that drops a single rule rather than the blob, "
     "because the rule is valid and the cost is a property of the page rather "
     "than of the rule. The remaining rules still apply.",
     true, "rule-too-expensive"},

    {DegradeCondition::kShieldsDown, DegradeOutcome::kGenericSetOnly,
     "Site-specific rules are off. The generic set STILL applies, because a "
     "shields-down must not leave the page half-styled — the user asked for "
     "less filtering, not for a broken layout.",
     false, ""},

    {DegradeCondition::kEmptyRuleSet, DegradeOutcome::kNoWork,
     "Zero rules means zero emitted style and no mutation observer installed. "
     "This is what makes the default-off state byte-identical to today's "
     "product, and it is asserted rather than assumed.",
     false, ""},

    {DegradeCondition::kDomMutationStorm, DegradeOutcome::kThrottle,
     "The page keeps rendering; cosmetic matching slows to the budget. A "
     "mutation storm must never make the page jank, so the cosmetic work "
     "yields rather than competing with layout.",
     true, "dom-mutation-storm"},

    {DegradeCondition::kEngineUnavailable, DegradeOutcome::kPageUnstyled,
     "The page renders unstyled. Fail-OPEN is correct here: refusing to hide "
     "an ad must never refuse to render a page. This is the deliberate "
     "opposite of the network layer's fail-CLOSED rule.",
     true, "engine-unavailable"},

    {DegradeCondition::kMainWorldRequired, DegradeOutcome::kDropRule,
     "A scriptlet that would need a main-world handle is refused outright — "
     "the handle is never set — and the rest of the registry still works.",
     true, "main-world-required"},

    {DegradeCondition::kGenericSetOnly, DegradeOutcome::kGenericSetOnly,
     "Only the conservative generic set applies. This is the floor: the least "
     "the feature ever does when everything else is unavailable.",
     false, ""},
  };
  return table;
}

const DegradeRow& LookupDegrade(DegradeCondition c) {
  for (const DegradeRow& row : DegradeTable()) {
    if (row.condition == c) return row;
  }
  // Unreachable when the table is exhaustive, which the test asserts. Returning
  // the safe default rather than a static empty row means a table that lost a
  // row degrades toward "render the page" and not toward "guess".
  static const DegradeRow kSafe = {
      DegradeCondition::kEngineUnavailable, DegradeOutcome::kPageUnstyled,
      "Table lookup missed; the safe default applies.", true,
      "engine-unavailable"};
  return kSafe;
}

bool ShouldInstallObserver(size_t rule_count, bool flag_on) {
  // The "no rules, no work" law. Both halves matter: with the flag off there is
  // no work regardless of the rule count, and with zero rules there is no work
  // regardless of the flag. An observer installed for nothing is a per-mutation
  // cost on every page in the browser, which is exactly what the plan's perf
  // budget forbids.
  return flag_on && rule_count > 0;
}

bool GenericSetApplies(bool shields_down, bool flag_on) {
  // The generic set is not exception-able at the Shield policy level. It still
  // requires the feature flag, because with the flag off there must be no
  // cosmetic call site active at all — that is what "off state identical to
  // today's product" means.
  return flag_on;
  (void)shields_down;
}

}  // namespace xr::cosmetic
