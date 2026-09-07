// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — JSON-file-backed preference docs for the resolver inputs
// (Plan P6-T2): identity-list-v1, trust-bindings-v1, exceptions-v1 (with a
// demo forward migration to v2 that adds `granted_by`, encoding the
// "exceptions are never permanent via in-flow grants" law as data). This
// mirrors the versions-from-creation + forward-only-migration pattern of
// Chromium's components/prefs (research log R4); the REAL PrefService
// integration (mojo, profile plumbing) is farm-gated HG-29 — noted here so
// nobody mistakes this file for it. Downgrade law: a doc written by a NEWER
// schema version, read by an OLDER binary, is a data-preserving no-op (raw
// bytes kept, never rewritten, never rejected) — proven by the round-trip
// property test. "permanent" exception entries carry Settings-pointer
// semantics exactly (Plan §1.5): permanent lives solely in Settings.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "policy/core/json.h"

namespace xr::policy {

enum class StoreError {
  kNone = 0,
  kIoError,
  kMalformedInput,   // envelope wrong / not strict JSON
  kUnknownSchema,    // schema id this binary does not know at all
  kInvalidEntry,     // data failed validation for its (known) version
};

const char* ToString(StoreError e);

struct StoreDoc {
  std::string schema;       // e.g. "xr-exceptions"
  int schema_version = 0;
  int64_t created_at = 0;
  JsonValue data;           // validated (known version) or raw (future version)
  bool future_version = false;  // version > known: data-preserving no-op mode

  JsonValue ToJson() const;
};

struct StoreLoadResult {
  bool ok = false;
  StoreDoc doc;
  StoreError error = StoreError::kNone;
  std::string error_detail;
  std::string path;
};

class PolicyStore {
 public:
  // Latest schema versions this binary knows.
  static std::map<std::string, int> DefaultKnownVersions();
  // `known_versions` lets tests simulate an older binary (downgrade no-op
  // property); production always uses DefaultKnownVersions().
  explicit PolicyStore(std::map<std::string, int> known_versions);

  // Load + envelope-validate. Unknown schema => kUnknownSchema (deny-safe).
  // Future KNOWN schema (version > known) => future_version=true, raw data
  // kept (no rewrite ever). Known version => strict data validation.
  StoreLoadResult Load(const std::string& path) const;

  // Canonical-JSON write via temp file + rename (atomic on POSIX).
  bool Save(const std::string& path, const StoreDoc& doc, std::string* error) const;

  // Forward-only migration to the latest known version. Returns a new doc;
  // the input is untouched. Future-version docs are returned unchanged
  // (never rewritten downward or sideways).
  struct MigrateResult {
    bool ok = false;
    StoreDoc doc;
    bool changed = false;
    std::string error;
  };
  MigrateResult MigrateToLatest(const StoreDoc& doc) const;

  int KnownVersion(const std::string& schema) const;

 private:
  std::map<std::string, int> known_;
};

// Strict validators per (schema, version). Exported for tests.
bool ValidateIdentityList(const JsonValue& data, std::string* error);
bool ValidateTrustBindings(const JsonValue& data, std::string* error);
bool ValidateExceptions(const JsonValue& data, int version, std::string* error);

}  // namespace xr::policy
