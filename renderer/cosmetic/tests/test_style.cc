// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/style — the emitted declaration form.
//
// The point of this suite is the allowlist's NEGATIVE half. A property
// allowlist that admits everything is not an allowlist, so the dangerous
// properties are asserted absent by name: `behavior` and `-moz-binding` (legacy
// code execution), `content` (generates content, so not a hide at all),
// `position`/`z-index` (a rule that can restack the page can hide the user's
// content instead of the ad's — a UI-spoofing channel), and every URL-taking
// property (a fetch side channel).

#include "renderer/cosmetic/core/style.h"

#include <string>

#include "harness.h"

namespace xrc = xr::cosmetic;
namespace {

int g_admitted = 0;
int g_refused = 0;

void TestActions() {
  xrc::Action a;
  XR_EXPECT(xrc::ParseAction("hide", &a) && a == xrc::Action::kHide);
  XR_EXPECT(xrc::ParseAction("collapse", &a) && a == xrc::Action::kCollapse);
  XR_EXPECT(xrc::ParseAction("visibility", &a) &&
            a == xrc::Action::kVisibilityOff);
  XR_EXPECT(xrc::ParseAction("remove", &a) && a == xrc::Action::kRemove);
  XR_EXPECT(xrc::ParseAction("style", &a) && a == xrc::Action::kStyle);
  g_admitted += 5;

  // Exact match only: case variants are a producer bug and must surface.
  XR_EXPECT(!xrc::ParseAction("Hide", &a));
  XR_EXPECT(!xrc::ParseAction("REMOVE", &a));
  XR_EXPECT(!xrc::ParseAction("", &a));
  XR_EXPECT(!xrc::ParseAction("script", &a));
  XR_EXPECT(!xrc::ParseAction("hide ", &a));
  g_refused += 5;
}

void TestPageModifying() {
  // Only removal mutates the page. The Observatory must label these honestly:
  // an injected/removed element is not a blocked request.
  XR_EXPECT(xrc::ActionIsPageModifying(xrc::Action::kRemove));
  XR_EXPECT(!xrc::ActionIsPageModifying(xrc::Action::kHide));
  XR_EXPECT(!xrc::ActionIsPageModifying(xrc::Action::kCollapse));
  XR_EXPECT(!xrc::ActionIsPageModifying(xrc::Action::kVisibilityOff));
  XR_EXPECT(!xrc::ActionIsPageModifying(xrc::Action::kStyle));

  // Names must be distinct, or an event row becomes ambiguous.
  const xrc::Action all[] = {xrc::Action::kHide, xrc::Action::kCollapse,
                             xrc::Action::kVisibilityOff, xrc::Action::kRemove,
                             xrc::Action::kStyle};
  for (size_t i = 0; i < 5; ++i) {
    for (size_t j = i + 1; j < 5; ++j) {
      XR_EXPECT_MSG(std::string(xrc::ActionName(all[i])) !=
                        xrc::ActionName(all[j]),
                    "two Action values share a name");
    }
  }
}

void TestDeclarations() {
  auto d = xrc::DeclarationsForAction(xrc::Action::kHide);
  XR_EXPECT_EQ(d.size(), 1u);
  if (!d.empty()) {
    XR_EXPECT_STREQ(d[0].property, "display");
    XR_EXPECT_STREQ(d[0].value, "none");
  }

  d = xrc::DeclarationsForAction(xrc::Action::kVisibilityOff);
  XR_EXPECT_EQ(d.size(), 1u);
  if (!d.empty()) {
    XR_EXPECT_STREQ(d[0].property, "visibility");
    XR_EXPECT_STREQ(d[0].value, "hidden");
  }

  // Removal has NO declaration: it is a DOM operation, and pretending otherwise
  // would put node removal on the CSSOM path.
  XR_EXPECT(xrc::DeclarationsForAction(xrc::Action::kRemove).empty());
  // A style action carries its own map.
  XR_EXPECT(xrc::DeclarationsForAction(xrc::Action::kStyle).empty());

  // Every built-in declaration must itself pass the property allowlist, or the
  // core would emit something its own validator refuses.
  const xrc::Action builtins[] = {xrc::Action::kHide, xrc::Action::kCollapse,
                                  xrc::Action::kVisibilityOff};
  for (xrc::Action a : builtins) {
    for (const xrc::Declaration& dec : xrc::DeclarationsForAction(a)) {
      XR_EXPECT_MSG(xrc::ValidateDeclaration(dec.property, dec.value) ==
                        xrc::StyleError::kOk,
                    (std::string("built-in declaration ") + dec.property +
                     " is refused by its own validator")
                        .c_str());
    }
  }
}

void TestPropertyAllowlist() {
  const char* ok[] = {"display", "visibility", "opacity", "height",
                      "max-height", "min-height", "width", "max-width",
                      "min-width", "overflow", "pointer-events", "clip",
                      "clip-path"};
  for (const char* p : ok) {
    XR_EXPECT_MSG(xrc::IsAdmittedProperty(p), (std::string(p) + " should be admitted").c_str());
    ++g_admitted;
  }

  // THE NEGATIVE HALF. Each of these is refused for a stated reason, and the
  // reason is why the allowlist exists rather than a denylist.
  const char* banned[] = {
      "behavior",          // IE code execution
      "-moz-binding",      // legacy Firefox code execution
      "content",           // generates content: not a hide operation
      "position",          // restacking can hide the USER's content
      "z-index",           // as above
      "background",        // takes a URL
      "background-image",  // takes a URL
      "list-style",        // takes a URL
      "border-image",      // takes a URL
      "cursor",            // takes a URL
      "mask",              // takes a URL
      "color",             // not a hide operation at all
      "font-size",         // not a hide operation at all
      "",                  // empty
      "DISPLAY",           // case-sensitive: a producer bug must surface
  };
  for (const char* p : banned) {
    XR_EXPECT_MSG(!xrc::IsAdmittedProperty(p),
                  (std::string(p) + " must NOT be admitted").c_str());
    ++g_refused;
  }
}

void TestValueValidation() {
  XR_EXPECT(xrc::ValidateDeclaration("display", "none") ==
            xrc::StyleError::kOk);
  XR_EXPECT(xrc::ValidateDeclaration("opacity", "0") ==
            xrc::StyleError::kOk);

  // `!important` is REFUSED, not stripped. Stripping would silently change the
  // author's intent and hide that a list tried to escalate.
  XR_EXPECT(xrc::ValidateDeclaration("display", "none !important") ==
            xrc::StyleError::kBangImportant);
  ++g_refused;

  // A value that fetches is a network side channel.
  XR_EXPECT(xrc::ValidateDeclaration("opacity", "url(http://x/y)") ==
            xrc::StyleError::kUrlFunction);
  XR_EXPECT(xrc::ValidateDeclaration("opacity", "JAVASCRIPT:alert(1)") ==
            xrc::StyleError::kUrlFunction);
  XR_EXPECT(xrc::ValidateDeclaration("opacity", "expression(alert(1))") ==
            xrc::StyleError::kUrlFunction);
  XR_EXPECT(xrc::ValidateDeclaration("opacity", "image-set(a.png)") ==
            xrc::StyleError::kUrlFunction);
  g_refused += 4;

  XR_EXPECT(xrc::ValidateDeclaration("display", "") ==
            xrc::StyleError::kEmptyValue);
  XR_EXPECT(xrc::ValidateDeclaration("display",
                                     std::string(300, 'x')) ==
            xrc::StyleError::kValueTooLong);
  XR_EXPECT(xrc::ValidateDeclaration("display", std::string("a\bb")) ==
            xrc::StyleError::kNonAsciiControl);
  XR_EXPECT(xrc::ValidateDeclaration("bogus", "none") ==
            xrc::StyleError::kUnknownProperty);
  g_refused += 4;

  // Names distinct.
  const xrc::StyleError all[] = {
      xrc::StyleError::kOk, xrc::StyleError::kEmptyMap,
      xrc::StyleError::kTooManyDeclarations, xrc::StyleError::kUnknownProperty,
      xrc::StyleError::kEmptyValue, xrc::StyleError::kValueTooLong,
      xrc::StyleError::kBangImportant, xrc::StyleError::kUrlFunction,
      xrc::StyleError::kNonAsciiControl};
  for (size_t i = 0; i < 9; ++i) {
    for (size_t j = i + 1; j < 9; ++j) {
      XR_EXPECT_MSG(std::string(xrc::StyleErrorName(all[i])) !=
                        xrc::StyleErrorName(all[j]),
                    "two StyleError values share a name");
    }
  }
}

}  // namespace

int main() {
  TestActions();
  TestPageModifying();
  TestDeclarations();
  TestPropertyAllowlist();
  TestValueValidation();
  XR_EXPECT_MSG(g_admitted >= 13, "the allowlist admitted too little to be real");
  XR_EXPECT_MSG(g_refused >= 15,
                "the suite refused too few properties — the allowlist's "
                "negative half is probably unexercised");
  std::printf("  style: %d admitted, %d refused\n", g_admitted, g_refused);
  return xrtest::Report("test_style");
}
