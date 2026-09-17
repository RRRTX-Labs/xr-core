// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// See scope_key.h for the derivation law and the partition citations.

#include "renderer/cosmetic/core/scope_key.h"

#include "common/core/sha256.h"

#include <cctype>

namespace xr::cosmetic {
namespace {

// Canonical field separator. 0x01 cannot appear in a hostname or an identity
// string, so the encoding is unambiguous without escaping — and an unambiguous
// encoding is what stops ("a.b", "c") and ("a", "b.c") from colliding.
constexpr char kSep = '\x01';

bool IsValidSite(const std::string& s) {
  if (s.empty() || s.size() > kMaxSiteLen) return false;
  // A SITE, not a URL: no scheme, no port, no path, no query. Accepting those
  // would let a key vary on data the rule set does not depend on, which both
  // fragments the cache and creates an oracle.
  for (const char c : s) {
    if (c == ':' || c == '/' || c == '?' || c == '#' || c == '@') return false;
    // The canonical field separator must not be constructible from an input
    // field, or ("a\x01b", "c") and ("a", "b\x01c") would encode identically
    // and collide. Control characters have no business in a site anyway.
    if (static_cast<unsigned char>(c) < 0x21 || c == 0x7f) return false;
    // Uppercase is refused rather than lowercased: the caller must normalize,
    // and silently lowercasing here would hide a caller that did not.
    if (std::isupper(static_cast<unsigned char>(c))) return false;
  }
  // A label must be non-empty: reject "a..b", ".a", "a.".
  if (s.front() == '.' || s.back() == '.') return false;
  if (s.find("..") != std::string::npos) return false;
  return true;
}

}  // namespace

const char* NavigationClassName(NavigationClass c) {
  switch (c) {
    case NavigationClass::kInitial: return "initial";
    case NavigationClass::kCrossDocument: return "cross-document";
    case NavigationClass::kSameDocument: return "same-document";
  }
  return "unknown";
}

const char* IdentityTrustName(IdentityTrust t) {
  switch (t) {
    case IdentityTrust::kAnonymous: return "anonymous";
    case IdentityTrust::kAuthenticated: return "authenticated";
    case IdentityTrust::kEnterprise: return "enterprise";
  }
  return "unknown";
}

const char* ScopeErrorName(ScopeError e) {
  switch (e) {
    case ScopeError::kOk: return "ok";
    case ScopeError::kEmptySite: return "empty-site";
    case ScopeError::kSiteTooLong: return "site-too-long";
    case ScopeError::kIdentityTooLong: return "identity-too-long";
    case ScopeError::kMalformedSite: return "malformed-site";
    case ScopeError::kEmptyDocumentClass: return "empty-document-class";
  }
  return "unknown";
}

std::string IdentityPartition(const std::string& identity,
                              IdentityTrust trust) {
  // The partition is derived from the identity and its TRUST, never from the
  // site: eviction is per-identity, so a blob dropped for identity A must not
  // be reachable from identity B, and the same identity string at a different
  // trust level is a different partition.
  std::string frame;
  frame.reserve(identity.size() + 24);
  frame += IdentityTrustName(trust);
  frame += kSep;
  frame += identity;
  // The SINGLE shared SHA-256 copy in //xr/common (P11-T0-b, ADR-0043). No
  // second hash, no second hex encoder: tools/no_new_crypto_check.py enforces
  // this, and a key-set hash that differs from the bundle's would be a
  // stop-condition, not an optimization.
  return xr::common::Sha256Hex(frame);
}

ScopeError DeriveScopeKey(const ScopeInput& in,
                          const std::string& embedder_site, ScopeKey* out) {
  out->hex.clear();
  out->partition.clear();
  out->valid = false;

  // THE SECURITY RULE, enforced structurally. The embedder is not an input to
  // the key. A caller that passes one is refused rather than silently
  // corrected, because "we noticed you tried to key on the top-level site and
  // fixed it for you" is how a leak survives a refactor.
  if (!embedder_site.empty()) return ScopeError::kMalformedSite;

  if (in.frame_site.empty()) return ScopeError::kEmptySite;
  if (in.frame_site.size() > kMaxSiteLen) return ScopeError::kSiteTooLong;
  if (!IsValidSite(in.frame_site)) return ScopeError::kMalformedSite;
  if (in.frame_identity.size() > kMaxIdentityLen) {
    return ScopeError::kIdentityTooLong;
  }
  if (in.document_url_class.empty()) return ScopeError::kEmptyDocumentClass;

  // Canonical frame. Field ORDER is part of the format: reordering it changes
  // every key, so it is pinned by the golden vectors.
  //
  // Note what is ABSENT: the embedder site, the full document URL, and any
  // timestamp. A same-document (SPA) navigation keeps the same key by design —
  // the rule set is site-scoped and the document did not change site — so
  // `navigation` participates only to distinguish an SPA push from a real
  // navigation for the cache's own bookkeeping, and kInitial/kCrossDocument
  // deliberately encode identically.
  std::string frame;
  frame.reserve(in.frame_site.size() + in.frame_identity.size() +
                in.document_url_class.size() + 48);
  frame += "cosmetic-scope-v1";
  frame += kSep;
  frame += in.frame_site;
  frame += kSep;
  frame += IdentityTrustName(in.trust);
  frame += kSep;
  frame += in.frame_identity;
  frame += kSep;
  frame += in.document_url_class;
  frame += kSep;
  frame += (in.navigation == NavigationClass::kSameDocument) ? "same-doc"
                                                             : "doc";

  out->hex = xr::common::Sha256Hex(frame);
  out->partition = IdentityPartition(in.frame_identity, in.trust);
  out->valid = true;
  return ScopeError::kOk;
}

}  // namespace xr::cosmetic
