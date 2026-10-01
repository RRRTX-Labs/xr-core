// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — identity model + lifecycle implementation (header has the
// laws). Fail-closed everywhere: unknown identity, unreadable prefs, or a
// purge that cannot be VERIFIED are errors, never silent successes.
#include "core/identity.h"

#include <utility>

namespace xr::identity {
namespace {

// The byte surface a fresh identity owns today (the verify step walks these
// kinds; the browser half adds real files, the model must stay honest about
// the CATEGORIES so the negative can plant one of each).
std::vector<SurfaceBlob> FreshSurface(bool in_memory) {
  std::vector<SurfaceBlob> s;
  s.push_back({"cookies", 4096});
  s.push_back({"cache", 65536});
  s.push_back({"prefs", 2048});
  s.push_back({"storage", 8192});
  if (!in_memory) {
    s.push_back({"session_data", 1024});  // on-disk only
  }
  return s;
}

}  // namespace

bool IdentityStore::Insert(const IdentityRecord& rec, std::string* error) {
  // The opacity law, enforced at the store boundary: the key must be a
  // mint-shaped domain (DomainShapeOk, inside LooksOpaque) and must NOT
  // embed the display name (a name-keyed store is the §1.1
  // identity-separation bug, made unrepresentable here).
  if (!LooksOpaque(rec.domain, rec.display_name)) {
    if (error) *error = "kNotPermitted (domain not opaque)";
    return false;
  }
  if (records_.count(rec.domain)) {
    if (error) *error = "kAlreadyExists";
    return false;
  }
  records_.emplace(rec.domain, rec);
  return true;
}

IdentityRecord* IdentityStore::Find(std::string_view domain) {
  auto it = records_.find(std::string(domain));
  return it == records_.end() ? nullptr : &it->second;
}

const IdentityRecord* IdentityStore::Find(std::string_view domain) const {
  auto it = records_.find(std::string(domain));
  return it == records_.end() ? nullptr : &it->second;
}

bool IdentityStore::Erase(std::string_view domain) {
  return records_.erase(std::string(domain)) > 0;
}

void IdentityStore::PlantResidual(std::string_view domain, SurfaceBlob blob) {
  residual_[std::string(domain)].push_back(std::move(blob));
}

size_t IdentityStore::ResidualBytes(std::string_view domain) const {
  size_t total = 0;
  if (const IdentityRecord* r = Find(domain)) {
    for (const auto& b : r->surface) total += b.bytes;
  }
  auto it = residual_.find(std::string(domain));
  if (it != residual_.end()) {
    for (const auto& b : it->second) total += b.bytes;
  }
  return total;
}

std::vector<std::string> IdentityStore::ResidualKinds(
    std::string_view domain) const {
  std::vector<std::string> kinds;
  if (const IdentityRecord* r = Find(domain)) {
    for (const auto& b : r->surface) kinds.push_back(b.kind);
  }
  if (auto it = residual_.find(std::string(domain)); it != residual_.end()) {
    for (const auto& b : it->second) kinds.push_back(b.kind);
  }
  return kinds;
}

CallResult Manager::Create(const CreateRequest& req, IdentityRecord* out) {
  CallResult res;
  std::string domain;
  if (!req.entropy.empty()) {
    if (!MintDomain(req.entropy, &domain)) {
      res.error = "kMalformedInput (mint refused)";
      return res;
    }
  } else if (req.replay) {
    // Explicit replay (vectors): the deterministic fixture table. The host
    // façade never sets `replay`; no-entropy-no-replay is refused below.
    if (!FixtureDomain(fixture_sequence_++, &domain)) {
      res.error = "kMalformedInput (fixture table exhausted)";
      return res;
    }
  } else {
    res.error = "kMalformedInput (entropy required)";
    return res;
  }
  IdentityRecord rec;
  rec.domain = domain;
  rec.display_name = req.display_name.empty() ? "Identity" : req.display_name;
  rec.template_id = req.template_id;
  rec.grade = req.grade;
  rec.in_memory = req.in_memory;
  rec.state = State::kActive;
  rec.surface = FreshSurface(req.in_memory);
  rec.color = "#5f6b7a";   // neutral default; templates overlay theirs
  rec.glyph = "•";
  rec.prefs.readable = true;
  rec.prefs.rows = req.prefs_overrides;
  // Overlay rows from the template are merged by the façade BEFORE Create
  // (templates.h owns them); here we only persist what we were given —
  // keeps this file free of template knowledge.
  std::string err;
  if (!store_->Insert(rec, &err)) {
    res.error = err;
    return res;
  }
  res.ok = true;
  if (out) *out = rec;
  return res;
}

CallResult Manager::Activate(std::string_view domain) {
  CallResult res;
  IdentityRecord* r = store_->Find(domain);
  if (!r) { res.error = "kUnknownIdentity"; return res; }
  if (r->state == State::kDestroyed) { res.error = "kNotPermitted"; return res; }
  r->state = State::kActive;
  res.ok = true;
  return res;
}

CallResult Manager::Hibernate(std::string_view domain) {
  CallResult res;
  IdentityRecord* r = store_->Find(domain);
  if (!r) { res.error = "kUnknownIdentity"; return res; }
  if (r->state == State::kDestroyed) { res.error = "kNotPermitted"; return res; }
  // Hibernation law: discard the RENDERERS (the volatile surface kinds),
  // PRESERVE the partition state (cookies/prefs/storage — what wake needs).
  // The model marks it by dropping 'cache' only; the record survives.
  std::vector<SurfaceBlob> kept;
  for (auto& b : r->surface) {
    if (b.kind != "cache") kept.push_back(b);
  }
  r->surface = std::move(kept);
  r->state = State::kHibernated;
  res.ok = true;
  return res;
}

CallResult Manager::Destroy(std::string_view domain,
                            bool* zero_residual_verified) {
  CallResult res;
  IdentityRecord* r = store_->Find(domain);
  if (!r) { res.error = "kUnknownIdentity"; return res; }
  // Step 1: purge — drop every blob of the identity's own surface, the
  // prefs namespace, and the record itself.
  r->surface.clear();
  r->prefs.rows.clear();
  r->prefs.readable = false;
  store_->Erase(domain);
  // Step 2: VERIFY THE STORE, not the purge's return code (§1.4): any bytes
  // still attributable to this domain — the identity's own surface (should
  // be none) or out-of-place bytes the purge missed (residual_) — make
  // destruction a FAILURE. The negative test plants one and proves it.
  const bool verified = store_->ResidualBytes(domain) == 0 &&
                        store_->Find(domain) == nullptr;
  if (zero_residual_verified) *zero_residual_verified = verified;
  if (!verified) {
    res.error = "kInternal (residual bytes after purge: " +
                std::to_string(store_->ResidualBytes(domain)) + " bytes in " +
                std::to_string(store_->ResidualKinds(domain).size()) +
                " place(s))";
    return res;
  }
  res.ok = true;
  return res;
}

CallResult Manager::PromoteToFortressProfile(std::string_view domain,
                                             IdentityRecord* out) {
  CallResult res;
  IdentityRecord* r = store_->Find(domain);
  if (!r) { res.error = "kUnknownIdentity"; return res; }
  if (r->state == State::kDestroyed) { res.error = "kNotPermitted"; return res; }
  if (r->in_memory) {
    // A disposable (in-memory) identity cannot hold a durable profile:
    // promoting it would persist what was promised to be volatile.
    res.error = "kNotPermitted (in-memory identity)";
    return res;
  }
  r->grade = Grade::kFortress;
  if (out) *out = *r;
  res.ok = true;
  return res;
}

std::optional<std::string> Manager::ResolvePref(std::string_view domain,
                                                std::string_view key) const {
  const IdentityRecord* r = store_->Find(domain);
  if (!r || !r->prefs.readable) {
    return std::nullopt;  // fail-safe: NO overrides (closed), never open
  }
  auto it = r->prefs.rows.find(std::string(key));
  if (it == r->prefs.rows.end()) return std::nullopt;
  return it->second;
}

IdentityRecord* Manager::FindInStore(std::string_view domain) {
  return store_->Find(domain);
}

const IdentityRecord* Manager::FindInStore(std::string_view domain) const {
  return store_->Find(domain);
}

}  // namespace xr::identity
