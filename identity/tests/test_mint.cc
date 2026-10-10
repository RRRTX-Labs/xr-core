// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// mint suite (P14-T1): the opacity laws. A domain derived from a name, a
// site, a URL, or anything guessable is the identity-separation bug (P4
// census ruling); this suite proves the mint cannot produce one and that
// the probe corpus (partition name / title / log line / site) is rejected
// by LooksOpaque. This is the model half of security req 2 — the brute-
// force derivation probe, planted here so it can never silently pass.
#include <array>
#include <cstdint>
#include <string>

#include "harness.h"
#include "core/mint.h"

using xr::identity::FixtureDomain;
using xr::identity::kDomainLength;
using xr::identity::LooksOpaque;
using xr::identity::MintDomain;

int main() {
  // 1. Shape: "xr:" + UUID v4, version nibble 4, variant 8/9/a/b.
  std::string d1;
  XR_EXPECT_MSG(MintDomain("entropy-one", &d1), "mint succeeds");
  XR_EXPECT_EQ(d1.size(), size_t(kDomainLength));
  XR_EXPECT_MSG(d1.rfind("xr:", 0) == 0, "prefix xr:");
  XR_EXPECT_EQ(d1[17], '4');
  XR_EXPECT_MSG(d1[22] == '8' || d1[22] == '9' || d1[22] == 'a' ||
                    d1[22] == 'b', "variant nibble");

  // 2. Determinism: same entropy, same domain; different entropy differs.
  std::string d2;
  XR_EXPECT(MintDomain("entropy-one", &d2));
  XR_EXPECT_STREQ(d1.c_str(), d2.c_str());
  std::string d3;
  XR_EXPECT(MintDomain("entropy-two", &d3));
  XR_EXPECT_MSG(d1 != d3, "different entropy mints a different domain");

  // 3. Fail-closed: empty entropy refuses; nullptr out refuses.
  std::string out;
  XR_EXPECT(!MintDomain("", &out));
  XR_EXPECT(!MintDomain("x", nullptr));

  // 4. OPACITY (the law): the domain must not embed the entropy or any
  // guessable candidate. Brute-force probe corpus per security req 2.
  XR_EXPECT_MSG(LooksOpaque(d1, "entropy-one"), "no entropy in domain");
  for (const char* probe : {"work", "Work", "banking", "gmail.com",
                            "https://mail.example", "partition:work",
                            "Identity", "tab-title", "log-line"}) {
    XR_EXPECT_MSG(LooksOpaque(d1, probe), std::string("probe rejected: ") + probe);
  }
  // 4b. The negative shapes: a domain that LITERALLY embeds the guessable
  // string is rejected — plant the identity-from-name bug and prove the
  // checker catches it. (A hex-ENCODED name is shape-valid and undetectable
  // by containment — the LAW that prevents it is the mint's purity: the
  // domain is sha256(entropy), never a name; the host's entropy comes from
  // the OS, per identity/host_protocol.md.)
  XR_EXPECT_MSG(!LooksOpaque("xr:0000w0rk-0000-4000-8000-000000000001", "w0rk"),
                "name-embedded domain is REJECTED (identity-from-name bug)");
  XR_EXPECT_MSG(!LooksOpaque("work-identity", ""), "wrong shape rejected");
  XR_EXPECT_MSG(!LooksOpaque("xr:00000000-0000-3000-8000-000000000001", ""),
                "wrong version nibble rejected");
  XR_EXPECT_MSG(!LooksOpaque("xr:00000000-0000-4000-c000-000000000001", ""),
                "wrong variant rejected");
  XR_EXPECT_MSG(!LooksOpaque("xr:00000000-0000-4000-8000-000000000001x", ""),
                "41 chars rejected (only 39/40 are the frozen surface)");

  // 5. Fixture table: the exact frozen sequence (fakes/identity.py _MINTS).
  std::string f;
  XR_EXPECT_MSG(FixtureDomain(0, &f), "fixture 0");
  XR_EXPECT_STREQ(f.c_str(), "xr:00000000-0000-4000-8000-000000000001");
  XR_EXPECT(FixtureDomain(1, &f));
  XR_EXPECT_STREQ(f.c_str(), "xr:00000000-0000-4000-8000-000000000002");
  XR_EXPECT(FixtureDomain(2, &f));
  XR_EXPECT_STREQ(f.c_str(), "xr:00000000-0000-4000-8000-00000000000ef");
  XR_EXPECT_MSG(!FixtureDomain(3, &f),
                "beyond the table is refused, never silently reused");
  // Fixture domains are shape-valid (including the frozen 40-char third
  // entry — the quirk documented in mint.h; the vectors rely on it).
  XR_EXPECT_MSG(LooksOpaque("xr:00000000-0000-4000-8000-000000000001", "name"),
                "fixture domain is opaque-shaped");
  XR_EXPECT_MSG(LooksOpaque("xr:00000000-0000-4000-8000-00000000000ef", "name"),
                "frozen 40-char fixture entry is shape-valid (frozen quirk)");

  // 5b. P14-CLOSE C-4: short all-hex display names are chance, not embedding
  // (a user may name an identity "B"), but a long all-hex embed and any probe
  // with a non-hex character are still refused.
  XR_EXPECT_MSG(LooksOpaque("xr:b0000000-0000-4000-8000-000000000001", "B"),
                "a one-letter name inside the hex is chance, not an embed");
  XR_EXPECT_MSG(LooksOpaque("xr:cafe0000-0000-4000-8000-000000000001", "Cafe"),
                "a 4-char all-hex name is chance, not an embed");
  XR_EXPECT_MSG(!LooksOpaque("xr:deadbeef-0000-4000-8000-000000000001", "deadbeef"),
                "an 8-char all-hex embed is still refused");
  XR_EXPECT_MSG(!LooksOpaque("xr:00000000-0000-4000-8000-000000000001", "0000-4000"),
                "a 9-char all-hex embed (with '-') is still refused");

  // 6. The mint is a PURE function: no global state, calling order does not
  // change outputs (no hidden counter — the P4 census's RNG/counter ban).
  std::string a, b;
  XR_EXPECT(MintDomain("replay-me", &a));
  XR_EXPECT(MintDomain("other", &b));
  std::string a2;
  XR_EXPECT(MintDomain("replay-me", &a2));
  XR_EXPECT_STREQ(a.c_str(), a2.c_str());

  // 7. Known answers (computed independently: Python hashlib, first 16
  // bytes of sha256, version/variant forced). Pins the byte derivation, not
  // just its shape: a mint that read the wrong digest offsets would still
  // look like a UUID.
  std::string kat;
  XR_EXPECT(MintDomain("abc", &kat));
  XR_EXPECT_STREQ(kat.c_str(), "xr:ba7816bf-8f01-4fea-8141-40de5dae2223");
  XR_EXPECT(MintDomain("entropy-one", &kat));
  XR_EXPECT_STREQ(kat.c_str(), "xr:7a43ffae-3a4f-4c6e-8938-e5f47f15687a");

  // 8. The digest decoding refuses rather than guesses, and a refusal
  // leaves the output untouched.
  std::array<uint8_t, 16> bytes{};
  XR_EXPECT_MSG(xr::identity::DecodeDigestHex("0123456789abcdef0f1e2d3c4b5a6978", &bytes),
                "exactly 32 lowercase hex digits decode");
  const std::array<uint8_t, 16> want = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                        0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78};
  XR_EXPECT_MSG(bytes == want, "every digit 0-9a-f decodes to its value, in order");
  std::array<uint8_t, 16> keep = want;
  XR_EXPECT(!xr::identity::DecodeDigestHex("0123456789abcdef0f1e2d3c4b5a697", &keep));
  XR_EXPECT(!xr::identity::DecodeDigestHex("0123456789abcdef0f1e2d3c4b5a697g", &keep));
  XR_EXPECT(!xr::identity::DecodeDigestHex("g123456789abcdef0f1e2d3c4b5a6978", &keep));
  XR_EXPECT(!xr::identity::DecodeDigestHex("0123456789ABCDEF0f1e2d3c4b5a6978", &keep));
  XR_EXPECT(!xr::identity::DecodeDigestHex("01234567/9abcdef0f1e2d3c4b5a6978", &keep));
  XR_EXPECT(!xr::identity::DecodeDigestHex("01234567:9abcdef0f1e2d3c4b5a6978", &keep));
  XR_EXPECT(!xr::identity::DecodeDigestHex("0123456789abcdef0f1e2d3c4b5a6978", nullptr));
  XR_EXPECT_MSG(keep == want, "a refused decode leaves the output untouched");

  // 9. LooksOpaque edges: no probe judges the shape alone; a short probe of
  // '-' is chance (every domain has dashes); a short probe with a non-hex
  // character is judged, and "xr" is in every domain's prefix.
  const std::string dom = "xr:7a43ffae-3a4f-4c6e-8938-e5f47f15687a";
  XR_EXPECT_MSG(LooksOpaque(dom, ""), "an empty probe judges only the shape");
  XR_EXPECT_MSG(LooksOpaque(dom, "-"), "a lone dash is chance, not an embed");
  XR_EXPECT_MSG(LooksOpaque(dom, "a-3"), "a short hex-and-dash probe is chance");
  XR_EXPECT_MSG(!LooksOpaque(dom, "xr"), "a short non-hex probe is still judged");
  XR_EXPECT_MSG(!LooksOpaque(dom, "XR:7"), "judging is case-insensitive");

  return xrtest::Report("identity/mint");
}
