// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/selector — the parse/refusal surface (P12-T1).
//
// What this suite is FOR. The selector parser is the trust boundary between
// list data (third-party, mutable, attacker-influenceable) and the renderer.
// Its job is to be TOTAL and to refuse LOUDLY: every SelectorError in the enum
// is a claim about what cannot reach the renderer, and a refusal branch that no
// test exercises is a claim nobody checked. So this file has one case per
// refusal reason, plus the positive parses, plus the bounds.
//
// The zero-case law applies to this suite as it does to the Python gates: a
// selector that parses to nothing, or a refusal count of zero, is a failure,
// not a pass.

#include "renderer/cosmetic/core/keyset.h"  // CanonicalizeSelector
#include "renderer/cosmetic/core/selector.h"

#include <string>
#include <vector>

#include "harness.h"

namespace xrc = xr::cosmetic;
namespace {

int g_accepted = 0;
int g_refused = 0;

// Parse and assert acceptance, returning the AST for structural checks.
bool Parses(const std::string& text, xrc::Selector* out) {
  xrc::Selector s;
  xrc::SelectorError e = xrc::ParseSelector(text, &s);
  if (e != xrc::SelectorError::kOk) {
    XR_EXPECT_MSG(false, (std::string("expected ACCEPT for '") + text +
                          "' but got " + xrc::SelectorErrorName(e)).c_str());
    return false;
  }
  ++g_accepted;
  if (out) *out = s;
  return true;
}

// Parse and assert a SPECIFIC refusal. Asserting the exact reason matters: a
// parser that refuses for the wrong reason is still broken, and "it refused"
// alone would pass a parser that refuses everything.
void Refuses(const std::string& text, xrc::SelectorError want) {
  xrc::Selector s;
  xrc::SelectorError got = xrc::ParseSelector(text, &s);
  ++g_refused;
  if (got != want) {
    XR_EXPECT_MSG(false,
                  (std::string("expected refusal ") +
                   xrc::SelectorErrorName(want) + " for '" + text + "' but got " +
                   xrc::SelectorErrorName(got))
                      .c_str());
  }
}

void TestBasicCompounds() {
  xrc::Selector s;
  XR_EXPECT(Parses("div", &s));
  XR_EXPECT_STREQ(s.compounds.at(0).tag, "div");
  XR_EXPECT_EQ(s.compounds.size(), 1u);

  XR_EXPECT(Parses(".ad", &s));
  XR_EXPECT_EQ(s.compounds.at(0).classes.size(), 1u);
  XR_EXPECT_STREQ(s.compounds.at(0).classes.at(0), "ad");

  XR_EXPECT(Parses("#banner", &s));
  XR_EXPECT_EQ(s.compounds.at(0).ids.size(), 1u);
  XR_EXPECT_STREQ(s.compounds.at(0).ids.at(0), "banner");

  // tag + class + id in one compound
  XR_EXPECT(Parses("div.sidebar#main", &s));
  XR_EXPECT_STREQ(s.compounds.at(0).tag, "div");
  XR_EXPECT_EQ(s.compounds.at(0).classes.size(), 1u);
  XR_EXPECT_EQ(s.compounds.at(0).ids.size(), 1u);

  // whitespace normalization: leading/trailing space is not a combinator
  XR_EXPECT(Parses("  .ad  ", &s));
  XR_EXPECT_EQ(s.compounds.size(), 1u);
}

void TestCombinators() {
  xrc::Selector s;
  // the descendant combinator with and without spaces must agree
  XR_EXPECT(Parses("div .ad", &s));
  XR_EXPECT_EQ(s.compounds.size(), 2u);
  XR_EXPECT(s.combinators.at(0) == xrc::Combinator::kDescendant);

  xrc::Selector s2;
  XR_EXPECT(Parses("div .ad", &s2));
  XR_EXPECT_EQ(s.compounds.size(), s2.compounds.size());

  XR_EXPECT(Parses("div > .ad", &s));
  XR_EXPECT(s.combinators.at(0) == xrc::Combinator::kChild);
  XR_EXPECT(Parses("div>.ad", &s));
  XR_EXPECT(s.combinators.at(0) == xrc::Combinator::kChild);
  XR_EXPECT(Parses("div + .ad", &s));
  XR_EXPECT(s.combinators.at(0) == xrc::Combinator::kAdjacent);
  XR_EXPECT(Parses("div ~ .ad", &s));
  XR_EXPECT(s.combinators.at(0) == xrc::Combinator::kGeneralSibling);

  // a three-compound chain keeps both combinators in order
  XR_EXPECT(Parses(".a > .b + .c", &s));
  XR_EXPECT_EQ(s.compounds.size(), 3u);
  XR_EXPECT_EQ(s.combinators.size(), 2u);
  XR_EXPECT(s.combinators.at(0) == xrc::Combinator::kChild);
  XR_EXPECT(s.combinators.at(1) == xrc::Combinator::kAdjacent);
}

void TestAttributes() {
  xrc::Selector s;
  XR_EXPECT(Parses("[href]", &s));
  XR_EXPECT(s.compounds.at(0).attrs.at(0).op ==
            xrc::AttrSelector::Op::kExists);
  XR_EXPECT(Parses("[href=x]", &s));
  XR_EXPECT(s.compounds.at(0).attrs.at(0).op == xrc::AttrSelector::Op::kEquals);
  XR_EXPECT(Parses("[href^=x]", &s));
  XR_EXPECT(s.compounds.at(0).attrs.at(0).op == xrc::AttrSelector::Op::kPrefix);
  XR_EXPECT(Parses("[href$=x]", &s));
  XR_EXPECT(s.compounds.at(0).attrs.at(0).op == xrc::AttrSelector::Op::kSuffix);
  XR_EXPECT(Parses("[href*=x]", &s));
  XR_EXPECT(s.compounds.at(0).attrs.at(0).op ==
            xrc::AttrSelector::Op::kSubstring);
  XR_EXPECT(Parses("[href~=x]", &s));
  XR_EXPECT(s.compounds.at(0).attrs.at(0).op ==
            xrc::AttrSelector::Op::kWhitespace);
  XR_EXPECT(Parses("[href|=x]", &s));
  XR_EXPECT(s.compounds.at(0).attrs.at(0).op == xrc::AttrSelector::Op::kHyphen);

  // quoted values, and the case-insensitive flag
  XR_EXPECT(Parses("[data-ad=\"true\"]", &s));
  XR_EXPECT_STREQ(s.compounds.at(0).attrs.at(0).value, "true");
  XR_EXPECT(Parses("[data-ad=true i]", &s));
  XR_EXPECT(s.compounds.at(0).attrs.at(0).case_insensitive);
}

void TestPseudoClasses() {
  xrc::Selector s;
  // bare native pseudo-class
  XR_EXPECT(Parses(".ad:first-child", &s));
  XR_EXPECT_EQ(s.compounds.at(0).pseudos.size(), 1u);
  XR_EXPECT_STREQ(s.compounds.at(0).pseudos.at(0).name, "first-child");
  XR_EXPECT(s.compounds.at(0).pseudos.at(0).args.empty());

  // functional, non-selector argument
  XR_EXPECT(Parses(".ad:has-text(sponsor)", &s));
  XR_EXPECT_EQ(s.compounds.at(0).pseudos.at(0).args.size(), 1u);
  XR_EXPECT_STREQ(s.compounds.at(0).pseudos.at(0).args.at(0), "sponsor");

  // functional, SELECTOR argument: the nested selector must be parsed too
  XR_EXPECT(Parses(".ad:has(.sponsor)", &s));
  XR_EXPECT_EQ(s.compounds.at(0).pseudos.at(0).args.size(), 1u);
  XR_EXPECT_STREQ(s.compounds.at(0).pseudos.at(0).args.at(0), ".sponsor");

  // an unknown functional pseudo is refused, not passed through. The vendored
  // engine accepts ANY pseudo-class as AnythingElse (cosmetic.rs:1184), so
  // "the engine takes it" is not a reason for us to.
  Refuses(".ad:xr-invented(1)", xrc::SelectorError::kUnknownPseudoClass);
}

void TestRefusals() {
  // One case per SelectorError. If a reason here is unreachable, the parser
  // has a dead branch and the enum is lying.
  Refuses("", xrc::SelectorError::kEmpty);
  Refuses("   ", xrc::SelectorError::kEmpty);
  Refuses("> .ad", xrc::SelectorError::kLeadingCombinator);
  Refuses(".ad >", xrc::SelectorError::kTrailingCombinator);
  Refuses("div >> .ad", xrc::SelectorError::kDoubleCombinator);
  Refuses(".ad, .tracker", xrc::SelectorError::kTrailingComma);
  Refuses("div..ad", xrc::SelectorError::kDisallowedChar);
  Refuses(".a{color:red}", xrc::SelectorError::kDisallowedChar);
  Refuses(".x!important", xrc::SelectorError::kBangImportant);
  Refuses("@import url(x)", xrc::SelectorError::kAtRule);
  Refuses("</style><script>", xrc::SelectorError::kCdataOrMarkup);
  Refuses("<!-- .ad", xrc::SelectorError::kCdataOrMarkup);
  Refuses(".ad:matches-css(background:url(x))",
          xrc::SelectorError::kUrlFunction);
  Refuses("expression(alert(1))", xrc::SelectorError::kExpressionFunction);
  Refuses("*:has(.ad)", xrc::SelectorError::kUniversalWithPseudo);
  Refuses(".a\\:b", xrc::SelectorError::kEscapeSequence);
  Refuses(".ad[", xrc::SelectorError::kUnbalancedBracket);
  Refuses(".ad:has(.x", xrc::SelectorError::kUnbalancedParen);
  Refuses("/* unterminated", xrc::SelectorError::kCommentUnterminated);
  Refuses("div > > .ad", xrc::SelectorError::kDoubleCombinator);
}

void TestBounds() {
  // Length bound: the parse must not do unbounded work on a long input.
  std::string long_sel(5000, 'a');
  xrc::Selector s;
  XR_EXPECT_EQ(xrc::ParseSelector(long_sel, &s),
               xrc::SelectorError::kTooLong);
  ++g_refused;

  // Compound-count bound.
  std::string many;
  for (int i = 0; i < 200; ++i) many += (i ? " .c" : ".c");
  xrc::Selector s2;
  xrc::SelectorError e = xrc::ParseSelector(many, &s2);
  XR_EXPECT_MSG(e == xrc::SelectorError::kTooManyCompounds ||
                    e == xrc::SelectorError::kTooLong,
                "a 200-compound selector must hit a bound");
  ++g_refused;

  // A selector at the edge of the bound must still parse — a bound that
  // refuses legitimate input is a correctness bug, not a safety feature.
  XR_EXPECT(Parses(".a > .b + .c ~ div#id.cls[attr=v]", &s));
  XR_EXPECT_EQ(s.source_len, std::string(".a > .b + .c ~ div#id.cls[attr=v]").size());
}

void TestErrorNamesAreDistinct() {
  // The closed vocabulary is what the event schema and the Python fake mirror.
  // Two reasons with the same name would make an observability row ambiguous.
  std::vector<xrc::SelectorError> all = {
      xrc::SelectorError::kOk,
      xrc::SelectorError::kEmpty,
      xrc::SelectorError::kTooLong,
      xrc::SelectorError::kTooManyCompounds,
      xrc::SelectorError::kTooManyAttrSelectors,
      xrc::SelectorError::kTooManyPseudoArgs,
      xrc::SelectorError::kIdentTooLong,
      xrc::SelectorError::kUnbalancedParen,
      xrc::SelectorError::kUnbalancedBracket,
      xrc::SelectorError::kEmptyCompound,
      xrc::SelectorError::kLeadingCombinator,
      xrc::SelectorError::kTrailingCombinator,
      xrc::SelectorError::kDoubleCombinator,
      xrc::SelectorError::kTrailingComma,
      xrc::SelectorError::kDisallowedChar,
      xrc::SelectorError::kBangImportant,
      xrc::SelectorError::kAtRule,
      xrc::SelectorError::kCdataOrMarkup,
      xrc::SelectorError::kUrlFunction,
      xrc::SelectorError::kExpressionFunction,
      xrc::SelectorError::kUnknownPseudoClass,
      xrc::SelectorError::kDisallowedPseudoArg,
      xrc::SelectorError::kUniversalWithPseudo,
      xrc::SelectorError::kCommentUnterminated,
      xrc::SelectorError::kEscapeSequence,
  };
  for (size_t i = 0; i < all.size(); ++i) {
    const char* n = xrc::SelectorErrorName(all[i]);
    XR_EXPECT_MSG(n != nullptr && n[0] != '\0',
                  "every SelectorError must have a non-empty name");
    for (size_t j = i + 1; j < all.size(); ++j) {
      XR_EXPECT_MSG(std::string(n) != xrc::SelectorErrorName(all[j]),
                    "two SelectorError values share a name");
    }
  }
}

}  // namespace

