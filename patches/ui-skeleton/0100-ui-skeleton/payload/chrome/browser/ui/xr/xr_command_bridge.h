// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — the chrome/browser/ui command bridge: the ONLY seam between the
// upstream chrome:// UI (app menu, toolbar slots, tab-strip) and the XR command
// registry (P7). Palette/menu/dispatch logic lives in commands_host (C++);
// this bridge is a THIN DATA PATH. It forwards user-initiated (non-page)
// invocations carrying the `menu` / `ui-chrome` source tag and serves the
// generated menu-model data. No business logic, no page-originated path
// (dispatch is host-enforced — see xr-core/commands/core/dispatch.cc).
//
// P7 scope: the hook + data path is registered here; the full chrome://
// integration (mojom channel to commands_host) is P16 glue (HG-31). Until then
// the methods are no-op-safe and L16-clean (local counters only).
#pragma once

#include <string>

namespace xr {

class XrCommandBridge {
 public:
  // Called from AppMenu::ExecuteCommand (the `menu` source). Forwards the
  // command to the command host with the given source tag. The host enforces
  // the source allowlist, id whitelist and confirmation gate; the bridge only
  // relays the host's typed result. Returns the canonical JSON result line
  // (empty when the registry flag is off -> stock chrome).
  static std::string OnAppMenuCommand(int command_id, const std::string& source);

  // Called from ToolbarView::Init. Registers the toolbar slot metadata
  // (identity pill / trust dial / shield chip) as data; layout is P16.
  static void RegisterToolbarSlots();
};

}  // namespace xr
