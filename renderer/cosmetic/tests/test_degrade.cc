// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/degrade — the single degrade truth table.
//
// The suite's job is to prove the table is EXHAUSTIVE and CONSISTENT, because a
// table with a hole makes the caller guess, and guessing at five call sites is
// how a page ends up half-styled. Three properties:
//
//   * every value of the condition enum has exactly one row (no hole, no dup);
//   * every reported row names a reason, and no unreported row does;
//   * the two laws the plan calls out by name — "no rules, no work" and
//     "generic set always-on" — hold in every combination of their inputs.

#include "renderer/cosmetic/core/degrade.h"

#include <set>
#include <string>

#include "harness.h"

namespace xrc = xr::cosmetic;
namespace {

// The full enum, spelled out. If a value is added to degrade.h and not here,
// the exhaustiveness assertion below fails — which is the point: the test is
// the thing that notices.
const xrc::DegradeCondition kAllConditions[] = {
    xrc::DegradeCondition::kFlagOff,
    xrc::DegradeCondition::kScriptletsOff,
    xrc::DegradeCondition::kBlobInvalid,
    xrc::DegradeCondition::kBlobMissing,
    xrc::DegradeCondition::kScopeMismatch,
    xrc::DegradeCondition::kRuleUnparsable,
    xrc::DegradeCondition::kRuleUnknownPseudo,
    xrc::DegradeCondition::kRuleTooExpensive,
    xrc::DegradeCondition::kShieldsDown,
    xrc::DegradeCondition::kEmptyRuleSet,
    xrc::DegradeCondition::kDomMutationStorm,
    xrc::DegradeCondition::kEngineUnavailable,
    xrc::DegradeCondition::kMainWorldRequired,
    xrc::DegradeCondition::kGenericSetOnly,
};
constexpr size_t kConditionCount =
    sizeof(kAllConditions) / sizeof(kAllConditions[0]);

void TestTableIsExhaustiveAndUnique() {
  const auto& table = xrc::DegradeTable();
  XR_EXPECT_MSG(table.size() >= xrc::kMinDegradeRows,
                "the degrade table shrank below its floor — a table that lost "
                "rows degrades by guessing");

  // Every enum value has exactly one row.
  for (size_t i = 0; i < kConditionCount; ++i) {
    int found = 0;
    for (const xrc::DegradeRow& row : table) {
      if (row.condition == kAllConditions[i]) ++found;
    }
    XR_EXPECT_MSG(found == 1,
                  (std::string(xrc::DegradeConditionName(kAllConditions[i])) +
                   " has " + std::to_string(found) +
                   " row(s); exactly one is required")
                      .c_str());
  }

  // And the table has no row for a condition outside the enum.
  XR_EXPECT_EQ(table.size(), kConditionCount);

  // Condition names distinct, or a report line is ambiguous.
  std::set<std::string> names;
  for (size_t i = 0; i < kConditionCount; ++i) {
    names.insert(xrc::DegradeConditionName(kAllConditions[i]));
  }
  XR_EXPECT_EQ(names.size(), kConditionCount);
}

void TestRowsAreWellFormed() {
  int reported = 0;
  for (const xrc::DegradeRow& row : xrc::DegradeTable()) {
    // Every row states what the page looks like. This is the field a reviewer
    // reads, and an empty one means the row was written without thinking about
    // the user-visible outcome.
    XR_EXPECT_MSG(row.page_effect && row.page_effect[0],
                  (std::string(xrc::DegradeConditionName(row.condition)) +
                   " has no page_effect")
                      .c_str());
    if (row.reported) {
      ++reported;
      XR_EXPECT_MSG(row.reason && row.reason[0],
                    (std::string(xrc::DegradeConditionName(row.condition)) +
                     " is reported but names no reason")
                        .c_str());
    } else {
      XR_EXPECT_MSG(row.reason == nullptr || row.reason[0] == '\0',
                    (std::string(xrc::DegradeConditionName(row.condition)) +
                     " is not reported but carries a reason")
                        .c_str());
    }
    // Outcome names must be non-empty.
    XR_EXPECT_MSG(std::string(xrc::DegradeOutcomeName(row.outcome))[0] != '\0',
                  "a row has an unnamed outcome");
  }
  // Silent degrades are how a cosmetic bug survives for months, so the table
  // must be mostly loud. A table that reported nothing would certify nothing.
  XR_EXPECT_MSG(reported >= 6,
                "fewer than 6 rows are reported — too many silent degrades");
}

void TestLookupAlwaysReturnsARow() {
  for (size_t i = 0; i < kConditionCount; ++i) {
    const xrc::DegradeRow& row = xrc::LookupDegrade(kAllConditions[i]);
    XR_EXPECT(row.condition == kAllConditions[i]);
  }
}

void TestNoRulesNoWork() {
  // THE LAW: with zero rules the emitter produces nothing and installs no
  // observer. This is what makes the default-off state byte-identical to
  // today's product, so it is asserted across every input combination rather
  // than for one convenient case.
  for (size_t rules = 0; rules <= 3; ++rules) {
    for (bool flag : {false, true}) {
      bool expect = flag && rules > 0;
      XR_EXPECT_MSG(xrc::ShouldInstallObserver(rules, flag) == expect,
                    ("ShouldInstallObserver(" + std::to_string(rules) + ", " +
                     (flag ? "on" : "off") + ") is wrong")
                        .c_str());
    }
  }
  // The table agrees with the function: an empty rule set is kNoWork.
  const xrc::DegradeRow& row =
      xrc::LookupDegrade(xrc::DegradeCondition::kEmptyRuleSet);
  XR_EXPECT(row.outcome == xrc::DegradeOutcome::kNoWork);

  // And the flag-off row is kInert, not kNoWork: the feature is present but
  // does nothing, which is a different observable state.
  const xrc::DegradeRow& off =
      xrc::LookupDegrade(xrc::DegradeCondition::kFlagOff);
  XR_EXPECT(off.outcome == xrc::DegradeOutcome::kInert);
  XR_EXPECT(!off.reported);
}

void TestGenericSetAlwaysOn() {
  // THE LAW: the generic set is not exception-able at the Shield policy level,
  // because a shields-down must not leave the page half-styled. It still needs
  // the feature flag, since with the flag off there must be no cosmetic call
  // site active at all.
  XR_EXPECT(xrc::GenericSetApplies(/*shields_down=*/true, /*flag_on=*/true));
  XR_EXPECT(xrc::GenericSetApplies(false, true));
  XR_EXPECT(!xrc::GenericSetApplies(true, false));
  XR_EXPECT(!xrc::GenericSetApplies(false, false));

  // The table agrees: shields-down yields the generic set, not an unstyled page.
  const xrc::DegradeRow& sd =
      xrc::LookupDegrade(xrc::DegradeCondition::kShieldsDown);
  XR_EXPECT(sd.outcome == xrc::DegradeOutcome::kGenericSetOnly);
}

void TestFailOpenOnCosmetic() {
  // The deliberate divergence from the network layer: an unavailable engine
  // yields an unstyled page, not a blocked one. Recorded in the contract so a
  // future reader does not "fix" it.
  const xrc::DegradeRow& row =
      xrc::LookupDegrade(xrc::DegradeCondition::kEngineUnavailable);
  XR_EXPECT(row.outcome == xrc::DegradeOutcome::kPageUnstyled);
  XR_EXPECT(row.reported);

  // One bad rule drops the WHOLE blob, so the page is never half-styled.
  const xrc::DegradeRow& bad =
      xrc::LookupDegrade(xrc::DegradeCondition::kRuleUnparsable);
  XR_EXPECT(bad.outcome == xrc::DegradeOutcome::kDropBlob);

  // The ONE condition that drops a single rule is the cost condition, because
  // there the rule is valid and the cost is a property of the page.
  const xrc::DegradeRow& costly =
      xrc::LookupDegrade(xrc::DegradeCondition::kRuleTooExpensive);
  XR_EXPECT(costly.outcome == xrc::DegradeOutcome::kDropRule);
}

}  // namespace

int main() {
  TestTableIsExhaustiveAndUnique();
  TestRowsAreWellFormed();
  TestLookupAlwaysReturnsARow();
  TestNoRulesNoWork();
  TestGenericSetAlwaysOn();
  TestFailOpenOnCosmetic();
  std::printf("  degrade: %zu conditions, %zu table rows\n", kConditionCount,
              xrc::DegradeTable().size());
  return xrtest::Report("test_degrade");
}
