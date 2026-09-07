// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the mojom-side resolver service. The mojom-binding half is
// farm-gated: it compiles ONLY when XR_HAVE_MOJOM_BINDINGS is defined (GN
// sets it when the xr.mojom bindings exist); the DEFAULT build here is the
// std C++ core + the JSON-over-stdio façade (policy_host), which speaks the
// same protocol as the Python fakes (fakes/README.md) so P5's parity
// harness and xrctl drive either implementation interchangeably. The
// service owns: store loading (identity-list / trust-bindings / exceptions
// with migrations), the managed enterprise source (signed-or-ignored), the
// generational cache, and snapshot sequencing. HG-29 tracks the real
// PrefService/mojo integration at the farm.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "policy/core/cache.h"
#include "policy/core/resolve.h"
#include "policy/core/snapshot.h"
#include "policy/core/store.h"
#include "policy/enterprise/managed_source.h"

namespace xr::policy {

struct ServiceDiagnostics {
  std::vector<std::string> ledger_rows;
  StoreLoadResult identity_list;
  StoreLoadResult trust_bindings;
  StoreLoadResult exceptions;
  ManagedResult managed;
  PolicyCache::Stats cache;
  uint64_t snapshot_seq = 0;
  size_t state_entries = 0;
};

class PolicyResolverService {
 public:
  // `store_dir` == "" => stateless defaults (in-memory fixture identities;
  // parity mode). Non-empty => loads the three pref docs + managed source.
  explicit PolicyResolverService(std::string store_dir = std::string());

  // Resolves one JSON request (stdio protocol shape). Store layers are
  // merged UNDER request-carried layers (explicit request fields win).
  // Returns the canonical JSON envelope. Ledger rows accumulate.
  std::string ResolveText(const std::string& request_json);
  JsonValue ResolveJson(const JsonValue& request);

  // Full snapshot of the accumulated resolve state at the next seq.
  SnapshotEncodeResult SnapshotFull();
  // Diff against the last emitted snapshot state.
  SnapshotEncodeResult SnapshotDiff();

  const ServiceDiagnostics& diagnostics() const { return diag_; }
  PolicyCache& cache() { return cache_; }

  // Human-readable dump (support-engineer surface). `managed` adds the
  // enterprise section. Canonical JSON twin via DumpJson.
  std::string Dump(bool include_managed) const;
  JsonValue DumpJson(bool include_managed) const;

 private:
  void LoadStores();
  JsonValue MergeStoreLayers(const JsonValue& request) const;

  std::string store_dir_;
  PolicyStore store_;
  PolicyCache cache_;
  mutable std::mutex mu_;
  std::map<std::string, SnapshotEntry> state_;  // key: identity|site|trust
  uint64_t seq_ = 0;
  uint64_t last_snapshot_seq_ = 0;
  std::vector<SnapshotEntry> last_snapshot_state_;
  ServiceDiagnostics diag_;
  bool has_store_ = false;
};

}  // namespace xr::policy
