// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P12 — ABPF ($removeparam) filter validation.
//
// AdBlock Plus Filter syntax is a DIFFERENT language from the cosmetic selector
// syntax in core/selector.h, and mixing the two would be a security bug: an
// ABPF filter carries a directive ($removeparam, $rewrite) that a cosmetic
// selector never should. This module parses only the subset the cosmetic
// surface needs and REFUSES everything else, including every directive that
// changes a request rather than the page.
//
// The reason it lives in xr-core and not in the list pipeline: the brief says a
// list that does not validate cannot be published, and the validator the
// pipeline calls must be the same code the renderer trusts. Two validators
// would mean a list that passes one and is refused by the other.
//
// SECURITY: validation only. Nothing here is executed and no filter here
// reaches the network layer — $removeparam is a NETWORK feature and belongs to
// a different phase's validator. This module exists so a cosmetic list cannot
// smuggle one in.

#ifndef XR_CORE_RENDERER_COSMETIC_ABPF_ABPF_H_
#define XR_CORE_RENDERER_COSMETIC_ABPF_ABPF_H_

#include <string>
#include <vector>

namespace xr::cosmetic {

// A refusal for one filter line.
enum class AbpfError {
  kOk,
  kEmpty,
  kTooLong,
  kTooManyOptions,
  kComment,              // `!` or the `[Adblock Plus 2.0]` header: skipped
  kNetworkFilter,        // has an address part: not a cosmetic filter
  kDirectiveNotAllowed,  // $removeparam, $rewrite, $csp — REFUSED
  kUnknownDirective,
  kEmptySelector,
  kSelectorRefused,      // delegates to core/selector.h; error_detail names it
  kGenerichideDirective,
  kMalformedOption,
};

// Returns a stable kebab-case name. Copied into fakes/cosmetic.py and
// tools/cosmetic_vectors_check.py; a divergence is a gate failure.
const char* AbpfErrorName(AbpfError e);

// A directive seen on the filter, kept so a caller can report what a list
// TRIED to do — which is what makes a coverage number honest.
struct AbpfDirective {
  std::string name;  // e.g. "removeparam", "generichide"
  std::string arg;   // text after the first '=', empty if none
};

// A cosmetic filter: `##selector` or `#@#selector` (an exception rule).
struct AbpfFilter {
  std::string raw;
  std::string selector;
  bool exception = false;            // `#@#`
  std::vector<std::string> domains;  // from `$domain=a|~b`
  std::vector<AbpfDirective> directives;
  std::string error_detail;  // set when the selector was refused
  bool valid = false;
};

// Directives that change a REQUEST rather than the page. Refused here so a
// cosmetic list cannot smuggle a network feature in. Spelled out rather than
// "everything not on an allowlist" so the refusal can name what was refused —
// an allowlist would refuse correctly but explain nothing.
bool IsNetworkDirective(const std::string& name);

// Parses one filter line. `strict` (default true) refuses a network filter.
//
// Returns kComment for a comment line, which a caller must SKIP rather than
// treat as a refusal — a list header is not an error.
AbpfError ParseAbpfFilter(const std::string& line, bool strict,
                          AbpfFilter* out);

constexpr size_t kMaxAbpfFilterLen = 8192;
constexpr size_t kMaxAbpfOptions = 32;

}  // namespace xr::cosmetic

#endif  // XR_CORE_RENDERER_COSMETIC_ABPF_ABPF_H_
