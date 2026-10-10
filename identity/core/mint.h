// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the identity partition-domain mint (P14-T1). The domain is an
// OPAQUE UUID (the P4 census ruling: deriving a partition domain from a name,
// a site, a URL, or anything guessable is the identity-separation bug — the
// seam takes an opaque domain, storage_partition_config.h). This module:
//   * takes CALLER-SUPPLIED ENTROPY (the production host passes OS entropy
//     per call — the core never reaches for a clock, a counter, or rand());
//   * derives the domain deterministically: sha256(entropy) formatted as a
//     version-4/variant-8 UUID, prefixed "xr:" (mojom identity.mojom v1);
//   * refuses empty entropy (fail-closed: no silent mint from nothing);
//   * offers the FIXTURE TABLE for replayable vectors (the same
//     deterministic sequence the Python fake mints — the fixture path exists
//     so golden vectors are byte-stable across both backends; it is marked
//     as such, and every opacity law applies to it identically).
// The opacity laws are TESTED (tests/test_mint.cc): the minted domain never
// contains the entropy, never equals a name/site/URL-derived candidate, and
// the derivation resists the P4 probe corpus (partition name, title, log
// line, site) — the "identity-from-name" pattern cannot be reproduced.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace xr::identity {

// "xr:" + 36-char UUID shape (8-4-4-4-12) = 39 chars, matching the frozen
// fake's vid shape (fakes/identity.py: xr:00000000-0000-4000-8000-...).
// The frozen table's THIRD entry is 40 chars ("…00000000000ef" — an
// upstream quirk now FROZEN with the v1 fixtures); the entropy mint always
// produces the canonical 39. Both lengths are the frozen v1 surface; a v2
// amendment (registry-post-freeze) would normalize — none is smuggled here.
inline constexpr std::string_view kDomainPrefix = "xr:";
inline constexpr size_t kDomainLength = 39;
inline constexpr size_t kDomainLengthFrozenMax = 40;
// LooksOpaque: all-hex probes shorter than this are chance, not embedding.
inline constexpr size_t kChanceProbeMin = 8;

// Derive the opaque domain from caller entropy. Deterministic; empty entropy
// returns false and leaves `out` untouched (fail-closed).
bool MintDomain(std::string_view entropy, std::string* out);

// The mint's digest decoding, exposed so its refusals are tested rather than
// unreachable: the first 16 bytes of a lowercase hex string. Returns false and
// leaves `out` untouched on a short input, a non-hex (or uppercase) digit, or
// a null `out`.
bool DecodeDigestHex(std::string_view hex, std::array<uint8_t, 16>* out);

// The fixture mint table (index 0 -> ...0001, 1 -> ...0002, ...), for
// replayable vectors only — same shape, same opacity laws. `sequence` is the
// number of prior mints; returns false when the table is exhausted
// (3 entries today: 1, 2, 0xef — a vector beyond the table must say so, not
// silently fall back to entropy-derived values).
bool FixtureDomain(uint64_t sequence, std::string* out);

// The frozen-v1 domain SHAPE test (39 or 40 chars, UUID positions, version
// nibble 4, variant 8/9/a/b) — no probe. The store and the binding model
// accept exactly this shape.
bool DomainShapeOk(std::string_view domain);

// Opacity probe (used by tests and the host façade's self-check): is this
// string REJECTED as an opaque domain? A candidate that embeds `probe`
// (case-insensitive), or is not exactly the UUID shape, is NOT opaque.
bool LooksOpaque(std::string_view domain, std::string_view probe);

}  // namespace xr::identity
