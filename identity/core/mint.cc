// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — mint implementation (header has the laws). The only crypto is
// the shared sha256 (common/core — the single in-tree copy); no RNG, no
// clock, no counter: the mint is a pure function of caller entropy.
#include "core/mint.h"

#include <cstdio>
#include <string>

#include "identity/core/sha256.h"

namespace xr::identity {
namespace {

// Format 16 bytes as the UUID v4 shape with version/variant forced
// (version nibble 4, variant 10xx) — the same bits the fixture table
// carries (...-4000-8000-...), so entropy- and fixture-minted domains are
// indistinguishable in shape and opacity.
std::string FormatUuid(const std::array<uint8_t, 16>& bytes) {
  const char* hex = "0123456789abcdef";
  std::array<uint8_t, 16> b = bytes;
  b[6] = static_cast<uint8_t>((b[6] & 0x0f) | 0x40);  // version 4
  b[8] = static_cast<uint8_t>((b[8] & 0x3f) | 0x80);  // variant 10xx
  std::string s(kDomainPrefix);
  for (size_t i = 0; i < 16; ++i) {
    s += hex[b[i] >> 4];
    s += hex[b[i] & 0xf];
    if (i == 3 || i == 5 || i == 7 || i == 9) s += '-';
  }
  return s;
}

// One lowercase hex digit. Anything else (uppercase included: the shared
// sha256 emits lowercase only) is refused.
bool HexNibble(char c, uint8_t* v) {
  if (c >= '0' && c <= '9') {
    *v = static_cast<uint8_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *v = static_cast<uint8_t>(c - 'a' + 10);
    return true;
  }
  return false;
}

}  // namespace

bool DecodeDigestHex(std::string_view hex, std::array<uint8_t, 16>* out) {
  if (out == nullptr || hex.size() < 2 * out->size()) {
    return false;
  }
  std::array<uint8_t, 16> bytes{};
  for (size_t i = 0; i < bytes.size(); ++i) {
    uint8_t hi = 0;
    uint8_t lo = 0;
    if (!HexNibble(hex[2 * i], &hi) || !HexNibble(hex[2 * i + 1], &lo)) {
      return false;  // leaves `out` untouched
    }
    bytes.at(i) = static_cast<uint8_t>(hi * 16 + lo);
  }
  *out = bytes;
  return true;
}

bool MintDomain(std::string_view entropy, std::string* out) {
  if (entropy.empty() || out == nullptr) {
    return false;  // fail-closed: no mint from nothing
  }
  // The first 32 hex chars (16 bytes) of sha256(entropy) become the UUID
  // body; byte 6 carries the version nibble, byte 8 the variant (forced in
  // FormatUuid). The shared sha256 always yields 64 lowercase hex chars; if
  // it ever did not, `domain` stays empty and the shape check below fails
  // closed (no guessed domain).
  std::array<uint8_t, 16> bytes{};
  std::string domain;
  if (DecodeDigestHex(Sha256Hex(std::string(entropy)), &bytes)) {
    domain = FormatUuid(bytes);
  }
  if (domain.size() != kDomainLength) return false;
  *out = domain;
  return true;
}

bool FixtureDomain(uint64_t sequence, std::string* out) {
  static const char* const kTable[] = {
      "xr:00000000-0000-4000-8000-000000000001",
      "xr:00000000-0000-4000-8000-000000000002",
      "xr:00000000-0000-4000-8000-00000000000ef",
  };
  if (out == nullptr || sequence >= sizeof(kTable) / sizeof(kTable[0])) {
    return false;
  }
  *out = kTable[sequence];
  return true;
}

bool DomainShapeOk(std::string_view domain) {
  // 39 (canonical mint) or 40 (the frozen table's third entry quirk).
  if (domain.size() != kDomainLength && domain.size() != kDomainLengthFrozenMax) {
    return false;
  }
  if (domain.compare(0, kDomainPrefix.size(), kDomainPrefix) != 0) return false;
  // hex-and-dashes at the fixed positions, else not the UUID shape
  for (size_t i = 3; i < domain.size(); ++i) {
    const char c = domain[i];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    const bool dash = c == '-' &&
                      (i == 11 || i == 16 || i == 21 || i == 26);
    if (!hex && !dash) return false;
  }
  if (domain[17] != '4') return false;  // version nibble
  const char v = domain[22];            // variant high nibble: 8,9,a,b
  return v == '8' || v == '9' || v == 'a' || v == 'b';
}

bool LooksOpaque(std::string_view domain, std::string_view probe) {
  if (!DomainShapeOk(domain)) return false;
  if (probe.empty()) return true;
  // P14-CLOSE C-4 finding (2026-10-10): a probe made ONLY of characters a
  // minted domain contains by chance (hex digits and '-') and shorter than
  // kChanceProbeMin is not evidence of embedding. It is a coincidence with
  // probability ~1 for one character: "B" sits inside ~87% of random domains,
  // so Create refused a user who named an identity "B" (or "A", "Dad",
  // "Cafe" ...) with kNotPermitted (domain not opaque). Such probes are not
  // judged. Every probe with a non-hex character, and every all-hex probe of
  // 8+ characters, is still judged exactly as before.
  if (probe.size() < kChanceProbeMin) {
    bool chance_alphabet = true;
    for (char c : probe) {
      const char l = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
      if (!((l >= '0' && l <= '9') || (l >= 'a' && l <= 'f') || l == '-')) {
        chance_alphabet = false;
        break;
      }
    }
    if (chance_alphabet) return true;
  }
  // case-insensitive containment: an opaque domain never embeds the probe
  std::string d(domain), p(probe);
  for (char& c : d) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  for (char& c : p) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  return d.find(p) == std::string::npos;
}

}  // namespace xr::identity
