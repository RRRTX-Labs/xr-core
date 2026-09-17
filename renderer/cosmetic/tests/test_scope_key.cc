// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/scope_key — the cache/identity key derivation
// (P12-T2).
//
// This suite tests the one part of the phase where a wrong answer is a SECURITY
// failure rather than a cosmetic one. The properties below are the properties
// the brief's Tests row demands ("OOPIF cosmetic applies in frame's partition,
// not embedder's"), stated as assertions rather than prose:
//
//   * the same frame site under two identities yields two different keys;
//   * the same identity at two trust levels yields two different partitions;
//   * passing an embedder site is REFUSED, not silently ignored — the
//     "key on the top-level site only" optimization must be structurally
//     impossible, not merely discouraged;
//   * a same-document (SPA) navigation yields a different key than the initial
//     load, so the cache can tell them apart;
//   * a malformed site is refused rather than normalized away.
//
// Determinism is asserted by deriving twice and comparing bytes, because a key
// that varies between runs would fragment the cache and make every measurement
// of it meaningless.

#include "renderer/cosmetic/core/scope_key.h"

#include <string>

#include "harness.h"

namespace xrc = xr::cosmetic;
namespace {

xrc::ScopeInput MakeInput(const std::string& site = "ads.example",
                          const std::string& identity = "alice") {
  xrc::ScopeInput in;
  in.frame_site = site;
  in.frame_identity = identity;
  in.trust = xrc::IdentityTrust::kAuthenticated;
  in.navigation = xrc::NavigationClass::kInitial;
  in.document_url_class = "article";
  return in;
}

void TestDeterministic() {
  xrc::ScopeKey a, b;
  XR_EXPECT_EQ(xrc::DeriveScopeKey(MakeInput(), "", &a), xrc::ScopeError::kOk);
  XR_EXPECT_EQ(xrc::DeriveScopeKey(MakeInput(), "", &b), xrc::ScopeError::kOk);
  XR_EXPECT(a.valid && b.valid);
  XR_EXPECT_STREQ(a.hex, b.hex);
  XR_EXPECT_STREQ(a.partition, b.partition);
  // 32 bytes of sha256, hex-encoded.
  XR_EXPECT_EQ(a.hex.size(), 64u);
  XR_EXPECT_EQ(a.partition.size(), 64u);
}

void TestIdentityPartitionsTheKey() {
  // THE cache-partition law: the same <iframe> from site S under identity A vs
  // identity B must not share a key. Sharing one would serve A's rule set to B.
  xrc::ScopeKey alice, bob;
  XR_EXPECT_EQ(xrc::DeriveScopeKey(MakeInput("ads.example", "alice"), "", &alice),
               xrc::ScopeError::kOk);
  XR_EXPECT_EQ(xrc::DeriveScopeKey(MakeInput("ads.example", "bob"), "", &bob),
               xrc::ScopeError::kOk);
  XR_EXPECT_MSG(alice.hex != bob.hex,
                "two identities on one site produced the SAME key");
  XR_EXPECT_MSG(alice.partition != bob.partition,
                "two identities produced the same partition");
}

void TestTrustLevelPartitions() {
  // The same identity string at a different trust level is a different
  // partition: an unauthenticated context must not inherit an authenticated
  // identity's rule set just because the string matches.
  xrc::ScopeInput anon = MakeInput();
  anon.trust = xrc::IdentityTrust::kAnonymous;
  xrc::ScopeInput ent = MakeInput();
  ent.trust = xrc::IdentityTrust::kEnterprise;
  xrc::ScopeKey a, e;
  XR_EXPECT_EQ(xrc::DeriveScopeKey(MakeInput(), "", &a), xrc::ScopeError::kOk);
  XR_EXPECT_EQ(xrc::DeriveScopeKey(anon, "", &e), xrc::ScopeError::kOk);
  XR_EXPECT_MSG(a.partition != e.partition,
                "anonymous and authenticated share a partition");
  xrc::ScopeKey e2;
  XR_EXPECT_EQ(xrc::DeriveScopeKey(ent, "", &e2), xrc::ScopeError::kOk);
  XR_EXPECT_MSG(a.partition != e2.partition,
                "authenticated and enterprise share a partition");

  // And the partition function itself must agree with the key's partition.
  XR_EXPECT_STREQ(xrc::IdentityPartition("alice",
                                         xrc::IdentityTrust::kAuthenticated),
                  a.partition);
}

void TestEmbedderIsRefused() {
  // The security rule, enforced structurally. A caller that passes an embedder
  // is refused rather than silently corrected: "we noticed you tried to key on
  // the top-level site and fixed it for you" is how a leak survives a refactor.
  xrc::ScopeKey k;
  XR_EXPECT_EQ(xrc::DeriveScopeKey(MakeInput(), "top.example", &k),
               xrc::ScopeError::kMalformedSite);
  XR_EXPECT(!k.valid);
  XR_EXPECT(k.hex.empty());

  // The empty embedder is the only accepted form.
  XR_EXPECT_EQ(xrc::DeriveScopeKey(MakeInput(), "", &k), xrc::ScopeError::kOk);
  XR_EXPECT(k.valid);
}

void TestNavigationClasses() {
  xrc::ScopeInput spa = MakeInput();
  spa.navigation = xrc::NavigationClass::kSameDocument;
  xrc::ScopeKey initial, same;
  XR_EXPECT_EQ(xrc::DeriveScopeKey(MakeInput(), "", &initial),
               xrc::ScopeError::kOk);
  XR_EXPECT_EQ(xrc::DeriveScopeKey(spa, "", &same), xrc::ScopeError::kOk);
  XR_EXPECT_MSG(initial.hex != same.hex,
                "an SPA push must not collide with the initial load");

  // A cross-document navigation encodes identically to the initial load: the
  // rule set is site-scoped, so the distinction the cache needs is SPA vs not.
  xrc::ScopeInput cross = MakeInput();
  cross.navigation = xrc::NavigationClass::kCrossDocument;
  xrc::ScopeKey c;
  XR_EXPECT_EQ(xrc::DeriveScopeKey(cross, "", &c), xrc::ScopeError::kOk);
  XR_EXPECT_STREQ(c.hex, initial.hex);
}

void TestSiteValidation() {
  xrc::ScopeKey k;
  struct {
    const char* site;
    xrc::ScopeError want;
  } cases[] = {
      {"", xrc::ScopeError::kEmptySite},
      {"HTTPS://Ads.Example/x", xrc::ScopeError::kMalformedSite},
      {"ads.example:8080", xrc::ScopeError::kMalformedSite},
      {"ads.example/path", xrc::ScopeError::kMalformedSite},
      {"ads.example?q=1", xrc::ScopeError::kMalformedSite},
      {"user@ads.example", xrc::ScopeError::kMalformedSite},
      {"Ads.Example", xrc::ScopeError::kMalformedSite},  // uppercase refused
      {".ads.example", xrc::ScopeError::kMalformedSite},
      {"ads.example.", xrc::ScopeError::kMalformedSite},
      {"ads..example", xrc::ScopeError::kMalformedSite},
      {"ads.example", xrc::ScopeError::kOk},
      {"a", xrc::ScopeError::kOk},
  };
  for (const auto& c : cases) {
    xrc::ScopeInput in = MakeInput();
    in.frame_site = c.site;
    xrc::ScopeError got = xrc::DeriveScopeKey(in, "", &k);
    XR_EXPECT_MSG(got == c.want,
                  (std::string("site '") + c.site + "' -> " +
                   xrc::ScopeErrorName(got) + ", expected " +
                   xrc::ScopeErrorName(c.want))
                      .c_str());
  }

  // Length bounds.
  xrc::ScopeInput long_site = MakeInput();
  long_site.frame_site = std::string(xrc::kMaxSiteLen + 1, 'a');
  XR_EXPECT_EQ(xrc::DeriveScopeKey(long_site, "", &k),
               xrc::ScopeError::kSiteTooLong);
  xrc::ScopeInput long_id = MakeInput();
  long_id.frame_identity = std::string(xrc::kMaxIdentityLen + 1, 'a');
  XR_EXPECT_EQ(xrc::DeriveScopeKey(long_id, "", &k),
               xrc::ScopeError::kIdentityTooLong);
  xrc::ScopeInput no_class = MakeInput();
  no_class.document_url_class = "";
  XR_EXPECT_EQ(xrc::DeriveScopeKey(no_class, "", &k),
               xrc::ScopeError::kEmptyDocumentClass);
}

void TestNoCollisionAcrossFields() {
  // The canonical encoding uses a 0x01 separator precisely so ("a.b","c") and
  // ("a","b.c") cannot collide. Assert it.
  xrc::ScopeInput x = MakeInput("a.b", "c");
  xrc::ScopeInput y = MakeInput("a", "b.c");
  xrc::ScopeKey kx, ky;
  XR_EXPECT_EQ(xrc::DeriveScopeKey(x, "", &kx), xrc::ScopeError::kOk);
  XR_EXPECT_EQ(xrc::DeriveScopeKey(y, "", &ky), xrc::ScopeError::kOk);
  XR_EXPECT_MSG(kx.hex != ky.hex, "field-boundary collision in the key");

  // A site whose name contains the separator must not be constructible.
  xrc::ScopeInput sep = MakeInput();
  sep.frame_site = std::string("a") + '\x01' + "b";
  xrc::ScopeKey ks;
  XR_EXPECT_MSG(xrc::DeriveScopeKey(sep, "", &ks) != xrc::ScopeError::kOk,
                "a site containing the separator must be refused");
}

void TestErrorNames() {
  const xrc::ScopeError all[] = {
      xrc::ScopeError::kOk, xrc::ScopeError::kEmptySite,
      xrc::ScopeError::kSiteTooLong, xrc::ScopeError::kIdentityTooLong,
      xrc::ScopeError::kMalformedSite,
      xrc::ScopeError::kEmptyDocumentClass};
  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
    const char* n = xrc::ScopeErrorName(all[i]);
    XR_EXPECT_MSG(n && n[0], "every ScopeError needs a name");
    for (size_t j = i + 1; j < sizeof(all) / sizeof(all[0]); ++j) {
      XR_EXPECT_MSG(std::string(n) != xrc::ScopeErrorName(all[j]),
                    "two ScopeError values share a name");
    }
  }
  XR_EXPECT_STREQ(xrc::NavigationClassName(xrc::NavigationClass::kInitial),
                  "initial");
  XR_EXPECT_STREQ(
      xrc::NavigationClassName(xrc::NavigationClass::kSameDocument),
      "same-document");
  XR_EXPECT_STREQ(xrc::IdentityTrustName(xrc::IdentityTrust::kAnonymous),
                  "anonymous");
}

}  // namespace

int main() {
  TestDeterministic();
  TestIdentityPartitionsTheKey();
  TestTrustLevelPartitions();
  TestEmbedderIsRefused();
  TestNavigationClasses();
  TestSiteValidation();
  TestNoCollisionAcrossFields();
  TestErrorNames();
  return xrtest::Report("test_scope_key");
}
