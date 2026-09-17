// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/selector — the ADMITTED CSS SUBSET parser
// (P12-T1). Everything outside the subset is REFUSED at compile time with a
// typed reason; nothing is ever "sanitized and kept".
//
// Why the subset is ours and not the engine's. The vendored adblock-rust
// 0.13.3 CAN validate selectors, but only behind the `css-validation` cargo
// feature (third_party/rust/adblock/0.13.3/src/filters/cosmetic.rs:661), and
// that feature is NOT in the consumed closure: shield/engine/Cargo.toml selects
// `embedded-domain-resolver, full-regex-handling, resource-assembler` with
// `default-features = false`, and `css-validation` is in neither list. Without
// it the vendored build compiles the fallback at cosmetic.rs:645-658, whose
// `validate_css_selector` returns the selector UNCHANGED and whose
// `is_valid_css_style` is literally `-> true`. So at the pin the engine offers
// NO selector validation and NO style validation on the path we consume.
// Enabling `css-validation` would pull `cssparser` + `selectors` into the
// consumed closure — a dependency change requiring the full vendoring
// ceremony — so this parser is the boundary instead, and it is deliberately
// narrower than CSS.
//
// Laws:
//   * deny-by-default: an unrecognised token is a refusal, never a guess;
//   * no `!important` (a cosmetic rule must not out-specify the page);
//   * no functional pseudo-class outside the admitted table (pseudo.h);
//   * bounded: MAX_SELECTOR_LEN, MAX_COMPOUNDS, MAX_ATTRS. A pathological
//     selector is a typed refusal, not a slow parse;
//   * total: never throws, never allocates unboundedly, always returns.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xr::cosmetic {

// Hard bounds. Cosmetic rules come from lists we compile, but the compiler is
// fed adversarial input by the fuzz lanes and (post-farm) by page-influenced
// strings, so "unbounded" is not an acceptable answer to "how long can this
// be". These are compile-time refusals, not truncations.
inline constexpr size_t kMaxSelectorLen = 512;
inline constexpr size_t kMaxCompounds = 24;    // sequences separated by combinators
inline constexpr size_t kMaxAttrSelectors = 6;
inline constexpr size_t kMaxPseudoArgs = 8;
inline constexpr size_t kMaxIdentLen = 128;

// Typed refusal vocabulary. CLOSED: a reason not in this list is a bug, and
// tools/*_check.py compares it against the docs table. The strings are the
// wire values (golden vectors pin them byte-for-byte).
enum class SelectorError {
  kOk = 0,
  kEmpty,
  kTooLong,
  kTooManyCompounds,
  kTooManyAttrSelectors,
  kTooManyPseudoArgs,
  kIdentTooLong,
  kUnbalancedParen,
  kUnbalancedBracket,
  kEmptyCompound,
  kLeadingCombinator,
  kTrailingCombinator,
  kDoubleCombinator,
  kTrailingComma,          // a selector LIST is refused: one rule, one selector
  kDisallowedChar,
  kBangImportant,          // `!important` smuggling
  kAtRule,                 // `@import`, `@media`, ... — never a selector
  kCdataOrMarkup,          // `</style>`, `<!--`, `<script`
  kUrlFunction,            // url(...) — a fetch side channel in a style context
  kExpressionFunction,     // legacy IE expression(...) — code execution
  kUnknownPseudoClass,     // functional pseudo outside pseudo.h's table
  kDisallowedPseudoArg,
  kUniversalWithPseudo,    // `*:has(...)` — matches the whole document
  kCommentUnterminated,
  kEscapeSequence,         // CSS `\` escapes: refused outright (see .cc)
};

const char* SelectorErrorName(SelectorError e);  // closed vocabulary

// The parsed AST. DATA ONLY: no code, no callbacks, nothing a renderer could
// execute. This is the shape the blob carries, so it must be serializable
// without interpretation (arch §6: "selectors and property maps, never code").
enum class Combinator { kDescendant, kChild, kAdjacent, kGeneralSibling };

struct AttrSelector {
  std::string name;
  std::string value;       // "" when no value is present
  // Closed set of the attribute-match operators we admit.
  // kExists is `[href]`; the rest are `[href=x]`, `[href^=x]`, `[href$=x]`,
  // `[href*=x]`, `[href~=x]`, `[href|=x]`.
  enum class Op { kExists, kEquals, kPrefix, kSuffix, kSubstring,
                  kWhitespace, kHyphen } op = Op::kExists;
  bool case_insensitive = false;   // the `[a=b i]` flag
};

struct PseudoClass {
  std::string name;                // lowercased, no leading ':'
  std::vector<std::string> args;   // positional; empty for bare pseudo-classes
};

struct Compound {
  std::string tag;                 // "" = any tag (the universal case)
  std::vector<std::string> ids;
  std::vector<std::string> classes;
  std::vector<AttrSelector> attrs;
  std::vector<PseudoClass> pseudos;
  bool universal = false;          // an explicit `*`
};

struct Selector {
  // compounds[0] compounds[1] ... joined by combinators[i] between i and i+1.
  std::vector<Compound> compounds;
  std::vector<Combinator> combinators;
  size_t source_len = 0;
};

// Parse `text` into `out`. Returns kOk and fills `out`, or returns the typed
// refusal and leaves `out` in a valid (possibly partially filled) state the
// caller MUST discard — a refusal is never a partial accept.
//
// Total: bounded work, no throw, no I/O, no clock.
SelectorError ParseSelector(const std::string& text, Selector* out);

// True when `text` is a bare identifier/class/id of the trivially-safe shape
// the vendored engine fast-paths (cosmetic.rs:684-691 uses
// `^[#.]?[A-Za-z_][\w-]*$`). Kept here so the fast path and the general path
// agree instead of drifting.
bool IsTrivialSelector(const std::string& text);

}  // namespace xr::cosmetic
