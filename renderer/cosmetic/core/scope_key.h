// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/core/scope_key — the cache/identity key derivation
// (P12-T2, arch §5). This is the one place in the phase where a wrong answer is
// a SECURITY failure rather than a cosmetic one: a key that ignores the
// identity serves one identity's rule set to another, and a key that inherits
// the embedder's scope leaks a shields-down decision into a cross-site frame.
//
// The law, stated as a derivation and not as prose:
//
//     key = f(frame_site, frame_identity, embedder = None)
//
//   * The EMBEDDER IS NOT AN INPUT to the key. It appears in the signature only
//     so a caller cannot pass it by accident and so the negative fixture can
//     plant the "obvious optimization" (key on top-level site only) and watch
//     the vectors go red. An embedder's shields-down must NOT extend to a
//     cross-site frame, and a frame's exception must NOT reach the embedder.
//   * Same `<iframe>` from site S in identity A vs identity B produces
//     DIFFERENT keys (cache-partition law — the same rule P4 gave the network
//     stack; see the citation below).
//   * A blob evicted for one identity never serves another.
//
// Matching the network stack rather than inventing a second rule. The plan's
// Tests row names "OOPIF cosmetic applies in frame's partition, not
// embedder's", and the brief requires citing the P4/P6 partition code this
// matches so the two cannot drift:
//   * xr-core/shield/core/scope.{h,cc} — the P11 single scope object that
//     exceptions live on (the SAME object the network path consults; T5 forbids
//     a second exception mechanism).
//   * xr-core/policy/core/ — the P6 resolver whose per-identity decision is the
//     precedent for identity being a key component rather than a filter.
//
// Bounded and total: no allocation beyond the key bytes, no I/O, no clock, no
// throw. Two runs with the same inputs are byte-identical.
#pragma once

#include <cstdint>
#include <string>

namespace xr::cosmetic {

inline constexpr size_t kScopeKeyBytes = 32;      // sha256 of the canonical frame
inline constexpr size_t kMaxSiteLen = 256;
inline constexpr size_t kMaxIdentityLen = 128;

// Why the document is being keyed. Cosmetic rules are per-document, and the
// plan's cache contract distinguishes a same-document (SPA) navigation from a
// cross-document one: an SPA push must NOT invalidate the key, because the
// rule set is site-scoped and the document did not change site.
enum class NavigationClass {
  kInitial = 0,        // first load of the document
  kCrossDocument,      // a real navigation
  kSameDocument,       // history.pushState / hash change (SPA)
};

const char* NavigationClassName(NavigationClass c);

// Trust level of the identity. A key must not collapse two trust levels:
// an unauthenticated context and an authenticated identity are different
// partitions even when the identity string matches.
enum class IdentityTrust {
  kAnonymous = 0,
  kAuthenticated,
  kEnterprise,
};

const char* IdentityTrustName(IdentityTrust t);

struct ScopeInput {
  std::string frame_site;        // the FRAME's registrable domain (not the top)
  std::string frame_identity;    // the identity owning this frame's partition
  IdentityTrust trust = IdentityTrust::kAnonymous;
  NavigationClass navigation = NavigationClass::kInitial;
  std::string document_url_class;  // a coarse class, never the full URL (see .cc)
};

struct ScopeKey {
  // Hex sha256 of the canonical encoding. Opaque: callers must not parse it,
  // and must not assume any structure survives a format change.
  std::string hex;
  // The identity partition this key belongs to, carried alongside so an
  // eviction path can drop a whole partition without re-deriving.
  std::string partition;
  bool valid = false;
};

// Typed refusals. A key derivation that fails must fail CLOSED (no key => no
// cosmetic rules => the page renders unstyled but intact), never fall back to
// a default key that might belong to someone else.
enum class ScopeError {
  kOk = 0,
  kEmptySite,
  kSiteTooLong,
  kIdentityTooLong,
  kMalformedSite,       // a scheme, a port, a path, or an uppercase host
  kEmptyDocumentClass,
};

const char* ScopeErrorName(ScopeError e);

// Derive the key. `embedder_site` is accepted ONLY to be rejected: passing a
// non-empty embedder is a refusal, which is how the "key on top-level site
// only" optimization becomes structurally impossible rather than merely
// discouraged. See the negative fixture in tools/negatives/p12_t2.sh.
ScopeError DeriveScopeKey(const ScopeInput& in, const std::string& embedder_site,
                          ScopeKey* out);

// The partition id for an identity, independent of site. Used by the eviction
// path: dropping an identity drops its partitions, and a blob evicted for one
// identity can never be served to another.
std::string IdentityPartition(const std::string& identity, IdentityTrust trust);

}  // namespace xr::cosmetic
