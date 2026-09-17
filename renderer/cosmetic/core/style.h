// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/style — the EMITTED DECLARATION FORM (P12-T1).
//
// This module is the last place a cosmetic rule turns into something a renderer
// acts on, so it is deliberately the narrowest thing in the phase: a closed set
// of actions, a closed set of CSS properties, and no string interpolation of
// list data into anything that gets executed.
//
// THE LAW. arch §6: style declarations go through the cited CSSOM path, never
// by writing HTML, and never by building a declaration string that a renderer
// then parses. So this module produces STRUCTURED DATA — {property, value}
// pairs from a closed vocabulary — and the renderer applies them through the
// CSSOM. A rule cannot express "set this to whatever I wrote" because there is
// no field that carries a free-form declaration.
//
// What that buys, concretely:
//   * `!important` is refused, not stripped. Stripping would silently change
//     the author's intent and hide the fact that a list tried to escalate.
//   * `url(...)` in a value is refused: a style value that fetches is a network
//     side channel from a cosmetic rule.
//   * The property allowlist is what stops `behavior:` (IE), `-moz-binding:`
//     (legacy Firefox), and `expression()` from ever reaching the CSSOM, even
//     though the selector parser already refuses those shapes. Two
//     independent refusals for one class of bug is the point, not redundancy.
#pragma once

#include <string>
#include <vector>

namespace xr::cosmetic {

// What a matched rule DOES. Closed set: the blob's `action` field maps to
// exactly one of these, and anything else is refused at blob validation.
enum class Action {
  kHide = 0,       // display:none — the element stays in the DOM
  kCollapse,       // display:none + layout removal (the uBO "collapse" sense)
  kVisibilityOff,  // visibility:hidden — keeps layout, blanks the content
  kRemove,         // DOM removal. PAGE-MODIFYING (see the note below)
  kStyle,          // an explicit {property,value} map from the rule
};

const char* ActionName(Action a);

// Parses the blob's action string. Returns false for anything not in the set,
// including empty and case variants — "Hide" is refused rather than lowered,
// so a producer that gets the case wrong finds out immediately.
bool ParseAction(const std::string& text, Action* out);

// True when the action mutates the page rather than hiding something on it.
// The Observatory must label these honestly: an injected/removed element is not
// a blocked request, and the plan's UX rows require the distinction.
bool ActionIsPageModifying(Action a);

// The CSSOM-facing declarations an action implies. Returned as data, in a
// stable order, so the emitted form is reproducible and can be golden-tested.
struct Declaration {
  const char* property;
  const char* value;
};

// The declarations for a built-in action. Empty for kStyle (which carries its
// own map) and for kRemove (which has no declaration — it removes the node).
std::vector<Declaration> DeclarationsForAction(Action a);

// Validation of a rule-supplied style map. This is the path a list author
// controls, so it is the one that needs the allowlist.
enum class StyleError {
  kOk = 0,
  kEmptyMap,
  kTooManyDeclarations,
  kUnknownProperty,      // outside the allowlist below
  kEmptyValue,
  kValueTooLong,
  kBangImportant,        // refused, never stripped
  kUrlFunction,          // a fetch side channel
  kNonAsciiControl,
};

const char* StyleErrorName(StyleError e);

struct ValidatedStyle {
  std::vector<std::pair<std::string, std::string>> declarations;
};

inline constexpr size_t kMaxDeclarations = 16;
inline constexpr size_t kMaxStyleValueLen = 256;

// Validates one property/value pair. Exposed so the host can validate a map
// entry-by-entry and report the first offending property.
StyleError ValidateDeclaration(const std::string& property,
                               const std::string& value);

// True when `property` is in the admitted CSSOM property set.
bool IsAdmittedProperty(const std::string& property);

}  // namespace xr::cosmetic
