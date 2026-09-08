// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/xr/xr_identity_color_bar.h"

#include <string>

namespace xr {
namespace {

IdentityColorBarState g_state;

// Deterministic, data-only tint by trust tier (the Identity/website-tint path,
// layout-agnostic). Not a resolver decision — it only maps a trusted input
// (the pinned snapshot's tier) to a color; the TIER itself comes from P6.
constexpr unsigned int kTierArgb[3] = {
    0x2E7D32FF,  // kStandard  (green)
    0x1565C0FF,  // kShield    (blue)
    0x6A1B9AFF,  // kFortress  (violet)
};

}  // namespace

void XrIdentityColorBar::OnTabStripStateChanged(const std::string& active_identity,
                                                const std::string& trust_tier) {
  g_state = IdentityColorBarState();
  if (active_identity.empty()) {
    return;  // no active Identity -> bar hidden (argb stays 0)
  }
  // Identity vocabulary (L21): label says Identity, never "container".
  g_state.label = active_identity;
  g_state.trust_tier = trust_tier;
  if (trust_tier == "kStandard") {
    g_state.argb = kTierArgb[0];
  } else if (trust_tier == "kShield") {
    g_state.argb = kTierArgb[1];
  } else if (trust_tier == "kFortress") {
    g_state.argb = kTierArgb[2];
  }
}

const IdentityColorBarState& XrIdentityColorBar::LastState() {
  return g_state;
}

std::string XrIdentityColorBar::PinnedActiveIdentity() {
  // P16 glue feeds the pinned P6 snapshot's active identity. Until then: none.
  return std::string();
}

std::string XrIdentityColorBar::PinnedTrustTier() {
  // P16 glue feeds the pinned P6 snapshot's trust tier. Until then: none.
  return std::string();
}

}  // namespace xr
