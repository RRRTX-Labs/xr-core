// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the permission overlay STORE (P15 T1, ADR-0051). Pure data:
// the typed model, a strict loader, the canonical serializer, and the
// projection that becomes the resolver's `permission_overlay` input.
//
// Integrity law ("corrupt => deny"): a stored document is accepted ONLY if
// (1) it parses, (2) its bytes are EXACTLY the canonical serialization of
// what it parses to (this rejects duplicate keys, which the shared parser
// resolves keep-last, plus unsorted keys, whitespace and trailing bytes),
// and (3) it satisfies the strict schema below. Any defect yields the
// fully-denying store {corrupt = true}; a partially applied store never
// escapes this file. Authenticity is NOT claimed here (a local writer with
// the profile can write a valid document); see the ADR's "does NOT prove".
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "permissions/core/types.h"

namespace xr::permissions {

struct StoredGrant {
  std::string id;                   // "g<seq>", assigned deterministically
  std::string identity;             // opaque identity value (xr:<uuid>)
  std::string domain;               // registrable domain (origin key)
  Capability capability = Capability::kGeolocation;
  Scope scope = Scope::kOnce;
  int64_t created_at = 0;           // caller-supplied ms (no clock read here)
  int64_t expires_at = 0;           // 7d only: created_at + kSevenDaysMillis
  std::string session_id;           // session only
  int64_t remaining_uses = 0;       // once only
  bool revoked = false;             // TOMBSTONE: never projected, never revived
  int64_t revoked_at = 0;
};

struct IdentityRecord {
  std::map<Capability, CapState> defaults;   // per-identity default, frozen four
  std::map<std::string, CapState> extras;    // deny-only (kAsk | kDeny), no slot
  bool denied = false;                       // Fortress deny-list (host data)
  std::vector<StoredGrant> grants;
};

struct Store {
  bool corrupt = false;
  uint64_t next_seq = 1;
  std::map<std::string, IdentityRecord> identities;  // sorted: canonical walk
};

// Strict load (see the integrity law above). Never throws, never partial.
Store LoadStore(std::string_view text);

// Canonical bytes for a store (the only form LoadStore accepts).
std::string SerializeStore(const Store& s);

// The resolver's `permission_overlay` input for this store, canonical JSON.
// Projects defaults, denied identities and NON-revoked, NON-spent grants.
// Extras are never projected (they have no slot). A corrupt store projects
// {"contract_version":1,"corrupt":true}, which the resolver reads as fully
// denying (policy/core/resolve.cc ApplyPermissionOverlay).
std::string ProjectViewJson(const Store& s);

}  // namespace xr::permissions
