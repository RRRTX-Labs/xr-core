// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// See selector.h for the contract and for WHY this parser exists instead of
// the vendored engine's (css-validation is not in the consumed feature set, so
// at the pin the engine's validate_css_selector passes selectors through
// unchanged and is_valid_css_style is `-> true`).

#include "renderer/cosmetic/core/selector.h"

#include "renderer/cosmetic/core/pseudo.h"

#include <cctype>
#include <cstring>

namespace xr::cosmetic {
namespace {

bool IsIdentStart(unsigned char c) {
  return std::isalpha(c) || c == '_' || c >= 0x80;
}
bool IsIdentChar(unsigned char c) {
  return std::isalnum(c) || c == '_' || c == '-' || c >= 0x80;
}

// Substring hunt for the markup/at-rule/escape classes. These are refused on
// SIGHT rather than parsed, because "sanitize and keep" is exactly the failure
// mode the brief forbids: a page-controlled string that gets rewritten into
// something almost-safe is a string we no longer understand.
bool ContainsNoCase(const std::string& s, const char* needle) {
  const size_t n = std::strlen(needle);
  if (n > s.size()) return false;
  for (size_t i = 0; i + n <= s.size(); ++i) {
    bool ok = true;
    for (size_t j = 0; j < n; ++j) {
      if (std::tolower(static_cast<unsigned char>(s[i + j])) !=
          std::tolower(static_cast<unsigned char>(needle[j]))) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

// The security-relevant substring refusals. Order matters only for which
// typed reason is reported; every one of these is a refusal.
SelectorError ScanDangerousSubstrings(const std::string& s) {
  // Markup escape. NOTE: a blanket `<`/`>` refusal is WRONG and was the first
  // thing the probe caught — `>` is the child combinator (`div > .ad`) and `<`
  // appears in `:lt()`. The actual escape shapes are far narrower: a `<` that
  // starts a tag or an HTML comment. Anything else containing `<` falls through
  // to kDisallowedChar, which is still a refusal, just an honest one.
  if (s.find("<!") != std::string::npos) return SelectorError::kCdataOrMarkup;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '<') continue;
    const size_t n = i + 1;
    if (n < s.size() &&
        (std::isalpha(static_cast<unsigned char>(s[n])) || s[n] == '/' ||
         s[n] == '?')) {
      return SelectorError::kCdataOrMarkup;   // `</style>`, `<script`, `<?`
    }
  }
  // ORDER MATTERS, and the tests pin it. `@import url(x)` is an AT-RULE, not a
  // url() function; reporting kUrlFunction there would send an operator to the
  // wrong part of the list-format spec to fix it. A refusal for the wrong
  // reason is a broken refusal even though the input is still rejected, so the
  // more specific shape is tested first.
  if (s.find('@') != std::string::npos) return SelectorError::kAtRule;
  if (ContainsNoCase(s, "expression(")) return SelectorError::kExpressionFunction;
  if (ContainsNoCase(s, "url(")) return SelectorError::kUrlFunction;
  if (ContainsNoCase(s, "image-set(")) return SelectorError::kUrlFunction;
  if (ContainsNoCase(s, "javascript:")) return SelectorError::kUrlFunction;
  if (s.find('\\') != std::string::npos) return SelectorError::kEscapeSequence;
  if (s.find("/*") != std::string::npos || s.find("*/") != std::string::npos) {
    return SelectorError::kCommentUnterminated;
  }
  return SelectorError::kOk;
}

bool ReadIdent(const std::string& s, size_t* i, std::string* out) {
  if (*i >= s.size() || !IsIdentStart(static_cast<unsigned char>(s[*i]))) {
    return false;
  }
  out->clear();
  while (*i < s.size() && IsIdentChar(static_cast<unsigned char>(s[*i]))) {
    out->push_back(s[*i]);
    ++*i;
  }
  return out->size() <= kMaxIdentLen;
}

// Read a possibly-quoted attribute value. Quotes are permitted INSIDE an
// attribute value (they are how `[title="a b"]` is written) and are stripped;
// they are not permitted anywhere else in the selector.
bool ReadAttrValue(const std::string& s, size_t* i, std::string* out) {
  out->clear();
  if (*i >= s.size()) return false;
  const char q = s[*i];
  if (q == '"' || q == '\'') {
    ++*i;
    while (*i < s.size() && s[*i] != q) {
      out->push_back(s[*i]);
      ++*i;
    }
    if (*i >= s.size()) return false;   // unterminated quote
    ++*i;
    return true;
  }
  while (*i < s.size() && s[*i] != ']' && !std::isspace(
             static_cast<unsigned char>(s[*i]))) {
    out->push_back(s[*i]);
    ++*i;
  }
  return true;
}

SelectorError ParseAttr(const std::string& s, size_t* i, AttrSelector* out,
                        size_t* attr_count) {
  if (*attr_count >= kMaxAttrSelectors) {
    return SelectorError::kTooManyAttrSelectors;
  }
  ++*attr_count;
  ++*i;  // consume '['
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++*i;
  if (!ReadIdent(s, i, &out->name)) {
    // Reaching EOF with no name means the bracket was never closed. Reporting
    // kDisallowedChar here would be an honest refusal but the wrong diagnosis:
    // `.ad[` is truncated, not carrying a banned character, and the two need
    // different fixes in the list source.
    return (*i >= s.size()) ? SelectorError::kUnbalancedBracket
                            : SelectorError::kDisallowedChar;
  }
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++*i;
  if (*i >= s.size()) return SelectorError::kUnbalancedBracket;
  if (s[*i] == ']') {
    out->op = AttrSelector::Op::kExists;
    ++*i;
    return SelectorError::kOk;
  }
  // operator
  if (*i + 1 < s.size() && s[*i + 1] == '=') {
    switch (s[*i]) {
      case '^': out->op = AttrSelector::Op::kPrefix; break;
      case '$': out->op = AttrSelector::Op::kSuffix; break;
      case '*': out->op = AttrSelector::Op::kSubstring; break;
      case '~': out->op = AttrSelector::Op::kWhitespace; break;
      case '|': out->op = AttrSelector::Op::kHyphen; break;
      default: return SelectorError::kDisallowedChar;
    }
    *i += 2;
  } else if (s[*i] == '=') {
    out->op = AttrSelector::Op::kEquals;
    ++*i;
  } else {
    return SelectorError::kDisallowedChar;
  }
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++*i;
  if (!ReadAttrValue(s, i, &out->value)) return SelectorError::kDisallowedChar;
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++*i;
  // the case-insensitivity flag
  if (*i < s.size() && (s[*i] == 'i' || s[*i] == 'I')) {
    out->case_insensitive = true;
    ++*i;
    while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++*i;
  }
  if (*i >= s.size() || s[*i] != ']') {
    return SelectorError::kUnbalancedBracket;
  }
  ++*i;
  return SelectorError::kOk;
}

}  // namespace

const char* SelectorErrorName(SelectorError e) {
  switch (e) {
    case SelectorError::kOk: return "ok";
    case SelectorError::kEmpty: return "empty-selector";
    case SelectorError::kTooLong: return "selector-too-long";
    case SelectorError::kTooManyCompounds: return "too-many-compounds";
    case SelectorError::kTooManyAttrSelectors: return "too-many-attr-selectors";
    case SelectorError::kTooManyPseudoArgs: return "too-many-pseudo-args";
    case SelectorError::kIdentTooLong: return "ident-too-long";
    case SelectorError::kUnbalancedParen: return "unbalanced-paren";
    case SelectorError::kUnbalancedBracket: return "unbalanced-bracket";
    case SelectorError::kEmptyCompound: return "empty-compound";
    case SelectorError::kLeadingCombinator: return "leading-combinator";
    case SelectorError::kTrailingCombinator: return "trailing-combinator";
    case SelectorError::kDoubleCombinator: return "double-combinator";
    case SelectorError::kTrailingComma: return "selector-list-refused";
    case SelectorError::kDisallowedChar: return "disallowed-char";
    case SelectorError::kBangImportant: return "bang-important-refused";
    case SelectorError::kAtRule: return "at-rule-refused";
    case SelectorError::kCdataOrMarkup: return "markup-refused";
    case SelectorError::kUrlFunction: return "url-function-refused";
    case SelectorError::kExpressionFunction: return "expression-refused";
    case SelectorError::kUnknownPseudoClass: return "unknown-pseudo-class";
    case SelectorError::kDisallowedPseudoArg: return "disallowed-pseudo-arg";
    case SelectorError::kUniversalWithPseudo: return "universal-with-pseudo";
    case SelectorError::kCommentUnterminated: return "comment-refused";
    case SelectorError::kEscapeSequence: return "escape-sequence-refused";
  }
  return "unknown";  // unreachable; keeps -Wreturn-type honest
}

bool IsTrivialSelector(const std::string& text) {
  if (text.empty() || text.size() > kMaxIdentLen) return false;
  size_t i = 0;
  if (text[0] == '#' || text[0] == '.') i = 1;
  if (i >= text.size()) return false;
  if (!IsIdentStart(static_cast<unsigned char>(text[i]))) return false;
  for (; i < text.size(); ++i) {
    if (!IsIdentChar(static_cast<unsigned char>(text[i]))) return false;
  }
  return true;
}

SelectorError ParseSelector(const std::string& text, Selector* out) {
  out->compounds.clear();
  out->combinators.clear();
  out->source_len = text.size();
  // Whitespace-only is EMPTY, not an empty compound. The distinction matters:
  // kEmptyCompound means the selector had structure but one compound resolved
  // to nothing (`div..ad`), while kEmpty means there was no selector at all.
  // A list entry that is blank after trimming is the common case, and telling
  // an operator "empty" rather than "malformed compound" is the difference
  // between a fixable data entry and a hunt for a parser bug.
  if (text.empty() ||
      text.find_first_not_of(" \t\r\n\f") == std::string::npos) {
    return SelectorError::kEmpty;
  }
  if (text.size() > kMaxSelectorLen) return SelectorError::kTooLong;
  // `!important` cannot appear in a SELECTOR at all, so its presence means
  // somebody is trying to smuggle a declaration through the selector field.
  if (ContainsNoCase(text, "!important")) return SelectorError::kBangImportant;
  // The dangerous-substring scan runs BEFORE the bare-'!' refusal. `<!--` is an
  // HTML comment open, and reporting it as kDisallowedChar would be an honest
  // refusal but the wrong diagnosis — an operator would go hunting for a stray
  // character instead of recognising a markup-escape attempt. Same principle as
  // the '@' before 'url(' ordering below: every one of these rejects the input,
  // and the typed reason is what makes the rejection actionable.
  const SelectorError danger = ScanDangerousSubstrings(text);
  if (danger != SelectorError::kOk) return danger;
  if (text.find('!') != std::string::npos) return SelectorError::kDisallowedChar;
  // One rule, one selector: a comma-separated LIST is refused rather than
  // split, so a rule can never widen its own scope after compilation.
  if (text.find(',') != std::string::npos) return SelectorError::kTrailingComma;

  size_t i = 0;
  size_t attr_count = 0;
  Compound cur;
  bool cur_empty = true;
  // ONE flag, ONE meaning: a combinator has been recorded into
  // `out->combinators` and its right-hand compound has not started yet. The
  // invariant that makes the whole machine checkable is
  //     out->combinators.size() == out->compounds.size() - 1 + (awaiting ? 1 : 0)
  // so at end of input `awaiting` must be false, and a second combinator while
  // `awaiting` is true is a double combinator. Three earlier attempts at this
  // used a second flag and mis-ordered the flush; the invariant is what the
  // test suite now pins.
  bool awaiting = false;

  auto complete = [&](SelectorError* err) -> bool {
    // Move the in-progress compound into the list, if there is one.
    if (cur_empty) return true;
    if (out->compounds.size() >= kMaxCompounds) {
      *err = SelectorError::kTooManyCompounds;
      return false;
    }
    out->compounds.push_back(std::move(cur));
    cur = Compound{};
    cur_empty = true;
    return true;
  };
  // Called on the first token of a compound's contents.
  auto begin_compound = [&](SelectorError* err) -> bool {
    if (!cur_empty) return true;             // already begun
    if (out->compounds.empty() && !awaiting) {
      return true;                           // the very first compound
    }
    if (!awaiting) {
      // A compound is starting with no combinator recorded: that is the
      // descendant combinator implied by whitespace we already skipped.
      out->combinators.push_back(Combinator::kDescendant);
    }
    awaiting = false;
    (void)err;
    return true;
  };

  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (std::isspace(c)) {
      size_t j = i;
      while (j < text.size() &&
             std::isspace(static_cast<unsigned char>(text[j]))) ++j;
      if (j >= text.size()) { i = j; continue; }   // trailing ws: ignore
      // Whitespace between compounds implies a descendant combinator. Complete
      // the in-progress compound now; the combinator itself is recorded lazily
      // by begin_compound(), which is what stops `div > .ad` from collecting a
      // second (descendant) combinator ahead of the explicit child one.
      SelectorError e = SelectorError::kOk;
      if (!complete(&e)) return e;
      i = j;
      continue;
    }
    if (c == '>' || c == '+' || c == '~') {
      if (awaiting) return SelectorError::kDoubleCombinator;   // `div > > .a`
      SelectorError e = SelectorError::kOk;
      if (!complete(&e)) return e;
      if (out->compounds.empty()) return SelectorError::kLeadingCombinator;
      out->combinators.push_back(c == '>' ? Combinator::kChild
                                 : c == '+' ? Combinator::kAdjacent
                                            : Combinator::kGeneralSibling);
      awaiting = true;
      ++i;
      continue;
    }
    {
      SelectorError e = SelectorError::kOk;
      if (!begin_compound(&e)) return e;
    }
    if (c == '*') {
      cur.universal = true;
      cur_empty = false;
      ++i;
      continue;
    }
    if (c == '#') {
      ++i;
      std::string id;
      if (!ReadIdent(text, &i, &id)) {
        return id.empty() ? SelectorError::kDisallowedChar
                          : SelectorError::kIdentTooLong;
      }
      cur.ids.push_back(std::move(id));
      cur_empty = false;
      continue;
    }
    if (c == '.') {
      ++i;
      std::string cls;
      if (!ReadIdent(text, &i, &cls)) {
        return cls.empty() ? SelectorError::kDisallowedChar
                           : SelectorError::kIdentTooLong;
      }
      cur.classes.push_back(std::move(cls));
      cur_empty = false;
      continue;
    }
    if (c == '[') {
      AttrSelector a;
      const SelectorError e = ParseAttr(text, &i, &a, &attr_count);
      if (e != SelectorError::kOk) return e;
      cur.attrs.push_back(std::move(a));
      cur_empty = false;
      continue;
    }
    if (c == ':') {
      ++i;
      if (i < text.size() && text[i] == ':') ++i;  // ::before / ::after form
      PseudoClass pc;
      if (!ReadIdent(text, &i, &pc.name)) return SelectorError::kDisallowedChar;
      for (auto& ch : pc.name) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      }
      if (i < text.size() && text[i] == '(') {
        ++i;
        int depth = 1;
        std::string arg;
        while (i < text.size() && depth > 0) {
          if (text[i] == '(') ++depth;
          if (text[i] == ')') {
            --depth;
            if (depth == 0) { ++i; break; }
          }
          arg.push_back(text[i]);
          ++i;
        }
        if (depth != 0) return SelectorError::kUnbalancedParen;
        if (pc.args.size() >= kMaxPseudoArgs) {
          return SelectorError::kTooManyPseudoArgs;
        }
        pc.args.push_back(std::move(arg));
      }
      // `*` + a functional pseudo (`*:has(...)`) selects the whole document and
      // is refused: a cosmetic rule that can match everything is a page-wide
      // mutation, not an ad hide.
      if (cur.universal && !pc.args.empty()) {
        return SelectorError::kUniversalWithPseudo;
      }

      // THE ALLOWLIST. The vendored engine passes ANY unrecognised pseudo-class
      // through as AnythingElse (cosmetic.rs:1184, and :963-966 returns
      // Ok(AnythingElse(...))), so "the engine accepts it" is not a reason for
      // us to. Two closed tables decide: pseudo.h's procedural table, and its
      // native CSS allowlist. Anything in neither is refused here rather than
      // forwarded, which is what keeps `kUnknownPseudoClass` a real refusal
      // instead of a name in the enum.
      const PseudoInfo* info = LookupPseudo(pc.name);
      if (info != nullptr) {
        // A table entry that is not admitted is refused with the parser's own
        // vocabulary; the table's kRefused* reasons are the WHY, recorded in
        // the blob's refusal note rather than in the parser's enum.
        if (info->support != PseudoSupport::kAdmitted) {
          return SelectorError::kUnknownPseudoClass;
        }
        // Arity is enforced from the table, not from the parser's guesses: a
        // functional pseudo used bare, or a bare one used functionally, is a
        // malformed rule.
        if (info->takes_arg && pc.args.empty()) {
          return SelectorError::kDisallowedPseudoArg;
        }
        if (!info->takes_arg && !pc.args.empty()) {
          return SelectorError::kDisallowedPseudoArg;
        }
        // A pseudo whose argument is itself a SELECTOR is parsed recursively so
        // it is subject to the same bounds and refusals. Without this, a nested
        // selector could smuggle anything the outer parser refuses.
        if (info->arg_is_selector && !pc.args.empty()) {
          Selector nested;
          SelectorError e = ParseSelector(pc.args[0], &nested);
          if (e != SelectorError::kOk) return e;
        }
      } else if (!IsAdmittedNativePseudo(pc.name)) {
        return SelectorError::kUnknownPseudoClass;
      }

      cur.pseudos.push_back(std::move(pc));
      cur_empty = false;
      continue;
    }
    if (IsIdentStart(c)) {
      std::string tag;
      if (!ReadIdent(text, &i, &tag)) return SelectorError::kIdentTooLong;
      if (!cur.tag.empty()) return SelectorError::kDisallowedChar;
      cur.tag = std::move(tag);
      cur_empty = false;
      continue;
    }
    return SelectorError::kDisallowedChar;
  }

  // Complete the final compound and check the invariant.
  {
    SelectorError e = SelectorError::kOk;
    if (!complete(&e)) return e;
  }
  if (out->compounds.empty()) return SelectorError::kEmptyCompound;
  if (awaiting) return SelectorError::kTrailingCombinator;   // `.ad >`
  // The invariant: N compounds are joined by exactly N-1 combinators.
  if (out->combinators.size() + 1 != out->compounds.size()) {
    return SelectorError::kTrailingCombinator;
  }
  return SelectorError::kOk;
}

}  // namespace xr::cosmetic
