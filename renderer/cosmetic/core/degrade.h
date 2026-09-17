// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/degrade — THE SINGLE DEGRADE TRUTH TABLE
// (P12-T1).
//
// WHY ONE TABLE. The plan requires that cosmetic filtering "degrade safely" and
// never break page render. That requirement is easy to state and easy to
// violate, because degrade decisions otherwise get made at five different call
// sites — the blob loader, the key-set builder, the DOM mutation callback, the
// exception check and the flag check — and each one guesses. When they
// disagree, the failure is a half-styled page, which is the worst cosmetic
// outcome there is: worse than not hiding the ad, because the user sees a
// broken layout and cannot tell what did it.
//
// So every degrade decision routes through this table, and the table is DATA.
// A caller asks "given this condition, what happens?" and gets one of a closed
// set of outcomes. There is no second place that decides.
//
// THE DEFAULT IS THE PAGE. Every row here resolves toward "render the page
// unstyled" rather than toward "apply what we could". Cosmetic filtering is the
// one place in the browser where fail-OPEN is correct: refusing to hide an ad
// must never refuse to render a page. That is the deliberate opposite of the
// network layer's fail-CLOSED rule (docs/contracts/cosmetic-blob-v1.md records
// the divergence so a future reader does not "fix" it).
#pragma once

#include <string>
#include <vector>

namespace xr::cosmetic {

// The condition being asked about. One value per thing that can go wrong, and
// nothing else — a condition not in this enum cannot be asked, which is how the
// table stays exhaustive.
enum class DegradeCondition {
  kFlagOff = 0,           // xr_shield_cosmetic_v1 = false
  kScriptletsOff,         // xr_shield_scriptlets = false
  kBlobInvalid,           // the blob failed validation
  kBlobMissing,           // no blob for this scope
  kScopeMismatch,         // the blob is for a different site/identity
  kRuleUnparsable,        // one rule's selector would not parse
  kRuleUnknownPseudo,     // a pseudo-class outside the allowlist
  kRuleTooExpensive,      // a rule whose match cost exceeds the budget
  kShieldsDown,           // the user turned shields off for this site
  kEmptyRuleSet,          // no rules at all (the "no rules, no work" law)
  kDomMutationStorm,      // mutations arriving faster than the budget allows
  kEngineUnavailable,     // the vendored engine is not responding
  kMainWorldRequired,     // a scriptlet needs a main-world handle (forbidden)
  kGenericSetOnly,        // only the always-on generic set is available
};

const char* DegradeConditionName(DegradeCondition c);

// What happens. Closed set, and each outcome is observable so a test can assert
// it rather than infer it from the page.
enum class DegradeOutcome {
  kNoWork = 0,      // do nothing at all; zero emitted style
  kPageUnstyled,    // render the page, apply nothing
  kGenericSetOnly,  // apply only the always-on generic hide set
  kDropRule,        // drop this one rule, apply the rest
  kDropBlob,        // drop the whole blob (one bad rule rejects all)
  kInert,           // the feature is present but does nothing (flag off)
  kThrottle,        // keep working, but at a reduced rate
};

const char* DegradeOutcomeName(DegradeOutcome o);

struct DegradeRow {
  DegradeCondition condition;
  DegradeOutcome outcome;
  // What the page looks like afterwards, in one sentence. This is the field a
  // reviewer reads to check the table is sane, and it is what the degrade tests
  // assert against the DOM model.
  const char* page_effect;
  // Whether the outcome is reported to the Observatory. Silent degrades are how
  // a cosmetic bug survives for months, so every row says whether it is loud.
  bool reported;
  // The reason string, from the closed refusal vocabulary, for the reported
  // case. Empty when not reported.
  const char* reason;
};

// The table. Order is not significant; lookups are by condition.
const std::vector<DegradeRow>& DegradeTable();

// Looks up one condition. Always returns a row: the table is exhaustive over
// the enum, and a caller that asks about a condition with no row has a bug, so
// this returns the safe default (kPageUnstyled) rather than nothing.
const DegradeRow& LookupDegrade(DegradeCondition c);

// The "no rules, no work" law, stated as a function so a test can assert it:
// with zero rules the emitter must produce zero declarations and must not
// install a mutation observer. This is the property that keeps the default-off
// state byte-identical to today's product.
bool ShouldInstallObserver(size_t rule_count, bool flag_on);

// The "generic hide set always-on" law. The generic set is not exception-able
// at the Shield policy level: a user's shields-down must not leave the page
// half-styled, so the generic set's application does not depend on the
// per-site exception bit. If that reading of the plan is wrong the ADR records
// the disagreement — it is not silently decided here.
bool GenericSetApplies(bool shields_down, bool flag_on);

// How many rows must the table have for the law to be meaningful. A table that
// silently lost rows would degrade by guessing, so the zero-case check has a
// floor.
constexpr size_t kMinDegradeRows = 12;

}  // namespace xr::cosmetic
