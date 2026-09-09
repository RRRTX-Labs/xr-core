// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the Attention-Budget ledger, LOCAL COUNTERS ONLY (Plan P8-T6
// and the no-hidden-telemetry law). Counters answer "which section opened,
// which search query was accepted, which setting changed" at DAY
// granularity so promotion decisions (L12) rest on usage data. They are:
//   * LOCAL: one JSON file (xr-settings-counters v1) under the settings
//     store dir — never uploaded, no network path exists, no identifiers,
//     no timestamps finer than day, no per-user keys beyond the counter
//     rows themselves;
//   * ROLLING: days older than kRetentionDays are dropped on save
//     (retention policy — stated here and in docs/state/attention-budget.md);
//   * DISPOSABLE-SAFE: with an empty store dir (disposable/ephemeral
//     contexts) counters live in memory ONLY — zero bytes ever hit disk
//     (asserted by a filesystem-diff test);
//   * KILL-SAFE: persistence is write-tmp -> fsync -> rename; a kill
//     mid-write can leave a partial .tmp but never a half-written ledger
//     (proven by the kill-loop test). A corrupt file on load is kept as-is
//     (deny-preserve: never silently rewritten or dropped).
//
// Day granularity: days are UTC calendar dates (no clock value is stored —
// no finer timestamp exists anywhere in this file).
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "settings/core/json.h"

namespace xr::settings {

class CounterStore {
 public:
  static constexpr const char* kSchema = "xr-settings-counters";
  static constexpr int kVersion = 1;
  // Retention policy (the stated law; enforced on Save).
  static constexpr int kRetentionDays = 90;

  explicit CounterStore(std::string store_dir = std::string())
      : dir_(std::move(store_dir)) {}

  // Load the ledger. Absent file => empty (ok). Truncated/corrupt =>
  // ok=false + preserved=true (file kept, nothing rewritten).
  struct LoadResult {
    bool ok = false;
    bool preserved = false;
    std::string error;
  };
  LoadResult Load();

  // Event records (day-granular; no-op on nothing).
  void OpenSection(const std::string& section);
  void AcceptQuery();
  void SettingChanged(const std::string& key);

  // Atomic durable persist (write-tmp -> fsync -> rename). Disposable
  // stores (empty dir) return true WITHOUT writing anything. A ledger that
  // failed to load is PRESERVED: Save refuses to overwrite it (deny-preserve
  // — never silently rewrite or drop a file the loader could not read).
  bool Save(std::string* error) const;

  // Canonical JSON dump of the whole ledger (counters-dump / xrctl).
  JsonValue Dump() const;

  // Current UTC day "YYYY-MM-DD" (deterministic given the clock; used for
  // bucket keys only — no finer timestamp is ever stored).
  static std::string UtcToday();

  size_t TotalQueries() const;   // all days (diagnostics)
  bool in_memory() const { return dir_.empty(); }
  std::string Path() const { return dir_.empty() ? "" : dir_ + "/settings-counters.json"; }

 private:
  std::string dir_;
  // day -> { opened: {section: n}, queries: n, changed: {key: n} }
  std::map<std::string, JsonValue> days_;
  // in-memory scratch used when dir_ is empty (never serialized to disk)
  bool loaded_ = false;
  // set when Load() met an on-disk ledger it could not read (deny-preserve)
  bool load_failed_ = false;
};

}  // namespace xr::settings
