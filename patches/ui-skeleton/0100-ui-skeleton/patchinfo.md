# patchinfo — 0100-ui-skeleton

- **id:** 0100-ui-skeleton
- **title:** Register the P7 chrome/browser/ui command seam (app-menu + toolbar
  slots + identity color-bar data path)
- **owner:** @xr/platform
- **category:** ui
- **files:** chrome/browser/ui/views/BUILD.gn,
  chrome/browser/ui/views/toolbar/app_menu.cc,
  chrome/browser/ui/views/toolbar/toolbar_view.cc,
  chrome/browser/ui/views/frame/browser_frame_view.cc,
  chrome/browser/ui/xr/BUILD.gn,
  chrome/browser/ui/xr/xr_command_bridge.h,
  chrome/browser/ui/xr/xr_command_bridge.cc,
  chrome/browser/ui/xr/xr_identity_color_bar.h,
  chrome/browser/ui/xr/xr_identity_color_bar.cc
- **upstream-bug-if-any:** none (XR feature seam; nothing to upstream)
- **retirement plan:** the four upstream hook one-liners exist for as long as the
  command registry (P7) ships and are superseded by the P16 chrome:// glue, which
  moves the invocation path onto the commands_host mojom channel; at that point the
  hook call-sites are re-pointed (regenerated from the pin via
  `build/webui/patch_roundtrip.py`, never hand-hacked). The //chrome/browser/ui/xr
  payload files are XR-owned and live until the registry is retired (P13+ note:
  the `xr_command_registry_v1` flag is kept until P13).
- **rebase-notes:** every hook is anchored to a UNIQUE line in the pinned file
  (asserted by the round-trip tool; a non-unique or drifted anchor fails the
  round-trip, so a rebase is detected, not guessed). The anchors are: the app_menu
  include + `AppMenu::ExecuteCommand(int,int) {`, the toolbar_view include +
  `ToolbarView::Init() {`, the browser_frame_view include +
  `BrowserFrameView::OnTabStripStateChanged() {`, and the `views` component dep
  `//chrome/browser/ui:layout_constants`. If any of those functions is renamed or
  the BUILD.gn dep list is restructured, regenerate the patch from the pin.

## Why this patch exists

Plan §4 P7 / §10: the command registry is the "spine UI" — one registry, four
views (palette, menus, shortcut editor, help index) — and "any feature that cannot
express itself as a command does not ship." This is the first chrome/browser/ui
consumer: it registers the seams through which the chrome:// UI talks to the
command registry (the app-menu group, the toolbar identity/trust/shield slots, and
the per-tab identity color-bar data path). It is deliberately a THIN DATA PATH:
all dispatch / source-tag / id-whitelist / confirmation logic lives in
`commands_host` (C++, P7); the bridge in `//chrome/browser/ui/xr` only forwards
user-initiated (non-page) invocations with the `menu`/`ui-chrome` source tag and
serves generated menu-model data. Layout realization and the mojom channel to
commands_host are P16 glue (HG-31). A patch is the chosen mechanism (Plan §1.2
intake order: components before patches) so the diff stays tiny and rebases stay
cheap. The ≤12-file budget is law: this set is 9 files (4 upstream + 5 XR-owned
payload).

## Upstream drift risk

- `chrome/browser/ui/views/BUILD.gn`: low-moderate — the `views` component dep
  list is re-sorted each milestone; the anchor is the `//chrome/browser/ui:layout_constants`
  dep line. Detected by the round-trip anchor-uniqueness assert.
- `chrome/browser/ui/views/toolbar/app_menu.cc`: moderate — `AppMenu::ExecuteCommand`
  is a stable public method but the file is large and touched often; the hook is a
  single line at the function entry.
- `chrome/browser/ui/views/toolbar/toolbar_view.cc`: moderate — `ToolbarView::Init`
  entry; stable.
- `chrome/browser/ui/views/frame/browser_frame_view.cc`: moderate —
  `BrowserFrameView::OnTabStripStateChanged` entry; stable but the tab-strip
  region view is an active upstream area.
- `chrome/browser/ui/xr/*` (XR-owned): none (our files).

Detection for all: `build/webui/patch_roundtrip.py` (real pin fetch,
apply/verify/revert byte-exact + a perturbed-anchor negative) — any drift fails
the round-trip and the P3 rebase bot files it to the owner.
