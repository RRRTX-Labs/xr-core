// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/blob — the strict cosmetic-blob-v1 parser.
//
// "Strict" is the whole content of this suite. Each of the three laws in
// blob.h gets cases, and the digest law gets the one that matters most: a blob
// whose bytes were changed after signing must be refused, not applied. A parser
// that verifies a digest and then continues anyway is worse than one that does
// not verify at all, because it reports having checked.

#include "renderer/cosmetic/core/blob.h"

#include <string>

#include "harness.h"

namespace xrc = xr::cosmetic;
namespace {

// A minimal valid blob, built through the module's own serializer so the digest
// is correct by construction. Tests then mutate it and assert the refusal.
xrc::CosmeticBlob MakeBlob() {
  xrc::CosmeticBlob b;
  b.blob_id = "test-blob-1";
  b.generated_epoch = 1758000000;
  b.scope.site = "example.test";
  b.scope.identity_class = "anonymous";
  xrc::BlobRule r1;
  r1.id = "r1";
  r1.selector = "div > .ad";
  r1.action = "hide";
  xrc::BlobRule r2;
  r2.id = "r2";
  r2.selector = ".sponsor";
  r2.action = "remove";
  b.rules = {r1, r2};
  xrc::BlobRefusal f;
  f.rule_index = 9;
  f.reason = "unknown-pseudo-class";
  b.refusals = {f};
  b.sha256 = xrc::BlobDigest(b);
  return b;
}

std::string Good() { return xrc::BlobToCanonicalJson(MakeBlob()); }

void TestValidBlobParsesAndCompiles() {
  xrc::BlobResult out;
  XR_EXPECT_EQ(xrc::ParseBlob(Good(), nullptr, &out), xrc::BlobError::kOk);
  XR_EXPECT(out.valid);
  XR_EXPECT_STREQ(out.blob.blob_id, "test-blob-1");
  XR_EXPECT_EQ(out.blob.rules.size(), 2u);
  // The key set is compiled here, exactly once, by this module.
  XR_EXPECT(out.key_set.valid);
  XR_EXPECT_EQ(out.key_set.compiled_selectors, 2u);
  XR_EXPECT(out.key_set.bytes > 0);
  // The digest round-trips.
  XR_EXPECT_STREQ(out.blob.sha256, xrc::BlobDigest(out.blob));
}

void TestSchemaIsConst() {
  xrc::CosmeticBlob b = MakeBlob();
  // A wrong schema id is refused, never migrated.
  std::string json = xrc::BlobToCanonicalJson(b);
  size_t at = json.find("xr-cosmetic-blob-v1");
  XR_EXPECT(at != std::string::npos);
  std::string wrong = json.substr(0, at) + "xr-cosmetic-blob-v2" +
                      json.substr(at + 19);
  xrc::BlobResult out;
  XR_EXPECT_EQ(xrc::ParseBlob(wrong, nullptr, &out),
               xrc::BlobError::kSchemaMismatch);
  XR_EXPECT(!out.valid);

  // As is a wrong version.
  size_t vat = json.find("\"schema_version\":1");
  XR_EXPECT(vat != std::string::npos);
  std::string v2 = json.substr(0, vat) + "\"schema_version\":2" +
                   json.substr(vat + 18);
  xrc::BlobResult o3;
  XR_EXPECT_EQ(xrc::ParseBlob(v2, nullptr, &o3),
               xrc::BlobError::kSchemaMismatch);
  XR_EXPECT(!o3.valid);
}

void TestUnknownFieldDeny() {
  // THE LAW: an unrecognized key is a refusal, not an ignored key. Ignoring it
  // would let a producer ship a field this consumer has never seen and have it
  // silently do nothing.
  xrc::CosmeticBlob b = MakeBlob();
  std::string json = xrc::BlobToCanonicalJson(b);
  // Inject a new top-level key before the closing brace.
  std::string injected = json.substr(0, json.size() - 1) +
                         ",\"xr_future_field\":true}";
  xrc::BlobResult out;
  XR_EXPECT_EQ(xrc::ParseBlob(injected, nullptr, &out),
               xrc::BlobError::kUnknownField);
  XR_EXPECT(!out.valid);

  // Same for a rule-level key.
  std::string rule_injected = json.replace(json.find("\"selector\""), 0,
                                           "\"onclick\":\"alert(1)\",");
  xrc::BlobResult o2;
  // The digest now mismatches too, but unknown-field is checked first because
  // it is the more useful diagnosis.
  xrc::BlobError e = xrc::ParseBlob(rule_injected, nullptr, &o2);
  XR_EXPECT_MSG(e == xrc::BlobError::kUnknownField ||
                    e == xrc::BlobError::kMalformed,
                "an injected rule key must be refused");
  XR_EXPECT(!o2.valid);
}

void TestDigestIsVerifiedBeforeUse() {
  // THE LAW THAT MATTERS MOST. Tamper with a rule and the digest must catch it.
  xrc::CosmeticBlob b = MakeBlob();
  std::string json = xrc::BlobToCanonicalJson(b);
  size_t at = json.find("div > .ad");
  XR_EXPECT(at != std::string::npos);
  std::string tampered = json.substr(0, at) + "body" + json.substr(at + 9);
  xrc::BlobResult out;
  XR_EXPECT_EQ(xrc::ParseBlob(tampered, nullptr, &out),
               xrc::BlobError::kSha256Mismatch);
  XR_EXPECT(!out.valid);
  XR_EXPECT(out.key_set.rules.empty());

  // Tampering with the scope is caught the same way.
  std::string json2 = xrc::BlobToCanonicalJson(b);
  size_t sat = json2.find("example.test");
  std::string scope_tampered =
      json2.substr(0, sat) + "other.test" + json2.substr(sat + 12);
  xrc::BlobResult o2;
  XR_EXPECT_EQ(xrc::ParseBlob(scope_tampered, nullptr, &o2),
               xrc::BlobError::kSha256Mismatch);

  // A truncated digest is malformed, not a mismatch — the distinction tells an
  // operator whether the blob is corrupt or forged.
  std::string json3 = xrc::BlobToCanonicalJson(b);
  size_t dat = json3.rfind("\"sha256\":\"") + 10;
  std::string short_digest = json3.substr(0, dat) + "abcd" +
                             json3.substr(dat + 64);
  xrc::BlobResult o3;
  XR_EXPECT_EQ(xrc::ParseBlob(short_digest, nullptr, &o3),
               xrc::BlobError::kMalformed);
}

void TestScopeIsEnforced() {
  xrc::BlobScope frame;
  frame.site = "example.test";
  frame.identity_class = "anonymous";
  xrc::BlobResult out;
  XR_EXPECT_EQ(xrc::ParseBlob(Good(), &frame, &out), xrc::BlobError::kOk);

  // A blob for one site must not apply in another. The embedder is never an
  // input to this comparison.
  xrc::BlobScope other;
  other.site = "other.test";
  other.identity_class = "anonymous";
  xrc::BlobResult o2;
  XR_EXPECT_EQ(xrc::ParseBlob(Good(), &other, &o2),
               xrc::BlobError::kScopeMismatch);
  XR_EXPECT(!o2.valid);

  // Nor across identity classes on the same site.
  xrc::BlobScope ent;
  ent.site = "example.test";
  ent.identity_class = "enterprise";
  xrc::BlobResult o3;
  XR_EXPECT_EQ(xrc::ParseBlob(Good(), &ent, &o3),
               xrc::BlobError::kScopeMismatch);
}

void TestProducerRefusalsAreChecked() {
  // An unknown refusal reason means producer and consumer disagree about the
  // contract, and guessing is how a validator becomes permissive.
  xrc::CosmeticBlob b = MakeBlob();
  b.refusals[0].reason = "invented-reason";
  b.sha256 = xrc::BlobDigest(b);
  xrc::BlobResult out;
  XR_EXPECT_EQ(xrc::ParseBlob(xrc::BlobToCanonicalJson(b), nullptr, &out),
               xrc::BlobError::kUnknownRefusalReason);
  XR_EXPECT_EQ(out.failed_index, 9);

  // A known one is accepted and preserved, so a consumer can report an honest
  // coverage number instead of silently applying fewer rules.
  xrc::CosmeticBlob good = MakeBlob();
  xrc::BlobResult o2;
  XR_EXPECT_EQ(xrc::ParseBlob(xrc::BlobToCanonicalJson(good), nullptr, &o2),
               xrc::BlobError::kOk);
  XR_EXPECT_EQ(o2.blob.refusals.size(), 1u);
  XR_EXPECT_STREQ(o2.blob.refusals[0].reason, "unknown-pseudo-class");
}

void TestRuleLevelStrictness() {
  // Duplicate ids.
  xrc::CosmeticBlob b = MakeBlob();
  b.rules[1].id = "r1";
  b.sha256 = xrc::BlobDigest(b);
  xrc::BlobResult out;
  XR_EXPECT_EQ(xrc::ParseBlob(xrc::BlobToCanonicalJson(b), nullptr, &out),
               xrc::BlobError::kDuplicateRuleId);

  // An unparsable selector rejects the whole blob and names the index.
  xrc::CosmeticBlob bad = MakeBlob();
  bad.rules[1].selector = "*.bogus(";
  bad.sha256 = xrc::BlobDigest(bad);
  xrc::BlobResult o2;
  XR_EXPECT_EQ(xrc::ParseBlob(xrc::BlobToCanonicalJson(bad), nullptr, &o2),
               xrc::BlobError::kRuleRejected);
  XR_EXPECT_EQ(o2.failed_index, 1);

  // A `*:has(...)` selector is refused: a rule that can match the whole
  // document is a page-wide mutation channel.
  xrc::CosmeticBlob wide = MakeBlob();
  wide.rules[0].selector = "*:has(.ad)";
  wide.sha256 = xrc::BlobDigest(wide);
  xrc::BlobResult o3;
  XR_EXPECT_EQ(xrc::ParseBlob(xrc::BlobToCanonicalJson(wide), nullptr, &o3),
               xrc::BlobError::kRuleRejected);
  XR_EXPECT_STREQ(o3.reason, "universal-with-pseudo");

  // A style rule with a banned property is refused at parse time, not
  // discovered by the renderer at document-start.
  xrc::CosmeticBlob sty = MakeBlob();
  sty.rules[0].action = "style";
  sty.rules[0].style = {{"behavior", "url(#x)"}};
  sty.sha256 = xrc::BlobDigest(sty);
  xrc::BlobResult o4;
  XR_EXPECT_EQ(xrc::ParseBlob(xrc::BlobToCanonicalJson(sty), nullptr, &o4),
               xrc::BlobError::kRuleRejected);
  XR_EXPECT_STREQ(o4.reason, "unknown-css-property");
}

void TestMalformedInput() {
  const char* bad[] = {"", "not json", "[]", "\"a string\"", "null",
                       "{\"schema\":\"xr-cosmetic-blob-v1\"}"};
  for (const char* s : bad) {
    xrc::BlobResult out;
    xrc::BlobError e = xrc::ParseBlob(s, nullptr, &out);
    XR_EXPECT_MSG(e != xrc::BlobError::kOk,
                  (std::string("accepted malformed input: ") + s).c_str());
    XR_EXPECT(!out.valid);
  }
}

void TestErrorNamesDistinct() {
  const xrc::BlobError all[] = {
      xrc::BlobError::kOk, xrc::BlobError::kMalformed,
      xrc::BlobError::kSchemaMismatch, xrc::BlobError::kUnknownField,
      xrc::BlobError::kScopeMismatch, xrc::BlobError::kSha256Mismatch,
      xrc::BlobError::kTooManyRules, xrc::BlobError::kUnknownRefusalReason,
      xrc::BlobError::kDuplicateRuleId, xrc::BlobError::kRuleRejected,
      xrc::BlobError::kEmptyBlobId};
  for (size_t i = 0; i < 11; ++i) {
    for (size_t j = i + 1; j < 11; ++j) {
      XR_EXPECT_MSG(std::string(xrc::BlobErrorName(all[i])) !=
                        xrc::BlobErrorName(all[j]),
                    "two BlobError values share a name");
    }
  }
}

}  // namespace

int main() {
  TestValidBlobParsesAndCompiles();
  TestSchemaIsConst();
  TestUnknownFieldDeny();
  TestDigestIsVerifiedBeforeUse();
  TestScopeIsEnforced();
  TestProducerRefusalsAreChecked();
  TestRuleLevelStrictness();
  TestMalformedInput();
  TestErrorNamesDistinct();
  return xrtest::Report("test_blob_strict");
}
