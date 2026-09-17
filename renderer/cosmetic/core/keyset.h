// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/keyset — per-site rule COMPILATION (P12-T1).
//
// A blob is data a list author wrote. A key set is the same rules after they
// have been parsed, deduped, bounded and accounted for — the form the renderer
// actually matches against. This module is the only place that transformation
// happens, so "what will this site do?" has one answer.
//
// THREE LAWS:
//
//   1. DEDUP IS BY SEMANTICS, NOT BY TEXT. Two rules with identical selectors
//      and identical actions are one rule. Deduping by text alone would keep
//      `div>.ad` and `div > .ad` as two, and a list that ships both would pay
//      twice for one hide. The canonical form is the parsed AST's stable
//      rendering, so text variants collapse.
//
//   2. THE BUDGET IS ENFORCED HERE, NOT IN THE RENDERER. A key set that exceeds
//      the rule or byte budget is refused as a whole with a typed reason. The
//      renderer must never be the thing that discovers a site has too many
//      rules, because by then it is already parsing them at document-start.
//
//   3. SIZE ACCOUNTING IS REAL. `bytes` is the serialized size the blob cache
//      will charge against its bound, and `compiled_selectors` is the count the
//      perf budget measures. Both are computed here so the cache and the
//      benchmark cannot disagree about what a key set costs.
#pragma once

#include <string>
#include <vector>

#include "renderer/cosmetic/core/selector.h"
#include "renderer/cosmetic/core/style.h"

namespace xr::cosmetic {

inline constexpr size_t kMaxKeySetRules = 4096;
inline constexpr size_t kMaxKeySetBytes = 1u << 20;  // 1 MiB per site

// One compiled rule. DATA ONLY: no callbacks, no engine handles, nothing a
// renderer could execute. This is the shape that crosses into the payload.
struct CompiledRule {
  std::string id;
  // The canonical rendering of the parsed selector. Two text variants of the
  // same selector produce the same string, which is what makes dedup semantic.
  std::string canonical_selector;
  // The parsed AST, kept so the renderer does not re-parse at document-start.
  Selector selector;
  Action action = Action::kHide;
  std::vector<std::pair<std::string, std::string>> style;
  std::vector<std::string> exception_sites;
  bool enabled = true;
  size_t bytes = 0;  // this rule's contribution to the key set's size
};

enum class KeySetError {
  kOk = 0,
  kTooManyRules,
  kTooManyBytes,
  kRuleRejected,      // one rule failed; `reason` says why and `at` says which
  kEmptyInput,
  kDuplicateId,
};

const char* KeySetErrorName(KeySetError e);

struct KeySetResult {
  std::vector<CompiledRule> rules;
  size_t bytes = 0;
  size_t compiled_selectors = 0;
  size_t duplicates_removed = 0;
  // Set when the result is a refusal, so a caller can report it honestly
  // instead of shipping a silently-truncated key set.
  KeySetError error = KeySetError::kOk;
  std::string reason;
  size_t failed_index = 0;
  bool valid = false;
};

// Compiles a set of rules into a key set. `rules_json` is the blob's `rules`
// array as canonical text; the host parses it and calls the structured overload
// below. This one exists so the tests can drive compilation without JSON.
struct InputRule {
  std::string id;
  std::string selector;
  std::string action;
  std::vector<std::pair<std::string, std::string>> style;
  std::vector<std::string> exception_sites;
  bool enabled = true;
};

// Compiles and bounds a key set. Total: no throw, no I/O, no clock.
//
// A refusal leaves `out->valid == false` and fills `error`/`reason`/
// `failed_index`. It never returns a partially compiled set, because a
// silently truncated key set is indistinguishable from a correct one to the
// renderer and to the user.
KeySetError CompileKeySet(const std::vector<InputRule>& in, KeySetResult* out);

// The canonical rendering of a parsed selector — the dedup key. Stable across
// runs and machines (no pointer values, no map iteration order leaking in).
std::string CanonicalizeSelector(const Selector& s);

// Serialized size of one rule, for the byte accounting. Counts the fields the
// blob cache actually stores, not sizeof(), so the number means something to
// the cache's bound.
size_t RuleBytes(const CompiledRule& r);

}  // namespace xr::cosmetic
