// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the settings section registry (Plan P8-T1). The registry is
// GENERATED from the settings-schema data (settings_schema_v1.json): there
// is no hand-maintained section list anywhere — a section exists iff it is
// in the schema data, and its settings/anchors/availability all derive from
// the same rows (orphan detection lives in the schema loader).
//
// Availability hooks (P8 one-brain law): a section's availability predicate
// is evaluated against a PINNED P6 policy snapshot (a versioned copy taken
// once at host start), never a live Resolve() — the same rule the P7
// commands predicates follow. Predicates: `always` and `identity.active`
// (an active identity in the pinned snapshot). Unknown predicates deny
// (L3 — never a guess).
#pragma once

#include <string>
#include <vector>

#include "settings/core/settings_schema.h"

namespace xr::settings {

// The pinned P6 snapshot surface settings may read. Shape
// (xr-settings-state-v1): {"schema","schema_version","context":
// {"identity_id": <xr: id|null>, "origin": <string|null>},
// "values": {key: {"value": <json>, "source": "resolver"|"enterprise",
// "managed": bool}}}. Strict: unknown top-level/row fields rejected. The
// snapshot is pinned per host run: settings display the policy truth they
// were GIVEN; they never compute policy (mode_lint L3).
struct StateLoadResult {
  bool ok = false;
  std::string error;
};

class PolicyState {
 public:
  static constexpr const char* kSchema = "xr-settings-state";
  static constexpr int kVersion = 1;

  StateLoadResult Load(const std::string& json_text);

  bool identity_active() const { return identity_active_; }
  bool HasValue(const std::string& key) const;
  // (value, source, managed) for a key; ok=false when the state doc has no
  // entry (caller falls back to the schema's deny-safe default).
  struct Value { bool ok = false; JsonValue value; std::string source; };
  Value GetValue(const std::string& key) const;

  int version() const { return version_; }

 private:
  int version_ = 0;
  bool identity_active_ = false;
  std::map<std::string, Value> values_;
};

struct SectionView {
  std::string id;
  int order = 0;
  std::string title_id;
  std::string availability;
  std::string anchor;             // derived: xr://settings/<id>
  bool available = false;
  std::string unavailable_reason;  // typed; "" when available
  std::vector<std::string> setting_keys;
};

// Build the ordered section registry from a loaded schema + pinned state.
std::vector<SectionView> BuildRegistry(const SettingsSchema& schema,
                                       const PolicyState& state);

// Availability hook (exported for the host + tests): denies unknown
// predicates; `always` => available; `identity.active` reads the state.
bool SectionAvailable(const SectionDef& section, const PolicyState& state,
                      std::string* reason);

}  // namespace xr::settings
