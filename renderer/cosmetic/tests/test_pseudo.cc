// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/pseudo — the `#?#` procedural pseudo-class
// table (P12-T1).
//
// The table's whole purpose is the distinction between "the vendored engine can
// express this" and "we allow this". So the suite's central job is to prove the
// distinction is REAL and not decoration:
//
//   * every entry's citation resolves to a real line in the sealed vendored
//     tree (checked by a separate Python lane; this suite asserts the shape of
//     the citation so that lane has something to check);
//   * every REFUSED entry names a reason from the closed support vocabulary;
//   * an entry that claims kAdmitted but has no capability note is a lie about
//     what it costs, so that is a failure;
//   * the ABP alias map canonicalizes to a name that EXISTS in the table — an
//     alias pointing at nothing would make the parser accept a rule the engine
//     then refuses.

#include "renderer/cosmetic/core/pseudo.h"

#include <string>
#include <vector>

#include "harness.h"

namespace xrc = xr::cosmetic;
namespace {

int g_admitted = 0;
int g_refused = 0;

void TestVocabularyIsClosedAndDistinct() {
  const xrc::PseudoCapability caps[] = {
      xrc::PseudoCapability::kNone, xrc::PseudoCapability::kTextContent,
      xrc::PseudoCapability::kSubtreeQuery,
      xrc::PseudoCapability::kAttributeMatch,
      xrc::PseudoCapability::kDocumentUrl,
      xrc::PseudoCapability::kAncestorWalk,
      xrc::PseudoCapability::kDomRemoval,
      xrc::PseudoCapability::kMainWorld};
  for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); ++i) {
    const char* n = xrc::PseudoCapabilityName(caps[i]);
    XR_EXPECT_MSG(n && n[0], "every PseudoCapability needs a name");
    for (size_t j = i + 1; j < sizeof(caps) / sizeof(caps[0]); ++j) {
      XR_EXPECT_MSG(std::string(n) != xrc::PseudoCapabilityName(caps[j]),
                    "two PseudoCapability values share a name");
    }
  }

  const xrc::PseudoSupport sups[] = {
      xrc::PseudoSupport::kAdmitted, xrc::PseudoSupport::kRefusedNoCssom,
      xrc::PseudoSupport::kRefusedUnbounded,
      xrc::PseudoSupport::kRefusedNotInEngine};
  for (size_t i = 0; i < sizeof(sups) / sizeof(sups[0]); ++i) {
    const char* n = xrc::PseudoSupportName(sups[i]);
    XR_EXPECT_MSG(n && n[0], "every PseudoSupport needs a name");
    for (size_t j = i + 1; j < sizeof(sups) / sizeof(sups[0]); ++j) {
      XR_EXPECT_MSG(std::string(n) != xrc::PseudoSupportName(sups[j]),
                    "two PseudoSupport values share a name");
    }
  }
}

void TestTableEntriesAreComplete() {
  const auto& table = xrc::PseudoTable();
  // The zero-case law: an empty table would make every lookup fail, which is
  // safe but useless — and would silently mean "no procedural pseudo-classes
  // at all" without anyone deciding that.
  XR_EXPECT_MSG(table.size() >= 10,
                "the pseudo table is suspiciously small");

  std::vector<std::string> seen;
  for (const xrc::PseudoInfo& info : table) {
    std::string name = info.name;
    XR_EXPECT_MSG(!name.empty(), "a table entry has an empty name");
    XR_EXPECT_MSG(name[0] != ':',
                  "pseudo names are stored without the leading ':'");
    for (char c : name) {
      XR_EXPECT_MSG((c >= 'a' && c <= 'z') || c == '-',
                    "pseudo names must be lowercase ascii");
    }
    // No duplicate names: two entries would make lookup order-dependent.
    for (const std::string& s : seen) {
      XR_EXPECT_MSG(s != name, "duplicate pseudo name in the table");
    }
    seen.push_back(name);

    // The citation is the proof, so its SHAPE is load-bearing: it must name a
    // file and a line, or say explicitly that the operator is absent from the
    // vendored set. A vague citation is an unverifiable claim.
    std::string cite = info.citation ? info.citation : "";
    bool looks_real =
        cite.find(".rs:") != std::string::npos ||
        cite.find("NOT in ") == 0 ||
        cite.find("native CSS") != std::string::npos;
    XR_EXPECT_MSG(looks_real,
                  (name + ": citation must cite a file:line or state absence")
                      .c_str());
    XR_EXPECT_MSG(info.note && info.note[0],
                  (name + ": every entry needs a note explaining the decision")
                      .c_str());

    if (info.support == xrc::PseudoSupport::kAdmitted) {
      ++g_admitted;
      // An admitted entry must say what it COSTS. "admitted, no capability"
      // would understate what the renderer has to do.
      XR_EXPECT_MSG(info.capability != xrc::PseudoCapability::kNone ||
                        name == "has-text",
                    (name + ": admitted but declares no capability").c_str());
      // The engine can express an admitted entry only if the citation is not
      // the "absent from the vendored set" form.
      XR_EXPECT_MSG(cite.find("NOT in ") != 0,
                    (name + ": admitted but cited as absent from the engine")
                        .c_str());
    } else {
      ++g_refused;
      // A refusal must say WHY, in the note, not just carry an enum.
      XR_EXPECT_MSG(std::string(info.note).find("REFUSED") !=
                        std::string::npos,
                    (name + ": a refused entry must explain the refusal")
                        .c_str());
    }
  }

  // The distinction must be populated on BOTH sides, or the table is not
  // expressing a decision — it is just a list.
  XR_EXPECT_MSG(g_admitted >= 4, "too few admitted entries to be a real table");
  XR_EXPECT_MSG(g_refused >= 5,
                "too few refused entries — the table is not refusing anything, "
                "which means the engine's capabilities were copied wholesale");
}

