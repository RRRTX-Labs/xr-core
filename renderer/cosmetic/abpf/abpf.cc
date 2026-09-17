// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.

#include "renderer/cosmetic/abpf/abpf.h"

#include <algorithm>

#include "renderer/cosmetic/core/selector.h"

namespace xr::cosmetic {
namespace {

bool StartsWith(const std::string& s, const char* p) {
  size_t n = 0;
  while (p[n] != '\0') ++n;
  return s.size() >= n && s.compare(0, n, p) == 0;
}

std::string LowerCopy(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
  });
  return out;
}

// Directives that change a request. Each one is a NETWORK feature and belongs
// to a different phase's validator; a cosmetic list that carries one is refused
// rather than partially honoured, because partially honouring it would mean the
// cosmetic surface performed a network action.
const char* const kNetworkDirectives[] = {
    "removeparam", "rewrite", "csp", "replace", "redirect", "redirect-rule",
    "header",      "requestheader", "responseheader", "urltransform",
};

}  // namespace

const char* AbpfErrorName(AbpfError e) {
  switch (e) {
    case AbpfError::kOk: return "ok";
    case AbpfError::kEmpty: return "empty-filter";
    case AbpfError::kTooLong: return "filter-too-long";
    case AbpfError::kTooManyOptions: return "too-many-options";
    case AbpfError::kComment: return "comment";
    case AbpfError::kNetworkFilter: return "network-filter-refused";
    case AbpfError::kDirectiveNotAllowed: return "directive-not-allowed";
    case AbpfError::kUnknownDirective: return "unknown-directive";
    case AbpfError::kEmptySelector: return "empty-selector";
    case AbpfError::kSelectorRefused: return "selector-refused";
    case AbpfError::kGenerichideDirective: return "generichide-refused";
    case AbpfError::kMalformedOption: return "malformed-option";
  }
  return "unknown";
}

bool IsNetworkDirective(const std::string& name) {
  std::string lower = LowerCopy(name);
  for (const char* d : kNetworkDirectives) {
    if (lower == d) return true;
  }
  return false;
}

