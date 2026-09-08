// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — shortcut binding store (Plan P7-T4 "shortcut editor + conflict
// detection + storage in prefs schema v1"). Mirrors Chromium's accelerator
// reservation model with the three conflict classes (reserved-by-browser,
// system, duplicate) and a CONSERVATIVE deny-by-default: a conflicting bind is
// refused and the conflicting command is NAMED (the editor shows the conflict
// before binding).
//
// Crash durability: persistence is the write-tmp → fsync → rename pattern
// (rename(2) is atomic on POSIX), schema shortcuts-v1. A kill mid-write can
// leave a partial .tmp but can NEVER leave a half-written shortcuts.json —
// proven by the kill-loop test (×20 seeds) in the test suite. A truncated /
// corrupt file on load is deny-preserve: the file is kept as-is, load fails,
// and no data is silently dropped or rewritten.
#pragma once

#include <string>
#include <vector>

namespace xr::commands {

struct Binding {
  std::string command_id;
  std::string accelerator;  // normalized (e.g. "CTRL+SHIFT+K")
};

enum class ConflictKind {
  kNone = 0,
  kDuplicate,         // another command already holds this accelerator
  kBrowserReserved,   // the browser owns this accelerator
  kSystemReserved,    // the OS owns this accelerator
};
const char* ToString(ConflictKind k);

struct BindResult {
  bool ok = false;
  ConflictKind conflict = ConflictKind::kNone;
  std::string conflicting_command;  // named for kDuplicate (conflict UX)
  std::string error;
};

class ShortcutStore {
 public:
  explicit ShortcutStore(std::string store_dir = std::string())
      : dir_(std::move(store_dir)) {}

  std::string Path() const { return dir_ + "/shortcuts.json"; }

  // Load shortcuts-v1. Absent file => empty (ok). Truncated/corrupt =>
  // ok=false + preserved=true (file kept, nothing rewritten).
  struct LoadResult {
    bool ok = false;
    bool preserved = false;
    std::string error;
  };
  LoadResult Load();

  // Bind with conflict detection (deny-by-default). Does NOT persist — call
  // Save() to commit (so the editor can show the conflict before binding).
  BindResult Bind(const std::string& command_id, const std::string& accelerator);
  bool Unbind(const std::string& command_id, std::string* error);

  // Atomic persist (write-tmp → fsync → rename).
  bool Save(std::string* error) const;

  const std::vector<Binding>& bindings() const { return bindings_; }

  static const std::vector<std::string>& BrowserReserved();
  static const std::vector<std::string>& SystemReserved();
  static std::string Normalize(const std::string& accelerator);

 private:
  std::string dir_;
  std::vector<Binding> bindings_;
};

}  // namespace xr::commands