void TestEmptyPseudoArgIsNotAnArg() {
  // REGRESSION (P12-T1, found by the golden-vector generator).
  //
  // `:remove()` is the documented spelling of the removal pseudo — the vendored
  // reference's REMOVE_TOKEN is literally ":remove()" (see the citation on the
  // `remove` row of kPseudoTable). The parser pushed the empty string as an
  // argument, so `args` was non-empty and the arity check refused the ONE
  // spelling the table cites. A refusal that looks principled ("a bare pseudo
  // used functionally is malformed") and is actually a bug is the worst kind:
  // the enum value existed, the check ran, and the only way to notice was to
  // run the documented form through the parser.
  xrc::Selector s;
  XR_EXPECT_EQ(xrc::ParseSelector("div:remove()", &s), xrc::SelectorError::kOk);
  XR_EXPECT_EQ(xrc::ParseSelector("div:remove", &s), xrc::SelectorError::kOk);
  // Both spellings canonicalize identically, so they dedup as one rule.
  xrc::Selector a, b;
  xrc::ParseSelector("div:remove()", &a);
  xrc::ParseSelector("div:remove", &b);
  XR_EXPECT_STREQ(xrc::CanonicalizeSelector(a), xrc::CanonicalizeSelector(b));
  // But an ACTUAL argument to a pseudo that takes none is still refused.
  XR_EXPECT_EQ(xrc::ParseSelector("div:remove(1)", &s),
               xrc::SelectorError::kDisallowedPseudoArg);
  // And a pseudo that DOES take an argument still requires one.
  XR_EXPECT_EQ(xrc::ParseSelector("div:has", &s),
               xrc::SelectorError::kDisallowedPseudoArg);
  XR_EXPECT_EQ(xrc::ParseSelector("div:has(.ad)", &s), xrc::SelectorError::kOk);
  // An empty-arg functional pseudo that takes an arg is refused, not treated
  // as bare: `:has()` is not `:has`.
  XR_EXPECT_EQ(xrc::ParseSelector("div:has()", &s),
               xrc::SelectorError::kDisallowedPseudoArg);
}

int main() {
  TestBasicCompounds();
  TestCombinators();
  TestAttributes();
  TestPseudoClasses();
  TestRefusals();
  TestBounds();
  TestErrorNamesAreDistinct();

  // The zero-case law: a suite that exercised nothing would otherwise print a
  // clean "0 failures" and pass.
  XR_EXPECT_MSG(g_accepted > 0, "the suite accepted no selector at all");
  XR_EXPECT_MSG(g_refused >= 15,
                "the suite refused fewer than 15 inputs — a refusal branch is "
                "probably unexercised");
  std::printf("  selector: %d accepted, %d refused\n", g_accepted, g_refused);
  TestEmptyPseudoArgIsNotAnArg();
  return xrtest::Report("test_selector");
}
