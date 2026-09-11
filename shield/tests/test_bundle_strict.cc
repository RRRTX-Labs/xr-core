// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/bundle strictness (P11-T2) — the normalized
// xr-list-bundle-v1 parse (deny-on-unknown at every level), the v1 filter
// grammar incl. every refusal token, digest determinism, and the
// list-bundle-manifest-v1 binding (name/rule-count/sha256 per entry).
#include <string>

#include "common/core/json.h"
#include "common/core/sha256.h"
#include "shield/core/bundle.h"

#include "harness.h"

using namespace xr::shield;
using xr::common::ParseJson;
using xr::common::Sha256Hex;

namespace {

const char* kValid = R"({
  "schema":"xr-list-bundle","schema_version":1,"name":"xr-default",
  "bundle_version":3,
  "lists":[{"name":"l1","attribution":"CC-BY-3.0",
    "rules":[
      {"id":"r1","kind":"network","filter":"||tracker.example^","action":"block"},
      {"id":"r2","kind":"network","filter":"allow-literal","action":"allow"}]}],
  "refusals":[{"directive":"##.ad","reason":"unsupported-directive:cosmetic-hash","count":2}]})";

BundleResult Parse(const std::string& json, NormalizedBundle* b,
                   std::string* detail) {
  auto pr = ParseJson(json);
  if (!pr.ok) {
    *detail = "bad-json";
    return BundleResult::kMalformed;
  }
  return ParseBundle(pr.value, b, detail);
}

void ExpectRefusal(const std::string& json, BundleResult want,
                   const std::string& frag, const char* what) {
  NormalizedBundle b;
  std::string detail;
  BundleResult r = Parse(json, &b, &detail);
  XR_EXPECT_MSG(r == want, std::string(what) + " (result, detail='" + detail + "')");
  XR_EXPECT_MSG(detail.find(frag) != std::string::npos,
                std::string(what) + " (detail contains '" + frag + "')");
}

// replace the r2 rule's filter in kValid (the "allow-literal" allow rule)
std::string WithFilter(const std::string& filter_json_escaped) {
  std::string j = kValid;
  const std::string target = "allow-literal";
  j.replace(j.find(target), target.size(), filter_json_escaped);
  return j;
}

}  // namespace

