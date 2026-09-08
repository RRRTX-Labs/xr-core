// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — commands_host: the JSON-over-stdio façade for the C++ command
// core (command-host-protocol v1, registered post-freeze). Speaks the SAME
// protocol as the Python reference fake (xr-core/fakes/commands.py), so
// `xrctl commands --backend fake|cpp` drives either interchangeably and the
// parity harness extends unchanged (P6 pattern). Subcommands / methods:
//   flag-status | list | query | invoke | bindings-set | bindings-list |
//   bindings-clear | menu-model | register
// Every result is a single canonical JSON line ({"ok":...} | {"error":...}),
// exit 0 for a typed result, 2 for usage. No exceptions cross the boundary.
//
// The registry is loaded from <store-dir>/registry.json (commands-registry-v1)
// or seeded from the checked-in first-20 roster when absent. The availability
// snapshot is pinned (pinned-at-version law); dials write P6 trust-bindings.
#pragma once

#include <string>

#include "commands/core/availability.h"
#include "commands/core/policy_state.h"
#include "commands/core/registry.h"
#include "commands/core/shortcuts.h"

namespace xr::commands::host {

struct Context {
  Registry registry;
  ShortcutStore shortcuts;
  PolicyState policy;
  std::string flag_registry = "on";  // xr_command_registry_v1
  bool store_dir_empty = true;       // ephemeral (no persistence)
  std::string registry_path;         // <store-dir>/registry.json ("" if ephemeral)
  bool FlagOn() const { return flag_registry != "off"; }
};

// Load registry (seed from roster if absent) + shortcuts + flag. `store_dir`
// "" => ephemeral (seeded roster, in-memory shortcuts, no persistence).
bool LoadContext(Context* ctx, const std::string& store_dir,
                 const std::string& roster_path, std::string* error);

// Handle one protocol method. `args` is the method's args object. Returns the
// canonical JSON result ({"ok":...} | {"error":...}).
std::string HandleMethod(const std::string& method, const JsonValue& args,
                         Context& ctx);

}  // namespace xr::commands::host
