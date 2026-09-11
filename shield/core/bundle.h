// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/bundle — parse/validate a NORMALIZED list bundle
// (P11-T2). Normalized = the structured form xr-lists/compile.py (T3)
// emits from ABP/uBO syntax: the engine never parses filter-list TEXT
// syntax options; anything the compiler could not normalize arrives here
// as a refusal-table entry, and anything malformed/unknown in the
// structured form is REFUSED (deny-on-unknown, the P6/P8 house pattern).
//
// Living shape (xr-list-bundle-v1; pinned by the golden vectors):
//   {"schema":"xr-list-bundle","schema_version":1,"name":str,
//    "bundle_version":int>0,
//    "lists":[{"name":str,"attribution":str,
//              "rules":[{"id":str,"kind":"network"|"cosmetic"|"redirect"|
//                               "resource",
//                        "filter":str,"action":"block"|"allow"|"redirect"|
//                                   "replace",
//                        "resource":str?,           // redirect/replace only
//                        "domains":[str]?,          // include list
//                        "exclude_domains":[str]?}]}],
//    "refusals":[{"directive":str,"reason":str,"count":int>=0}]}
//
// Manifest binding: the FROZEN list-bundle-manifest-v1 lists[] entries are
// {name, sha256, rules}; CheckAgainstManifest re-binds a parsed bundle to
// those entries (name set, per-list rule count, per-list canonical-bytes
// sha256). The frozen schema's lists[] ITEMS are unconstrained, which is
// where T3's attribution rides (inside the bundle's list entries and thus
// inside the sha256) — recorded in the T3 notes, no schema touch.
#pragma once

#include <string>
#include <vector>

#include "common/core/json.h"
#include "shield/core/engine.h"

namespace xr::shield {

// v1 filter grammar (documented subset; anything else is a refusal):
//   "||" domain-anchor prefix · "|" left/right anchor · "*" wildcard ·
//   "^" separator · literals. NO "$options" in filter text (options are
//   structured fields), NO /regex/, NO "!" comments, NO cosmetic syntax in
//   network rules. Matched against scheme://host/path (query/fragment
//   stripped — the same redaction events use).
// The "^" separator sentinel inside parsed literals (0x01 cannot occur in
// filter text or URLs — it is the engine-internal encoding of the
// separator class "/ : ? # end-of-url"; on the v1 match surface, where
// query/fragment/port are already stripped, it matches "/" ":" or
// end-of-string).
inline constexpr char kSep = '\x01';

struct ParsedFilter {
  bool domain_anchor = false;   // "||" prefix
  bool left_anchor = false;     // "|" prefix (after the optional "||")
  bool right_anchor = false;    // "|" suffix
  // The body split on '*': segments[0] matches at the anchor position (or
  // anywhere when unanchored), each later segment must appear after the
  // previous with an arbitrary gap; the last must END the match URL when
  // right_anchor. '^' is the separator class (/: ? # end-of-url), encoded
  // as the 0x01 sentinel inside segments. Empty segments are legal at the
  // edges (a leading/trailing '*'); interior empties ("**") collapse.
  std::vector<std::string> segments;
};

struct Rule {
  std::string id;
  std::string kind;    // network | cosmetic | redirect | resource
  std::string filter;  // raw (as compiled)
  ParsedFilter parsed;
  Action action = Action::kBlock;
  std::string resource;  // redirect/replace only, else ""
  std::vector<std::string> domains;         // include list ("" entries refused)
  std::vector<std::string> exclude_domains;
};

struct BundleList {
  std::string name;
  std::string attribution;  // T3: per-list attribution, inside the digest
  std::vector<Rule> rules;
};

struct BundleRefusal {
  std::string directive;
  std::string reason;
  long long count = 0;
};

struct NormalizedBundle {
  std::string name;
  long long bundle_version = 0;
  std::vector<BundleList> lists;
  std::vector<BundleRefusal> refusals;
  std::string digest;  // sha256 over the canonical lists bytes (below)
};

enum class BundleResult {
  kOk = 0,
  kMalformed,
  kUnknownField,
  kUnsupportedDirective,  // filter grammar outside the v1 subset
  kManifestMismatch,      // CheckAgainstManifest binding failure
};

// Strict parse. `raw` is the bundle document. Refusals: unknown/missing
// fields, wrong types, empty ids/names, duplicate rule ids, non-positive
// bundle_version, action/resource inconsistency, unparsable filters (each
// reported in `detail` as a closed-vocabulary token, mirrored by the fake).
BundleResult ParseBundle(const common::JsonValue& raw, NormalizedBundle* out,
                         std::string* detail);

// Parse just a filter (exposed for tests + the fake's byte-parity).
bool ParseFilter(const std::string& filter, ParsedFilter* out,
                 std::string* detail);

// The canonical bytes a manifest list entry's sha256 covers: the canonical
// JSON of {"attribution":…,"name":…,"rules":[…]} per list (sorted keys,
// the shared serializer — one definition, both backends).
std::string ListCanonicalBytes(const BundleList& list);

// Manifest binding (frozen list-bundle-manifest-v1 entries).
struct ManifestListEntry {
  std::string name;
  std::string sha256;
  long long rules = 0;
  // T3: the frozen schema leaves lists[] entries free-form, so the
  // pipeline embeds the per-list attribution here. Optional in the entry
  // grammar; when PRESENT it must equal the bundle list's attribution
  // (which the digest binds) — a manifest may not claim attribution
  // other than the one the bytes it pins actually carry.
  bool has_attribution = false;
  std::string attribution;
};
BundleResult CheckAgainstManifest(const NormalizedBundle& bundle,
                                  const std::vector<ManifestListEntry>& man,
                                  std::string* detail);

// Canonical echo for vectors/evidence: name, bundle_version, digest,
// per-list rule counts, refusal summary — NOT the whole rule set (vectors
// pin decisions, not payloads).
common::JsonValue BundleSummaryJson(const NormalizedBundle& bundle);

}  // namespace xr::shield