AbpfError ParseAbpfFilter(const std::string& line, bool strict,
                          AbpfFilter* out) {
  *out = AbpfFilter{};
  out->raw = line;

  if (line.empty()) return AbpfError::kEmpty;
  if (line.size() > kMaxAbpfFilterLen) return AbpfError::kTooLong;

  // A comment, including the list header. NOT a refusal: a caller must skip it.
  if (line[0] == '!' || StartsWith(line, "[Adblock")) {
    return AbpfError::kComment;
  }

  // A cosmetic filter has an anchor: `##` (hide) or `#@#` (exception). Anything
  // with an address part before the anchor is a NETWORK filter, and in strict
  // mode that is refused rather than silently truncated.
  size_t anchor = line.find("##");
  size_t ex_anchor = line.find("#@#");
  if (ex_anchor != std::string::npos &&
      (anchor == std::string::npos || ex_anchor < anchor)) {
    anchor = ex_anchor;
    out->exception = true;
  }
  if (anchor == std::string::npos) {
    // No anchor at all: an address filter. Refused.
    return strict ? AbpfError::kNetworkFilter : AbpfError::kNetworkFilter;
  }
  if (anchor > 0 && strict) {
    // Something precedes the anchor (a domain part like `example.com##.ad` is
    // legitimate ABPF, so only a NON-domain prefix is a network filter). ABPF
    // puts domains before `##` as a comma-separated list of bare hosts, so a
    // prefix containing `/`, `:` or `*` is an address pattern, not domains.
    const std::string prefix = line.substr(0, anchor);
    if (prefix.find('/') != std::string::npos ||
        prefix.find(':') != std::string::npos ||
        prefix.find('*') != std::string::npos) {
      return AbpfError::kNetworkFilter;
    }
    // Otherwise the prefix is a domain list; record it.
    size_t start = 0;
    while (start <= prefix.size()) {
      size_t comma = prefix.find(',', start);
      std::string part =
          prefix.substr(start, comma == std::string::npos ? std::string::npos
                                                          : comma - start);
      if (!part.empty()) out->domains.push_back(part);
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
  }

  std::string rest = line.substr(anchor + (out->exception ? 3 : 2));
  if (rest.empty()) return AbpfError::kEmptySelector;

  // Split off `$`-options. The selector cannot contain a `$`, so the first `$`
  // is unambiguous.
  std::string selector = rest;
  std::string options;
  size_t dollar = rest.find('$');
  if (dollar != std::string::npos) {
    selector = rest.substr(0, dollar);
    options = rest.substr(dollar + 1);
  }
  if (selector.empty()) return AbpfError::kEmptySelector;
  out->selector = selector;

  // Parse options.
  size_t count = 0;
  size_t start = 0;
  while (start <= options.size()) {
    size_t comma = options.find(',', start);
    std::string opt =
        options.substr(start, comma == std::string::npos ? std::string::npos
                                                        : comma - start);
    if (comma == std::string::npos) {
      start = options.size() + 1;
    } else {
      start = comma + 1;
    }
    if (opt.empty()) continue;
    if (++count > kMaxAbpfOptions) return AbpfError::kTooManyOptions;

    std::string name = opt;
    std::string arg;
    // An option's name ends at the first '=' OR the first ':'. ABPF uses '='
    // for `$domain=…`, but the scriptlet family uses ':' — `$ext-abp-resource:
    // blank-css`. Splitting on '=' alone made the whole string the name, so a
    // refusal named the directive AND its argument, and `IsNetworkDirective`
    // was being asked about a string that could never match.
    size_t eq = opt.find_first_of("=:");
    if (eq != std::string::npos) {
      name = opt.substr(0, eq);
      arg = opt.substr(eq + 1);
    }
    if (name.empty()) return AbpfError::kMalformedOption;
    std::string lower = LowerCopy(name);

    // $domain is the one directive a cosmetic filter legitimately carries.
    if (lower == "domain") {
      size_t s = 0;
      while (s <= arg.size()) {
        size_t pipe = arg.find('|', s);
        std::string d = arg.substr(s, pipe == std::string::npos
                                          ? std::string::npos
                                          : pipe - s);
        if (!d.empty()) out->domains.push_back(d);
        if (pipe == std::string::npos) break;
        s = pipe + 1;
      }
      continue;
    }
    // $generichide/$elemhide change whether the GENERIC set applies, which is
    // a policy decision the browser owns — not a list. Refused.
    if (lower == "generichide" || lower == "elemhide" ||
        lower == "genericblock") {
      return AbpfError::kGenerichideDirective;
    }
    // A network directive on a cosmetic filter: refused, naming it.
    if (IsNetworkDirective(lower)) {
      out->error_detail = lower;
      return AbpfError::kDirectiveNotAllowed;
    }
    // $ext-… is the extension/scriptlet family. Recorded as a directive so the
    // caller can report it, and REFUSED here because scriptlet execution is off
    // in this phase — a validator that accepted it would imply it runs.
    if (StartsWith(lower, "ext-")) {
      out->directives.push_back({lower, arg});
      // Name the directive, not its argument: the list author needs to know
      // which directive to remove, and the argument can be arbitrary text.
      out->error_detail = name;
      return AbpfError::kDirectiveNotAllowed;
    }
    out->error_detail = lower;
    return AbpfError::kUnknownDirective;
  }

  // The selector itself is validated by the SAME parser the renderer uses, so a
  // filter this validator accepts is one the renderer can compile.
  Selector parsed;
  SelectorError se = ParseSelector(selector, &parsed);
  if (se != SelectorError::kOk) {
    out->error_detail = SelectorErrorName(se);
    return AbpfError::kSelectorRefused;
  }
  out->valid = true;
  return AbpfError::kOk;
}

}  // namespace xr::cosmetic
