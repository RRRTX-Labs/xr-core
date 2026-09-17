// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// See style.h for the law this module enforces.

#include "renderer/cosmetic/core/style.h"

#include <cctype>
#include <cstring>

namespace xr::cosmetic {
namespace {

bool ContainsNoCase(const std::string& haystack, const std::string& needle) {
  if (needle.empty() || haystack.size() < needle.size()) return false;
  for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    bool ok = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
          std::tolower(static_cast<unsigned char>(needle[j]))) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

}  // namespace

const char* ActionName(Action a) {
  switch (a) {
    case Action::kHide: return "hide";
    case Action::kCollapse: return "collapse";
    case Action::kVisibilityOff: return "visibility";
    case Action::kRemove: return "remove";
    case Action::kStyle: return "style";
  }
  return "unknown";
}

bool ParseAction(const std::string& text, Action* out) {
  // Exact match only. A case-insensitive match would accept "Remove" and
  // "REMOVE" from a list, which is a producer bug that should surface as a
  // refusal rather than be papered over here.
  if (text == "hide") { *out = Action::kHide; return true; }
  if (text == "collapse") { *out = Action::kCollapse; return true; }
  if (text == "visibility") { *out = Action::kVisibilityOff; return true; }
  if (text == "remove") { *out = Action::kRemove; return true; }
  if (text == "style") { *out = Action::kStyle; return true; }
  return false;
}

bool ActionIsPageModifying(Action a) {
  // Only removal mutates the page. Hiding, collapsing and blanking are all
  // style operations on a node that stays in the DOM, so a user inspecting the
  // page still sees the same structure.
  return a == Action::kRemove;
}

std::vector<Declaration> DeclarationsForAction(Action a) {
  switch (a) {
    case Action::kHide:
      return {{"display", "none"}};
    case Action::kCollapse:
      // uBO's "collapse": out of layout as well as out of paint, so the page
      // does not keep a hole where the ad was.
      return {{"display", "none"}};
    case Action::kVisibilityOff:
      // Keeps layout deliberately: used where collapsing would shift content
      // the user is reading.
      return {{"visibility", "hidden"}};
    case Action::kRemove:
      // No declaration. Removal is a DOM operation, not a style one, and
      // pretending otherwise would put node removal on the CSSOM path.
      return {};
    case Action::kStyle:
      // The rule carries its own map; see ValidateDeclaration.
      return {};
  }
  return {};
}

const char* StyleErrorName(StyleError e) {
  switch (e) {
    case StyleError::kOk: return "ok";
    case StyleError::kEmptyMap: return "empty-style-map";
    case StyleError::kTooManyDeclarations: return "too-many-declarations";
    case StyleError::kUnknownProperty: return "unknown-css-property";
    case StyleError::kEmptyValue: return "empty-style-value";
    case StyleError::kValueTooLong: return "style-value-too-long";
    case StyleError::kBangImportant: return "bang-important-refused";
    case StyleError::kUrlFunction: return "url-function-refused";
    case StyleError::kNonAsciiControl: return "control-char-refused";
  }
  return "unknown";
}

bool IsAdmittedProperty(const std::string& property) {
  // The admitted CSSOM property set. Deliberately short and layout-only: a
  // cosmetic rule's job is to make something not be seen, and every property
  // here serves that job. Notably ABSENT, on purpose:
  //   * `behavior`, `-moz-binding` — legacy code-execution vectors.
  //   * `position`/`z-index` — a rule that can restack the page can hide the
  //     user's content instead of the ad's, which is a UI-spoofing channel.
  //   * `content` — generates content, so it is not a hide operation at all.
  //   * anything taking a URL (`background`, `background-image`, `list-style`,
  //     `border-image`, `cursor`, `mask`) — a fetch side channel.
  static const char* const kAdmitted[] = {
      "display", "visibility", "opacity", "height", "max-height",
      "min-height", "width", "max-width", "min-width", "overflow",
      "pointer-events", "clip", "clip-path",
  };
  for (const char* p : kAdmitted) {
    if (property == p) return true;
  }
  return false;
}

StyleError ValidateDeclaration(const std::string& property,
                               const std::string& value) {
  if (!IsAdmittedProperty(property)) return StyleError::kUnknownProperty;
  if (value.empty()) return StyleError::kEmptyValue;
  if (value.size() > kMaxStyleValueLen) return StyleError::kValueTooLong;
  for (const unsigned char c : value) {
    if (c < 0x20 || c == 0x7f) return StyleError::kNonAsciiControl;
  }
  // `!important` is REFUSED, not stripped. Stripping would silently change the
  // author's intent and hide that a list tried to escalate past the page's own
  // styles, which is exactly the information a reviewer needs.
  if (value.find('!') != std::string::npos) return StyleError::kBangImportant;
  // A value that fetches is a network side channel from a cosmetic rule.
  if (ContainsNoCase(value, "url(") || ContainsNoCase(value, "image-set(") ||
      ContainsNoCase(value, "javascript:") ||
      ContainsNoCase(value, "expression(")) {
    return StyleError::kUrlFunction;
  }
  return StyleError::kOk;
}

}  // namespace xr::cosmetic
