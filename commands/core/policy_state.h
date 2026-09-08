// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — the bridge between the command registry and P6 policy STATE.
// The trust dial's per-site position IS the P6 trust-binding
// (xr-core/policy/core/resolve.h TrustBinding; trust-bindings-v1). A dial
// command's handler writes/reads that doc, so "dial steps toggle P6 policy
// state end-to-end through the host" is REAL: the file this module writes is
// byte-valid against the P6 store's xr-trust-bindings-v1 schema, and the
// dial test links policy_core (PolicyStore + resolver) to prove the resolver
// reflects the toggled tier.
//
// std-only (no policy include): this module OWNS the read/write of the doc.
// policy_core is the authority that VALIDATES it (test links it). The pinned
// availability snapshot is a separate, versioned file (pinned-at-version, not
// a live Resolve() — the P6 TOCTOU law).
#pragma once

#include <string>

#include "commands/core/json.h"

namespace xr::commands {

class PolicyState {
 public:
  explicit PolicyState(std::string store_dir = std::string())
      : dir_(std::move(store_dir)) {}

  std::string TrustBindingsPath() const { return dir_ + "/trust_bindings.json"; }
  std::string SnapshotPath() const { return dir_ + "/policy_snapshot.json"; }

  // Current trust tier for (domain, identity); "" when no binding.
  std::string CurrentTrust(const std::string& domain,
                           const std::string& identity) const;

  // Upsert the trust binding and persist atomically. Returns the PREVIOUS
  // tier ("", "kStandard", "kShield", or "kFortress"). `trust` must be a
  // legal P6 tier (kStandard|kShield|kFortress); reset uses RemoveTrust.
  std::string SetTrust(const std::string& domain, const std::string& identity,
                       const std::string& trust, std::string* error);

  // Remove the per-site binding (dial reset). Returns the previous tier.
  std::string RemoveTrust(const std::string& domain, const std::string& identity,
                          std::string* error);

  // The PINNED availability snapshot (by value — a pinned copy, TOCTOU-safe).
  // Reads policy_snapshot.json if present, else the default golden snapshot
  // (dial writable, no Tor capability, one active identity).
  JsonValue Snapshot() const;

 private:
  std::string dir_;
  JsonValue::Array mem_bindings_;  // ephemeral (dir_ empty) in-memory state
  // Load the bindings array (best-effort; malformed => empty, deny-safe).
  JsonValue::Array LoadBindings() const;
  bool SaveBindings(const JsonValue::Array& bindings, std::string* error);
};

}  // namespace xr::commands