void TestSpecificDecisions() {
  // The decisions that a reviewer would check by hand, pinned as assertions so
  // a future edit cannot quietly reverse one.
  const xrc::PseudoInfo* xpath = xrc::LookupPseudo("xpath");
  XR_EXPECT(xpath != nullptr);
  if (xpath) {
    XR_EXPECT(xpath->support == xrc::PseudoSupport::kRefusedNoCssom);
  }

  const xrc::PseudoInfo* has = xrc::LookupPseudo("has");
  XR_EXPECT(has != nullptr);
  if (has) {
    XR_EXPECT(has->support == xrc::PseudoSupport::kAdmitted);
    XR_EXPECT(has->arg_is_selector);
  }

  const xrc::PseudoInfo* remove = xrc::LookupPseudo("remove");
  XR_EXPECT(remove != nullptr);
  if (remove) {
    // :remove() is page-modifying. The Observatory must label it as such — an
    // injected/removed element is not a blocked request.
    XR_EXPECT(xrc::IsPageModifying(*remove));
  }

  const xrc::PseudoInfo* htext = xrc::LookupPseudo("has-text");
  XR_EXPECT(htext != nullptr);
  if (htext) {
    XR_EXPECT(!xrc::IsPageModifying(*htext));
    XR_EXPECT(!htext->arg_is_selector);  // a substring, not a selector
  }

  // An unlisted name returns nullptr, which the caller must refuse.
  XR_EXPECT(xrc::LookupPseudo("xr-not-a-pseudo") == nullptr);
  XR_EXPECT(xrc::LookupPseudo("") == nullptr);
}

void TestAliases() {
  // The engine honours exactly three aliases (cosmetic.rs:978-980). An alias
  // that canonicalizes to a name NOT in the table would make the parser accept
  // a rule the engine then refuses.
  const char* aliases[] = {"-abp-has", "-abp-contains", "contains"};
  for (const char* a : aliases) {
    const char* canon = xrc::CanonicalizePseudoAlias(a);
    XR_EXPECT_MSG(canon && canon[0],
                  (std::string(a) + " should canonicalize").c_str());
    if (canon) {
      XR_EXPECT_MSG(xrc::LookupPseudo(canon) != nullptr,
                    (std::string(a) + " canonicalizes to '" + canon +
                     "', which is not in the table")
                        .c_str());
    }
  }

  // Non-aliases must not be invented. CanonicalizePseudoAlias("") == "" and an
  // arbitrary name returns "" rather than guessing.
  XR_EXPECT_STREQ(xrc::CanonicalizePseudoAlias("has"), "");
  XR_EXPECT_STREQ(xrc::CanonicalizePseudoAlias("xpath"), "");
  XR_EXPECT_STREQ(xrc::CanonicalizePseudoAlias(""), "");
  XR_EXPECT_STREQ(xrc::CanonicalizePseudoAlias("-abp-xpath"), "");
}

void TestNativeAllowlist() {
  // The engine passes ANY unknown pseudo-class through as AnythingElse
  // (cosmetic.rs:1184), so our allowlist is the only thing bounding the set.
  XR_EXPECT(xrc::IsAdmittedNativePseudo("first-child"));
  XR_EXPECT(xrc::IsAdmittedNativePseudo("nth-of-type"));
  XR_EXPECT(xrc::IsAdmittedNativePseudo("empty"));
  // ...and it must actually bound it.
  XR_EXPECT(!xrc::IsAdmittedNativePseudo("xr-invented"));
  XR_EXPECT(!xrc::IsAdmittedNativePseudo(""));
  XR_EXPECT(!xrc::IsAdmittedNativePseudo("FIRST-CHILD"));  // case-sensitive
  XR_EXPECT(!xrc::IsAdmittedNativePseudo("before"));       // a pseudo-ELEMENT
}

}  // namespace

int main() {
  TestVocabularyIsClosedAndDistinct();
  TestTableEntriesAreComplete();
  TestSpecificDecisions();
  TestAliases();
  TestNativeAllowlist();

  XR_EXPECT_MSG(g_admitted > 0 && g_refused > 0,
                "the table must both admit and refuse");
  std::printf("  pseudo table: %zu entries, %d admitted, %d refused\n",
              xrc::PseudoTable().size(), g_admitted, g_refused);
  return xrtest::Report("test_pseudo");
}
