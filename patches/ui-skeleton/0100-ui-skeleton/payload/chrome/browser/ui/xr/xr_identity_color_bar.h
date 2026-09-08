// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — the identity color-bar DATA path (vocabulary law L21: this is
// the Identity system's first visual surface; the word "container" resolves to
// Identity and is never used in strings). Per §10 the color bar is the
// per-Identity/website-tint path — data only here, layout-agnostic. The tabstrip
// tint hook (P4/P6 prior art: Brave's container-tint approach) consumes the
// color produced by ResolveColor; the actual paint is P16/farm (HG-31).
//
// No page-originated input: the color is derived from the PINNED P6 snapshot
// (identity + trust tier), never from the renderer.
#pragma once

#include <string>

namespace xr {

// Resolves the Identity color-bar state for a site from the pinned snapshot.
// Deterministic: (active identity, trust tier) -> color + label. Pure data.
struct IdentityColorBarState {
  // 0xRRGGBBAA, or 0x00000000 when no Identity is active (bar hidden).
  unsigned int argb = 0;
  // The Identity pill label (Identity vocabulary, never "container").
  std::string label;
  // The trust tier that tints the bar (kStandard|kShield|kFortress).
  std::string trust_tier;
};

class XrIdentityColorBar {
 public:
  // Called from BrowserFrameView::OnTabStripStateChanged. Recomputes the
  // color-bar state for the active tab from the pinned snapshot (data path;
  // paint is P16).
  static void OnTabStripStateChanged(const std::string& active_identity,
                                     const std::string& trust_tier);

  // The most recently computed state (for the P16 paint glue to read).
  static const IdentityColorBarState& LastState();

  // The active Identity / trust tier from the PINNED P6 snapshot (the only
  // input the color is derived from — never page-originated). P7 returns
  // empty (the snapshot channel is P16 glue); the data path is registered now.
  static std::string PinnedActiveIdentity();
  static std::string PinnedTrustTier();
};

}  // namespace xr
