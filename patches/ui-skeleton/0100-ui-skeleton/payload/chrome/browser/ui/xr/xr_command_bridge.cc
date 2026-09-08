// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/xr/xr_command_bridge.h"

#include <string>

namespace xr {
namespace {

// L16-clean: local counter only (no network, no telemetry). A non-zero
// count proves the seam fired during a session; the real dispatch result is
// served by commands_host via the P16 mojom glue.
int g_menu_dispatches = 0;
bool g_toolbar_slots_registered = false;

}  // namespace

std::string XrCommandBridge::OnAppMenuCommand(int command_id,
                                              const std::string& source) {
  (void)command_id;
  // This is a chrome:// UI call site (the app menu), NOT a renderer callback:
  // a page-originated invocation cannot reach it. The source tag passed in is
  // "menu" (a user surface in the host's allowlist). The host's cross-process
  // origin check is defense in depth on top of that structural fact.
  if (!source.empty()) {
    ++g_menu_dispatches;
  }
  // P16 glue: forward (command, source, confirmed) to commands_host over the
  // mojom channel and return its canonical {"ok":...} result. Until then: no-op.
  return std::string();
}

void XrCommandBridge::RegisterToolbarSlots() {
  // Idempotent data registration of the three chrome identity/trust slots:
  // identity pill, trust dial, shield chip. Layout realization is P16/farm.
  g_toolbar_slots_registered = true;
}

}  // namespace xr
