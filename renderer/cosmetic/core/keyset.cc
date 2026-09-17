// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// See keyset.h for the three laws this module enforces.

#include "renderer/cosmetic/core/keyset.h"

#include <algorithm>
#include <set>

namespace xr::cosmetic {
namespace {

// A sorted copy, so canonicalization cannot depend on the order a list author
// happened to type things in. Without this, `.a.b` and `.b.a` would produce
// different dedup keys and a list shipping both would pay twice for one hide.
std::vector<std::string> SortedCopy(const std::vector<std::string>& v) {
  std::vector<std::string> out = v;
  std::sort(out.begin(), out.end());
  return out;
}

const char* CombinatorToken(Combinator c) {
  switch (c) {
    case Combinator::kDescendant: return " ";
    case Combinator::kChild: return ">";
    case Combinator::kAdjacent: return "+";
    case Combinator::kGeneralSibling: return "~";
  }
  return " ";
}

const char* AttrOpToken(AttrSelector::Op op) {
  switch (op) {
    case AttrSelector::Op::kExists: return "";
    case AttrSelector::Op::kEquals: return "=";
    case AttrSelector::Op::kPrefix: return "^=";
    case AttrSelector::Op::kSuffix: return "$=";
    case AttrSelector::Op::kSubstring: return "*=";
    case AttrSelector::Op::kWhitespace: return "~=";
    case AttrSelector::Op::kHyphen: return "|=";
  }
  return "";
}

void AppendCompound(const Compound& c, std::string* out) {
  if (c.universal) out->push_back('*');
  out->append(c.tag);
  // Sorted, so a rule written `.a.b` and one written `.b.a` canonicalize the
  // same. Without this, dedup would depend on the order a list author happened
  // to type the classes in.
  for (const std::string& id : SortedCopy(c.ids)) {
    out->push_back('#');
    out->append(id);
  }
  for (const std::string& cls : SortedCopy(c.classes)) {
    out->push_back('.');
    out->append(cls);
  }
  for (const AttrSelector& a : c.attrs) {
    out->push_back('[');
    out->append(a.name);
    out->append(AttrOpToken(a.op));
    if (a.op != AttrSelector::Op::kExists) {
      out->push_back('"');
      out->append(a.value);
      out->push_back('"');
      if (a.case_insensitive) out->append(" i");
    }
    out->push_back(']');
  }
  for (const PseudoClass& p : c.pseudos) {
    out->push_back(':');
    out->append(p.name);
    if (!p.args.empty()) {
      out->push_back('(');
      for (size_t i = 0; i < p.args.size(); ++i) {
        if (i) out->push_back(',');
        out->append(p.args[i]);
      }
      out->push_back(')');
    }
  }
}

}  // namespace

const char* KeySetErrorName(KeySetError e) {
  switch (e) {
    case KeySetError::kOk: return "ok";
    case KeySetError::kTooManyRules: return "too-many-rules";
    case KeySetError::kTooManyBytes: return "too-many-bytes";
    case KeySetError::kRuleRejected: return "rule-rejected";
    case KeySetError::kEmptyInput: return "empty-rule-set";
    case KeySetError::kDuplicateId: return "duplicate-rule-id";
  }
  return "unknown";
}

std::string CanonicalizeSelector(const Selector& s) {
  std::string out;
  for (size_t i = 0; i < s.compounds.size(); ++i) {
    if (i > 0) {
      out.push_back(' ');
      out.append(CombinatorToken(s.combinators.at(i - 1)));
      out.push_back(' ');
    }
    AppendCompound(s.compounds[i], &out);
  }
  return out;
}

size_t RuleBytes(const CompiledRule& r) {
  // The fields the blob cache actually stores. sizeof() would include padding
  // and vector capacity, which would make the number meaningless as a cache
  // bound, so this counts characters instead.
  size_t n = r.id.size() + r.canonical_selector.size();
  for (const auto& [k, v] : r.style) n += k.size() + v.size() + 2;
  for (const std::string& s : r.exception_sites) n += s.size() + 1;
  return n;
}

KeySetError CompileKeySet(const std::vector<InputRule>& in, KeySetResult* out) {
  out->rules.clear();
  out->bytes = 0;
  out->compiled_selectors = 0;
  out->duplicates_removed = 0;
  out->error = KeySetError::kOk;
  out->reason.clear();
  out->failed_index = 0;
  out->valid = false;

  if (in.empty()) {
    // Not an error at the call site — the degrade table maps an empty rule set
    // to kNoWork — but it IS a refusal here, because compiling nothing and
    // calling it a key set would let a caller install an observer for nothing.
    out->error = KeySetError::kEmptyInput;
    out->reason = KeySetErrorName(KeySetError::kEmptyInput);
    return out->error;
  }
  if (in.size() > kMaxKeySetRules) {
    out->error = KeySetError::kTooManyRules;
    out->reason = KeySetErrorName(KeySetError::kTooManyRules);
    return out->error;
  }

  std::set<std::string> seen_ids;
  std::set<std::string> seen_semantics;

  for (size_t i = 0; i < in.size(); ++i) {
    const InputRule& ir = in[i];
    if (ir.id.empty()) {
      out->error = KeySetError::kDuplicateId;
      out->reason = "empty-rule-id";
      out->failed_index = i;
      out->rules.clear();
      out->bytes = 0;
      out->compiled_selectors = 0;
      return out->error;
    }
    if (!seen_ids.insert(ir.id).second) {
      out->error = KeySetError::kDuplicateId;
      out->reason = KeySetErrorName(KeySetError::kDuplicateId);
      out->failed_index = i;
      out->rules.clear();
      out->bytes = 0;
      out->compiled_selectors = 0;
      return out->error;
    }

    // THE PARSE IS THE VALIDATION. The same parser the renderer will use, so a
    // rule this function accepts is a rule the renderer can compile.
    Selector parsed;
    SelectorError e = ParseSelector(ir.selector, &parsed);
    if (e != SelectorError::kOk) {
      out->error = KeySetError::kRuleRejected;
      out->reason = SelectorErrorName(e);
      out->failed_index = i;
      out->rules.clear();
      out->bytes = 0;
      out->compiled_selectors = 0;
      return out->error;
    }

    Action action = Action::kHide;
    if (!ParseAction(ir.action, &action)) {
      out->error = KeySetError::kRuleRejected;
      out->reason = "unknown-action";
      out->failed_index = i;
      out->rules.clear();
      out->bytes = 0;
      out->compiled_selectors = 0;
      return out->error;
    }

    std::vector<std::pair<std::string, std::string>> style;
    if (action == Action::kStyle) {
      if (ir.style.empty()) {
        out->error = KeySetError::kRuleRejected;
        out->reason = StyleErrorName(StyleError::kEmptyMap);
        out->failed_index = i;
        out->rules.clear();
        out->bytes = 0;
        out->compiled_selectors = 0;
        return out->error;
      }
      if (ir.style.size() > kMaxDeclarations) {
        out->error = KeySetError::kRuleRejected;
        out->reason = StyleErrorName(StyleError::kTooManyDeclarations);
        out->failed_index = i;
        out->rules.clear();
        out->bytes = 0;
        out->compiled_selectors = 0;
        return out->error;
      }
      for (const auto& [prop, value] : ir.style) {
        StyleError se = ValidateDeclaration(prop, value);
        if (se != StyleError::kOk) {
          out->error = KeySetError::kRuleRejected;
          out->reason = StyleErrorName(se);
          out->failed_index = i;
          out->rules.clear();
          out->bytes = 0;
          out->compiled_selectors = 0;
          return out->error;
        }
        style.emplace_back(prop, value);
      }
    } else {
      // A built-in action's declarations come from the table, so a rule cannot
      // mix "hide" with its own style map and get an unbounded declaration set.
      for (const Declaration& d : DeclarationsForAction(action)) {
        style.emplace_back(d.property, d.value);
      }
    }

    CompiledRule cr;
    cr.id = ir.id;
    cr.canonical_selector = CanonicalizeSelector(parsed);
    cr.selector = std::move(parsed);
    cr.action = action;
    cr.style = std::move(style);
    cr.exception_sites = ir.exception_sites;
    cr.enabled = ir.enabled;

    // DEDUP BY SEMANTICS. The key includes the action and the style, because
    // the same selector with a different action is a different rule: hiding
    // `.ad` and removing `.ad` are not the same thing.
    std::string semantic = cr.canonical_selector;
    semantic.push_back('\x01');
    semantic.append(ActionName(cr.action));
    for (const auto& [k, v] : cr.style) {
      semantic.push_back('\x01');
      semantic.append(k);
      semantic.push_back('=');
      semantic.append(v);
    }
    if (!seen_semantics.insert(semantic).second) {
      ++out->duplicates_removed;
      continue;
    }

    cr.bytes = RuleBytes(cr);
    out->bytes += cr.bytes;
    ++out->compiled_selectors;
    out->rules.push_back(std::move(cr));

    // The byte budget is enforced DURING the walk, not after, so a pathological
    // input cannot allocate 1 GiB of rules before being refused.
    if (out->bytes > kMaxKeySetBytes) {
      out->error = KeySetError::kTooManyBytes;
      out->reason = KeySetErrorName(KeySetError::kTooManyBytes);
      out->failed_index = i;
      out->rules.clear();
      return out->error;
    }
  }

  out->valid = true;
  return KeySetError::kOk;
}

}  // namespace xr::cosmetic
