// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — the SHARED SHA-256 known-answer test (P11-T0-b). ONE
// source, compiled into EVERY consumer suite (policy, commands, settings,
// themes, update, shield, common) by each suite's Makefile, so KAT coverage
// can never desynchronize again (before T0-b: five byte-identical sha256.cc
// copies, and the FIPS vectors pinned exactly one of them, in
// policy/tests/test_vectors.cc — four copies no test owned).
//
// Vector provenance (verified 2026-09-11):
//   * empty / "abc" / 56-byte / 112-byte: published FIPS 180-4 / NIST CAVP
//     values (they match the digests printed in the NIST SHS examples).
//   * 55/64/65/114-byte padding-boundary, 1 MiB and 64 MiB streams:
//     committed goldens computed with Python hashlib.sha256 AND coreutils
//     sha256sum in the same run — the two agreed byte-for-byte on every
//     value before commit (dual-tool provenance, never one tool's word).
//   * the 64 MiB case is the brief's "long input >= 2^29 bits" length-field
//     case (67,108,864 bytes = exactly 2^29 bits). The 64-bit length HIGH
//     word only becomes non-zero at >= 2^32 bits (512 MiB); that case is
//     deliberately NOT run in-suite (RAM/time across 7 binaries) and is
//     recorded here rather than silently omitted — the high word is a
//     constant 0 for every input this tree hashes in practice.
//
// Zero dependencies beyond common/core/sha256.h + <string>/<cstdio>.
#include <cstdio>
#include <string>

#include "common/core/sha256.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void ExpectHex(const std::string& data, const char* want, const char* what) {
  ++g_checks;
  const std::string got = xr::common::Sha256Hex(data);
  if (got != want) {
    ++g_failures;
    std::fprintf(stderr, "  FAILED %s: got %s want %s\n", what, got.c_str(),
                 want);
  }
}

}  // namespace

int main() {
  // 1. empty string (FIPS 180-4 published).
  ExpectHex("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b"
                "7852b855", "empty");
  // 2. "abc" (FIPS 180-4 published).
  ExpectHex("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410f"
                   "f61f20015ad", "abc");
  // 3. 55 bytes — the longest single-block message (padding boundary).
  ExpectHex(std::string(55, 'a'),
            "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f7343"
            "18", "55-byte boundary");
  // 4. 56 bytes — the standard two-block message (FIPS published).
  ExpectHex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06"
            "c1", "56-byte standard");
  // 5. 64 bytes — exactly one full block of data (length lands in block 2).
  ExpectHex(std::string(64, 'a'),
            "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668"
            "eb", "64-byte");
  // 6. 65 bytes — one byte past the block boundary.
  ExpectHex(std::string(65, 'a'),
            "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0a"
            "e0", "65-byte");
  // 7. 112 bytes — the standard multi-block message (FIPS published).
  ExpectHex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijkl"
            "mnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
            "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9"
            "d1", "112-byte standard");
  // 8. 114 bytes — multi-block, non-aligned tail.
  ExpectHex(std::string(114, 'a'),
            "6cb750b2181606511f4a7e4786b53692f51a9cdc4d3172fdcfb9267aeca7e4"
            "e4", "114-byte multi-block");
  // 9. 1 MiB stream of 'a' (committed golden; hashlib == sha256sum).
  ExpectHex(std::string(1024 * 1024, 'a'),
            "9bc1b2a288b26af7257a36277ae3816a7d4f16e89c1e7e77d0a5c48bad62b3"
            "60", "1 MiB stream");
  // 10. 64 MiB (exactly 2^29 bits) of 0xa5 — the long-input length-field
  //     case (committed golden; hashlib == sha256sum).
  ExpectHex(std::string(64u * 1024u * 1024u, '\xa5'),
            "1c5386005b9cc63833a7cac9ee928040d05d4128b81715d893f2e2fb2a3391"
            "b7", "64 MiB (2^29-bit length field)");

  std::printf("test_sha256_kat: %d checks, %d failures\n", g_checks,
              g_failures);
  return g_failures == 0 ? 0 : 1;
}
