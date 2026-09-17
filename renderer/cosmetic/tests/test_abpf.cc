// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/abpf — filter validation.
//
// The security content of this suite is the NEGATIVE half. A cosmetic list
// that carries `$removeparam` or `$redirect` must be refused, because honouring
// it would mean the cosmetic surface performed a network action — a privilege
// this surface does not have and must not grow by accident. Each network
// directive gets its own case, by name, so a future edit that adds one to an
// allowlist has to delete a test to get away with it.

#include "renderer/cosmetic/abpf/abpf.h"

#include <string>
#include <vector>

#include "harness.h"

namespace xrc = xr::cosmetic;
namespace {

xrc::AbpfError Parse(const std::string& line, xrc::AbpfFilter* f) {
  return xrc::ParseAbpfFilter(line, /*strict=*/true, f);
}

void TestValidCosmeticFilters() {
  xrc::AbpfFilter f;
  XR_EXPECT_EQ(Parse("##div > .ad", &f), xrc::AbpfError::kOk);
  XR_EXPECT(f.valid);
  XR_EXPECT_STREQ(f.selector, "div > .ad");
  XR_EXPECT(!f.exception);

  // An exception rule.
  XR_EXPECT_EQ(Parse("#@#.sponsor", &f), xrc::AbpfError::kOk);
  XR_EXPECT(f.exception);
  XR_EXPECT_STREQ(f.selector, ".sponsor");

  // A domain part before the anchor is legitimate ABPF and is recorded.
  XR_EXPECT_EQ(Parse("example.com,~sub.example.com##.ad", &f),
               xrc::AbpfError::kOk);
  XR_EXPECT_EQ(f.domains.size(), 2u);
  XR_EXPECT_STREQ(f.domains[0], "example.com");
  XR_EXPECT_STREQ(f.domains[1], "~sub.example.com");

  // $domain= as an option, pipe-separated.
  XR_EXPECT_EQ(Parse("##.ad$domain=a.example|~b.example", &f),
               xrc::AbpfError::kOk);
  XR_EXPECT_EQ(f.domains.size(), 2u);
  XR_EXPECT_STREQ(f.domains[1], "~b.example");

  // A selector the renderer can actually compile, including an admitted pseudo.
  XR_EXPECT_EQ(Parse("##div:has-text(Sponsored)", &f), xrc::AbpfError::kOk);
  XR_EXPECT(f.valid);
}

void TestCommentsAreSkippedNotRefused() {
  // A list header is not an error. Treating it as one would make every real
  // list fail validation on its first line.
  xrc::AbpfFilter f;
  XR_EXPECT_EQ(Parse("! this is a comment", &f), xrc::AbpfError::kComment);
  XR_EXPECT(!f.valid);
  XR_EXPECT_EQ(Parse("[Adblock Plus 2.0]", &f), xrc::AbpfError::kComment);
  XR_EXPECT_EQ(Parse("", &f), xrc::AbpfError::kEmpty);
}

void TestNetworkFiltersAreRefused() {
  // An address filter with no anchor at all.
  xrc::AbpfFilter f;
  XR_EXPECT_EQ(Parse("||ads.example.com/banner.js", &f),
               xrc::AbpfError::kNetworkFilter);
  XR_EXPECT(!f.valid);

  // An address pattern BEFORE the anchor: the `/` makes it an address, not a
  // domain list, so this is a network filter wearing a cosmetic anchor.
  XR_EXPECT_EQ(Parse("/track/*##.ad", &f), xrc::AbpfError::kNetworkFilter);
  XR_EXPECT_EQ(Parse("https://x.example##.ad", &f),
               xrc::AbpfError::kNetworkFilter);
  XR_EXPECT_EQ(Parse("||*.example##.ad", &f), xrc::AbpfError::kNetworkFilter);
}

void TestNetworkDirectivesAreRefused() {
  // THE SECURITY CASE. Each one names a request-changing feature. Honoured,
  // any of these would let a cosmetic list perform a network action.
  const char* blocked[] = {
      "$removeparam=utm_source", "$rewrite=abp-resource:blank-css",
      "$csp=img-src 'none'",     "$replace=/a/b/",
      "$redirect=blank.js",      "$redirect-rule=blank.js",
      "$header=x",               "$requestheader=x",
      "$responseheader=x",       "$urltransform=|",
  };
  for (const char* opt : blocked) {
    xrc::AbpfFilter f;
    std::string line = std::string("##.ad") + opt;
    xrc::AbpfError e = Parse(line, &f);
    XR_EXPECT_MSG(e == xrc::AbpfError::kDirectiveNotAllowed,
                  (std::string(line) + " was NOT refused as a network "
                                       "directive (got " +
                   xrc::AbpfErrorName(e) + ")")
                      .c_str());
    XR_EXPECT(!f.valid);
    // The refusal names what was refused, so an operator can tell the list
    // author which line to remove.
    XR_EXPECT_MSG(f.error_detail.size() > 0,
                  (std::string(line) + " refused without naming the directive")
                      .c_str());
  }
  // And IsNetworkDirective agrees on each one, since that is the table the
  // parser consults.
  XR_EXPECT(xrc::IsNetworkDirective("removeparam"));
  XR_EXPECT(xrc::IsNetworkDirective("RemoveParam"));  // case-insensitive
  XR_EXPECT(!xrc::IsNetworkDirective("domain"));
}

void TestGenerichideIsRefused() {
  // $generichide turns off the generic set, which is a POLICY decision the
  // browser owns. A list must not be able to make it.
  xrc::AbpfFilter f;
  XR_EXPECT_EQ(Parse("##.ad$generichide", &f),
               xrc::AbpfError::kGenerichideDirective);
  XR_EXPECT_EQ(Parse("##.ad$elemhide", &f),
               xrc::AbpfError::kGenerichideDirective);
  XR_EXPECT_EQ(Parse("##.ad$genericblock", &f),
               xrc::AbpfError::kGenerichideDirective);
}

void TestScriptletDirectivesAreRefused() {
  // $ext-… is the scriptlet family. Scriptlet EXECUTION is off this phase, so
  // the validator refuses rather than accepts-and-ignores: accepting would
  // imply the scriptlet runs. The directive is recorded so the caller can
  // report what the list tried to do.
  xrc::AbpfFilter f;
  XR_EXPECT_EQ(Parse("##.ad$ext-abp-resource:blank-css", &f),
               xrc::AbpfError::kDirectiveNotAllowed);
  XR_EXPECT_STREQ(f.error_detail, "ext-abp-resource");
  XR_EXPECT_EQ(f.directives.size(), 1u);
  XR_EXPECT(!f.valid);
}

void TestUnknownDirectiveIsRefused() {
  // Not on any list ⇒ refused. An unknown directive is a contract disagreement,
  // and guessing would make the validator permissive over time.
  xrc::AbpfFilter f;
  XR_EXPECT_EQ(Parse("##.ad$invented", &f), xrc::AbpfError::kUnknownDirective);
  XR_EXPECT_STREQ(f.error_detail, "invented");
}

void TestSelectorIsValidatedByTheRendererParser() {
  // THE INTEGRATION PROPERTY: the selector goes through core/selector.h, so a
  // filter this validator accepts is one the renderer can compile. A validator
  // with its own looser grammar would accept filters that then crash or
  // mis-parse at document-start.
  xrc::AbpfFilter f;
  // Refused by the selector parser, and the detail names why. `*.bogus(`
  // trips the disallowed-character rule before the pseudo rule because the
  // markup scan runs first (refusal order is observable; see test_selector).
  XR_EXPECT_EQ(Parse("##*.bogus(", &f), xrc::AbpfError::kSelectorRefused);
  XR_EXPECT_STREQ(f.error_detail, "disallowed-char");
  // A genuinely unknown pseudo, so the pseudo rule is exercised too.
  XR_EXPECT_EQ(Parse("##div:nonsense()", &f), xrc::AbpfError::kSelectorRefused);
  XR_EXPECT_STREQ(f.error_detail, "unknown-pseudo-class");

  // A `*:has(...)` page-wide channel is refused here too.
  XR_EXPECT_EQ(Parse("##*:has(.ad)", &f), xrc::AbpfError::kSelectorRefused);
  XR_EXPECT_STREQ(f.error_detail, "universal-with-pseudo");

  // Markup is refused — and this is the case that matters, because a filter
  // carrying markup is an injection attempt.
  XR_EXPECT_EQ(Parse("##div<!--x-->", &f), xrc::AbpfError::kSelectorRefused);
  XR_EXPECT_STREQ(f.error_detail, "markup-refused");

  // An empty selector after the anchor.
  XR_EXPECT_EQ(Parse("##", &f), xrc::AbpfError::kEmptySelector);
  XR_EXPECT_EQ(Parse("##$domain=a.example", &f), xrc::AbpfError::kEmptySelector);
}

void TestBoundsAreEnforced() {
  xrc::AbpfFilter f;
  // Over-long line.
  std::string long_line = "##.ad." + std::string(xrc::kMaxAbpfFilterLen, 'a');
  XR_EXPECT_EQ(Parse(long_line, &f), xrc::AbpfError::kTooLong);

  // Too many options.
  // NOTE: the options follow a single `$`, not one per option. An earlier
  // version of this test built ",domain=a0,domain=a1,…" with no `$` at all, so
  // the parser never saw any options and the bound was never exercised — the
  // assertion failed for the opposite reason from the one it was written to
  // check.
  std::string many = "##.ad$";
  for (size_t i = 0; i < xrc::kMaxAbpfOptions + 2; ++i) {
    if (i != 0) many += ",";
    many += "domain=a" + std::to_string(i);
  }
  XR_EXPECT_EQ(Parse(many, &f), xrc::AbpfError::kTooManyOptions);

  // A malformed option (empty name before '=').
  XR_EXPECT_EQ(Parse("##.ad$=value", &f), xrc::AbpfError::kMalformedOption);
}

void TestErrorNamesDistinct() {
  const xrc::AbpfError all[] = {
      xrc::AbpfError::kOk, xrc::AbpfError::kEmpty, xrc::AbpfError::kTooLong,
      xrc::AbpfError::kTooManyOptions, xrc::AbpfError::kComment,
      xrc::AbpfError::kNetworkFilter, xrc::AbpfError::kDirectiveNotAllowed,
      xrc::AbpfError::kUnknownDirective, xrc::AbpfError::kEmptySelector,
      xrc::AbpfError::kSelectorRefused, xrc::AbpfError::kGenerichideDirective,
      xrc::AbpfError::kMalformedOption};
  for (size_t i = 0; i < 12; ++i) {
    for (size_t j = i + 1; j < 12; ++j) {
      XR_EXPECT_MSG(std::string(xrc::AbpfErrorName(all[i])) !=
                        xrc::AbpfErrorName(all[j]),
                    "two AbpfError values share a name");
    }
  }
}

}  // namespace

int main() {
  TestValidCosmeticFilters();
  TestCommentsAreSkippedNotRefused();
  TestNetworkFiltersAreRefused();
  TestNetworkDirectivesAreRefused();
  TestGenerichideIsRefused();
  TestScriptletDirectivesAreRefused();
  TestUnknownDirectiveIsRefused();
  TestSelectorIsValidatedByTheRendererParser();
  TestBoundsAreEnforced();
  TestErrorNamesDistinct();
  return xrtest::Report("test_abpf");
}
