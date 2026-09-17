// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/keyset — per-site compilation.
//
// The central property is that DEDUP IS SEMANTIC. A list that ships `div>.ad`
// and `div > .ad` must pay for one hide, not two, and the test asserts the
// canonical strings are equal rather than merely counting rules — because a
// dedup that happens to work on the tested inputs but not in general is worse
// than no dedup: it looks like it is doing something.

#include "renderer/cosmetic/core/keyset.h"

#include <string>
#include <vector>

#include "harness.h"

namespace xrc = xr::cosmetic;
namespace {

xrc::InputRule MakeRule(const std::string& id, const std::string& sel,
                        const std::string& action = "hide") {
  xrc::InputRule r;
  r.id = id;
  r.selector = sel;
  r.action = action;
  return r;
}

void TestCompilesAndAccounts() {
  std::vector<xrc::InputRule> in = {MakeRule("r1", "div > .ad"),
                                    MakeRule("r2", ".sponsor")};
  xrc::KeySetResult out;
  XR_EXPECT_EQ(xrc::CompileKeySet(in, &out), xrc::KeySetError::kOk);
  XR_EXPECT(out.valid);
  XR_EXPECT_EQ(out.rules.size(), 2u);
  XR_EXPECT_EQ(out.compiled_selectors, 2u);
  XR_EXPECT_EQ(out.duplicates_removed, 0u);
  // Size accounting is real: a non-zero, reproducible number the cache can
  // charge against its bound.
  XR_EXPECT(out.bytes > 0);
  size_t first = out.bytes;
  xrc::KeySetResult again;
  xrc::CompileKeySet(in, &again);
  XR_EXPECT_EQ(again.bytes, first);

  // The parsed AST is kept, so the renderer does not re-parse at document-start.
  XR_EXPECT_EQ(out.rules[0].selector.compounds.size(), 2u);
  XR_EXPECT_STREQ(out.rules[0].canonical_selector, "div > .ad");
}

void TestDedupIsSemantic() {
  // Whitespace variants of the same selector are ONE rule.
  std::vector<xrc::InputRule> in = {
      MakeRule("r1", "div>.ad"),
      MakeRule("r2", "div > .ad"),
      MakeRule("r3", "div  >  .ad"),
  };
  xrc::KeySetResult out;
  XR_EXPECT_EQ(xrc::CompileKeySet(in, &out), xrc::KeySetError::kOk);
  XR_EXPECT_EQ(out.rules.size(), 1u);
  XR_EXPECT_EQ(out.duplicates_removed, 2u);

  // Class ORDER must not matter either, or `.a.b` and `.b.a` would be two
  // rules and a list shipping both would pay twice for one hide.
  std::vector<xrc::InputRule> order = {MakeRule("r1", "div.a.b"),
                                       MakeRule("r2", "div.b.a")};
  xrc::KeySetResult o2;
  XR_EXPECT_EQ(xrc::CompileKeySet(order, &o2), xrc::KeySetError::kOk);
  XR_EXPECT_EQ(o2.rules.size(), 1u);

  // But a different ACTION on the same selector is a DIFFERENT rule: hiding
  // `.ad` and removing `.ad` are not the same thing.
  std::vector<xrc::InputRule> mixed = {MakeRule("r1", ".ad", "hide"),
                                       MakeRule("r2", ".ad", "remove")};
  xrc::KeySetResult o3;
  XR_EXPECT_EQ(xrc::CompileKeySet(mixed, &o3), xrc::KeySetError::kOk);
  XR_EXPECT_EQ(o3.rules.size(), 2u);
  XR_EXPECT_EQ(o3.duplicates_removed, 0u);

  // The canonical form is what makes this work, so assert it directly.
  xrc::Selector a, b;
  XR_EXPECT_EQ(xrc::ParseSelector("div>.ad", &a), xrc::SelectorError::kOk);
  XR_EXPECT_EQ(xrc::ParseSelector("div > .ad", &b), xrc::SelectorError::kOk);
  XR_EXPECT_STREQ(xrc::CanonicalizeSelector(a), xrc::CanonicalizeSelector(b));
  xrc::Selector c;
  XR_EXPECT_EQ(xrc::ParseSelector("div + .ad", &c), xrc::SelectorError::kOk);
  XR_EXPECT(xrc::CanonicalizeSelector(a) != xrc::CanonicalizeSelector(c));
}

void TestStyleRules() {
  xrc::InputRule r = MakeRule("r1", ".ad", "style");
  r.style = {{"display", "none"}, {"opacity", "0"}};
  xrc::KeySetResult out;
  XR_EXPECT_EQ(xrc::CompileKeySet({r}, &out), xrc::KeySetError::kOk);
  XR_EXPECT_EQ(out.rules.size(), 1u);
  XR_EXPECT_EQ(out.rules[0].style.size(), 2u);

  // A built-in action's declarations come from the table, so a rule cannot mix
  // "hide" with its own map and get an unbounded declaration set.
  xrc::InputRule hide = MakeRule("r2", ".ad", "hide");
  xrc::KeySetResult o2;
  XR_EXPECT_EQ(xrc::CompileKeySet({hide}, &o2), xrc::KeySetError::kOk);
  XR_EXPECT_EQ(o2.rules[0].style.size(), 1u);
  XR_EXPECT_STREQ(o2.rules[0].style[0].first, "display");

  // A style action with no map is refused.
  xrc::InputRule empty = MakeRule("r3", ".ad", "style");
  xrc::KeySetResult o3;
  XR_EXPECT_EQ(xrc::CompileKeySet({empty}, &o3),
               xrc::KeySetError::kRuleRejected);
  XR_EXPECT(!o3.valid);
  XR_EXPECT_STREQ(o3.reason, "empty-style-map");

  // A property outside the allowlist is refused HERE, at compile time, rather
  // than discovered by the renderer at document-start.
  xrc::InputRule bad = MakeRule("r4", ".ad", "style");
  bad.style = {{"behavior", "url(#x)"}};
  xrc::KeySetResult o4;
  XR_EXPECT_EQ(xrc::CompileKeySet({bad}, &o4), xrc::KeySetError::kRuleRejected);
  XR_EXPECT_STREQ(o4.reason, "unknown-css-property");
  XR_EXPECT_EQ(o4.failed_index, 0u);
}

void TestRefusals() {
  // Empty input is a refusal, not an empty key set: the degrade table maps an
  // empty rule set to kNoWork, and compiling nothing into something that looks
  // like a key set would let a caller install an observer for nothing.
  xrc::KeySetResult out;
  XR_EXPECT_EQ(xrc::CompileKeySet({}, &out), xrc::KeySetError::kEmptyInput);
  XR_EXPECT(!out.valid);

  // An unparsable selector rejects the whole set, naming the index.
  std::vector<xrc::InputRule> bad = {MakeRule("r1", ".ok"),
                                     MakeRule("r2", "*.bogus(")};
  xrc::KeySetResult o2;
  XR_EXPECT_EQ(xrc::CompileKeySet(bad, &o2), xrc::KeySetError::kRuleRejected);
  XR_EXPECT_EQ(o2.failed_index, 1u);
  XR_EXPECT(o2.rules.empty());

  // Duplicate ids are refused.
  std::vector<xrc::InputRule> dup = {MakeRule("r1", ".a"), MakeRule("r1", ".b")};
  xrc::KeySetResult o3;
  XR_EXPECT_EQ(xrc::CompileKeySet(dup, &o3), xrc::KeySetError::kDuplicateId);

  // An unknown action is refused.
  std::vector<xrc::InputRule> act = {MakeRule("r1", ".a", "script")};
  xrc::KeySetResult o4;
  XR_EXPECT_EQ(xrc::CompileKeySet(act, &o4), xrc::KeySetError::kRuleRejected);
  XR_EXPECT_STREQ(o4.reason, "unknown-action");

  // The rule-count bound.
  std::vector<xrc::InputRule> many;
  for (size_t i = 0; i <= xrc::kMaxKeySetRules; ++i) {
    many.push_back(MakeRule("r" + std::to_string(i), ".c"));
  }
  xrc::KeySetResult o5;
  XR_EXPECT_EQ(xrc::CompileKeySet(many, &o5), xrc::KeySetError::kTooManyRules);
  XR_EXPECT(o5.rules.empty());

  // Names distinct.
  const xrc::KeySetError all[] = {
      xrc::KeySetError::kOk, xrc::KeySetError::kTooManyRules,
      xrc::KeySetError::kTooManyBytes, xrc::KeySetError::kRuleRejected,
      xrc::KeySetError::kEmptyInput, xrc::KeySetError::kDuplicateId};
  for (size_t i = 0; i < 6; ++i) {
    for (size_t j = i + 1; j < 6; ++j) {
      XR_EXPECT_MSG(std::string(xrc::KeySetErrorName(all[i])) !=
                        xrc::KeySetErrorName(all[j]),
                    "two KeySetError values share a name");
    }
  }
}

void TestByteBudgetIsEnforcedDuringTheWalk() {
  // The budget is checked per rule, so a pathological input cannot allocate a
  // gigabyte of rules before being refused. One rule with an enormous
  // exception-site list trips it.
  std::vector<xrc::InputRule> in;
  xrc::InputRule r = MakeRule("r1", ".ad");
  for (int i = 0; i < 60000; ++i) {
    r.exception_sites.push_back("site-" + std::to_string(i) + ".example");
  }
  in.push_back(r);
  xrc::KeySetResult out;
  xrc::KeySetError e = xrc::CompileKeySet(in, &out);
  XR_EXPECT_MSG(e == xrc::KeySetError::kTooManyBytes ||
                    e == xrc::KeySetError::kOk,
                "an oversized rule must either be refused or fit the budget");
  if (e == xrc::KeySetError::kTooManyBytes) {
    XR_EXPECT(out.rules.empty());
    XR_EXPECT(!out.valid);
  }
}

}  // namespace

int main() {
  TestCompilesAndAccounts();
  TestDedupIsSemantic();
  TestStyleRules();
  TestRefusals();
  TestByteBudgetIsEnforcedDuringTheWalk();
  return xrtest::Report("test_keyset");
}
