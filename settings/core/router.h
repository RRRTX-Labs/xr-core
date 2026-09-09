// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the settings deep-link router (Plan P8-T1). Every section
// and every setting is addressable by a STABLE anchor path derived from the
// schema data (xr://settings/<section>[/<setting-key>]); anchors are never
// hand-maintained — the router and the help-anchor contract share the same
// derivation, so a dangling anchor is impossible (T7). An unresolvable
// anchor is a TYPED error (never a silent redirect), with near-miss
// suggestions for the search-first UI.
#pragma once

#include <string>
#include <vector>

#include "settings/core/settings_schema.h"

namespace xr::settings {

enum class ResolveKind { kHome, kSection, kSetting, kUnknown };

struct ResolveResult {
  bool ok = false;             // false => kUnknown (typed error)
  ResolveKind kind = ResolveKind::kUnknown;
  std::string section;         // set for kSection/kSetting
  std::string setting;         // set for kSetting
  std::string canonical;       // canonical anchor for this resolution
  std::string error;           // typed human-readable reason
  std::vector<std::string> suggestions;  // near-miss anchors (UI aid)
};

class Router {
 public:
  explicit Router(const SettingsSchema& schema);

  // Resolve an anchor: accept full ("xr://settings/network/adblock"), a
  // root-relative path ("/network/adblock") or the bare suffix
  // ("network/adblock"). Empty/root => home.
  ResolveResult Resolve(const std::string& anchor) const;

  // Canonical anchor for a known section / setting ("" when unknown).
  std::string SectionAnchor(const std::string& section) const;
  std::string SettingAnchor(const std::string& setting) const;

  // Every section's anchor — the T7 "anchors generated from the same data
  // as the router" surface.
  std::vector<std::string> SectionAnchors() const;

  // Section id owning a setting ("" when unknown).
  std::string SectionOf(const std::string& setting) const;

  // The canonical per-setting URL suffix: drops the "<section>." prefix
  // from a dotted key (network.adblock -> "adblock").
  static std::string AnchorSuffix(const std::string& section,
                                  const std::string& key);

 private:
  const SettingsSchema* schema_;
};

}  // namespace xr::settings