int main() {
  // valid baseline
  NormalizedBundle b;
  std::string detail;
  {
    BundleResult r = Parse(kValid, &b, &detail);
    XR_EXPECT_MSG(r == BundleResult::kOk, std::string("valid bundle: ") + detail);
    XR_EXPECT(b.name == "xr-default");
    XR_EXPECT(b.bundle_version == 3);
    XR_EXPECT(b.lists.size() == 1);
    XR_EXPECT(b.lists[0].rules.size() == 2);
    XR_EXPECT(b.lists[0].rules[0].parsed.domain_anchor);
    XR_EXPECT(b.lists[0].rules[1].action == Action::kAllow);
    XR_EXPECT(b.refusals.size() == 1 && b.refusals[0].count == 2);
    XR_EXPECT(b.digest.size() == 64);
  }
  // top-level strictness
  ExpectRefusal(R"({"schema":"other","schema_version":1,"name":"n",)"
                R"("bundle_version":1,"lists":[]})",
                BundleResult::kMalformed, "wrong-schema", "wrong schema");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":2,"name":"n",)"
                R"("bundle_version":1,"lists":[]})",
                BundleResult::kMalformed, "wrong-schema-version", "schema_version 2");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":0,"lists":[]})",
                BundleResult::kMalformed, "non-positive-bundle-version", "version 0");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"",)"
                R"("bundle_version":1,"lists":[]})",
                BundleResult::kMalformed, "empty-name", "empty name");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":1,"lists":[],"signed_by":"x"})",
                BundleResult::kUnknownField, "unknown-field:signed_by",
                "unknown top-level key");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":1})",
                BundleResult::kMalformed, "lists-not-array",
                "lists required");
  // list + rule strictness
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":1,"lists":[{"name":"l","attribution":"",)"
                R"("rules":[],"license":"MIT"}]})",
                BundleResult::kUnknownField, "unknown-field:license",
                "unknown list key");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":1,"lists":[{"name":"l","attribution":"",)"
                R"("rules":[{"id":"r","kind":"network","filter":"||a.example^",)"
                R"("action":"block","priority":5}]}]})",
                BundleResult::kUnknownField, "unknown-field:priority",
                "unknown rule key");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":1,"lists":[{"name":"l","attribution":"",)"
                R"("rules":[{"id":"r","kind":"script","filter":"||a.example^",)"
                R"("action":"block"}]}]})",
                BundleResult::kUnknownField, "unknown-rule-kind", "unknown kind");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":1,"lists":[{"name":"l","attribution":"",)"
                R"("rules":[{"id":"r","kind":"network","filter":"||a.example^",)"
                R"("action":"block"},{"id":"r","kind":"network",)"
                R"("filter":"||b.example^","action":"block"}]}]})",
                BundleResult::kMalformed, "duplicate-rule-id:r", "dup rule id");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":1,"lists":[{"name":"l","attribution":"",)"
                R"("rules":[{"id":"r","kind":"redirect","filter":"||a.example^",)"
                R"("action":"redirect"}]}]})",
                BundleResult::kMalformed, "redirect-without-resource",
                "redirect needs resource");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":1,"lists":[{"name":"l","attribution":"",)"
                R"("rules":[{"id":"r","kind":"network","filter":"||a.example^",)"
                R"("action":"block","resource":"1x1.gif"}]}]})",
                BundleResult::kMalformed, "resource-without-redirect",
                "resource needs redirect");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":1,"lists":[{"name":"l","attribution":"",)"
                R"("rules":[{"id":"r","kind":"network","filter":"||a.example^",)"
                R"("action":"block","domains":"x"}]}]})",
                BundleResult::kMalformed, "domains-not-array:domains",
                "domains must be array");
  ExpectRefusal(R"({"schema":"xr-list-bundle","schema_version":1,"name":"n",)"
                R"("bundle_version":1,"lists":[{"name":"l","attribution":"",)"
                R"("rules":[{"id":"r","kind":"network","filter":"||a.example^",)"
                R"("action":"block","domains":[""]}]}]})",
                BundleResult::kMalformed, "bad-domain-entry:domains",
                "empty domain entry");

  // filter grammar: refusals (closed vocabulary tokens)
  struct FCase { const char* filter; const char* frag; };
  for (const FCase& fc : {
           FCase{"", "empty-filter"},
           FCase{"! comment", "unsupported-directive:comment"},
           FCase{"/re.gex/", "unsupported-directive:regex"},
           FCase{"||a.example^$third-party", "unsupported-directive:options-in-filter"},
           FCase{"##.advert", "unsupported-directive:cosmetic-hash"},
           FCase{"||a.example^$x", "unsupported-directive:options-in-filter"},
           FCase{"@@||a.example^", "unsupported-directive:at-syntax"},
           FCase{"a|b", "unsupported-directive:interior-pipe"},
           FCase{"|", "empty-filter"},
           FCase{"||", "empty-filter"},
       }) {
    NormalizedBundle bb;
    std::string d2;
    ParsedFilter pf;
    bool ok = ParseFilter(fc.filter, &pf, &d2);
    XR_EXPECT_MSG(!ok && d2.find(fc.frag) != std::string::npos,
                  std::string("filter refused '") + fc.filter + "' -> " + d2);
    // and the SAME filter inside a bundle refuses the bundle
    std::string esc = fc.filter;
    BundleResult r = Parse(WithFilter(esc), &bb, &d2);
    if (fc.filter[0] == '\0') {
      XR_EXPECT_MSG(r == BundleResult::kMalformed || r == BundleResult::kUnsupportedDirective,
                    "empty filter in bundle refused");
    } else {
      XR_EXPECT_MSG(r == BundleResult::kUnsupportedDirective &&
                        d2.find(fc.frag) != std::string::npos,
                    std::string("bundle refusal for '") + fc.filter + "': " + d2);
    }
  }
  // filter grammar: accepted shapes parse to the right structure
  {
    ParsedFilter pf;
    std::string d2;
    XR_EXPECT(ParseFilter("||tracker.example^", &pf, &d2) && pf.domain_anchor &&
              !pf.left_anchor && !pf.right_anchor && pf.segments.size() == 1 &&
              pf.segments[0] == std::string("tracker.example") + kSep);
    XR_EXPECT(ParseFilter("|https://a.example/x", &pf, &d2) && pf.left_anchor &&
              !pf.domain_anchor && pf.segments[0] == "https://a.example/x");
    XR_EXPECT(ParseFilter("||a.example/exact|", &pf, &d2) && pf.domain_anchor &&
              pf.right_anchor);
    XR_EXPECT(ParseFilter("||cdn.example*/ad_*.js", &pf, &d2) &&
              pf.segments.size() == 3 && pf.segments[0] == "cdn.example" &&
              pf.segments[1] == "/ad_" && pf.segments[2] == ".js");
    XR_EXPECT(ParseFilter("*", &pf, &d2) && pf.segments.size() == 2);
    XR_EXPECT(ParseFilter("plain-literal", &pf, &d2) && !pf.domain_anchor &&
              !pf.left_anchor && !pf.right_anchor);
    // "**" collapses (no interior empty segment refusal)
    XR_EXPECT(ParseFilter("a**b", &pf, &d2) && pf.segments.size() == 2);
    // "$" anywhere is refused even mid-literal; "^" becomes the sentinel
    XR_EXPECT(ParseFilter("a^b", &pf, &d2) &&
              pf.segments[0] == std::string("a") + kSep + "b");
  }

  // digest determinism + definition (sha256 over canonical list bytes)
  {
    NormalizedBundle b2;
    std::string d2;
    XR_EXPECT(Parse(kValid, &b2, &d2) == BundleResult::kOk);
    XR_EXPECT(b2.digest == b.digest);  // stable across parses
    std::string joined = "[" + ListCanonicalBytes(b2.lists[0]) + "]";
    XR_EXPECT(b2.digest == Sha256Hex(joined));
    // canonical bytes: sorted keys, no spaces (byte-parity target)
    XR_EXPECT(ListCanonicalBytes(b2.lists[0]).find("\": \"") == std::string::npos);
    XR_EXPECT(ListCanonicalBytes(b2.lists[0]).rfind("{\"attribution\":", 0) == 0);
  }

  // manifest binding (frozen list-bundle-manifest-v1 entry shape)
  {
    std::vector<ManifestListEntry> man;
    ManifestListEntry e;
    e.name = b.lists[0].name;
    e.sha256 = Sha256Hex(ListCanonicalBytes(b.lists[0]));
    e.rules = 2;
    man.push_back(e);
    std::string d2;
    XR_EXPECT_MSG(CheckAgainstManifest(b, man, &d2) == BundleResult::kOk,
                  std::string("binding ok: ") + d2);
    man[0].rules = 3;
    XR_EXPECT(CheckAgainstManifest(b, man, &d2) == BundleResult::kManifestMismatch &&
              d2 == "manifest-rule-count:l1");
    man[0].rules = 2;
    man[0].name = "other";
    XR_EXPECT(CheckAgainstManifest(b, man, &d2) == BundleResult::kManifestMismatch &&
              d2 == "manifest-list-name:other");
    man[0].name = "l1";
    man[0].sha256 = std::string(64, '0');
    XR_EXPECT(CheckAgainstManifest(b, man, &d2) == BundleResult::kManifestMismatch &&
              d2 == "manifest-sha256:l1");
    man[0].sha256 = Sha256Hex(ListCanonicalBytes(b.lists[0]));
    man.push_back(e);  // extra entry
    XR_EXPECT(CheckAgainstManifest(b, man, &d2) == BundleResult::kManifestMismatch &&
              d2 == "manifest-list-count");
  }

  // summary echo (vectors pin these bytes)
  {
    std::string s = BundleSummaryJson(b).Canonical();
    XR_EXPECT(s.find("\"bundle_version\":3") != std::string::npos);
    XR_EXPECT(s.find("\"rules\":2") != std::string::npos);
    XR_EXPECT(s.find("\"count\":2") != std::string::npos);
    XR_EXPECT(s.find("attribution") == std::string::npos);  // summary, not payload
  }
  return xrtest::Report("test_bundle_strict");
}
