# patchinfo — 0400-permission-prompt-order

- **id:** 0400-permission-prompt-order
- **title:** Permission prompt: one-time first (order only), so keyboard focus lands on the one-time grant
- **owner:** @xr/platform
- **category:** ui
- **files:** chrome/browser/ui/views/permissions/permission_prompt_bubble_base_view.cc — 1 file, within the P15 brief's ≤4-file budget under chrome/browser/ui/views/permissions/
- **upstream-bug-if-any:** none (an embedder UX choice; upstream's order is not a defect)
- **retirement plan:** obsolete when the farm-side //xr permission UI renders the prompt from xr-core's PromptModel (permissions/core/present.h) through a delegate instead of the upstream bubble; or superseded if upstream exposes a button-order hook. Retirement goes through `xr-patch retire` (P3-T7).
- **rebase-notes:** one transform, anchored to the three `buttons_container->AddChildView(...)` lines of the one-time branch of `PermissionPromptBubbleBaseView::CreatePermissionButtons` (live-read at pin d04cdb24d67b081f6cf80200ffc5233f44b61109 on 2026-10-10; research-log-P15.md C7). The anchor must occur exactly once. `build/webui/permission_patch_roundtrip.py --check` re-fetches, regenerates and byte-compares this patch every run, and refuses any added line that is not a guard, a comment or an existing upstream line (order only). The non-one-time branch (notifications: OK/Cancel dialog buttons, platform-ordered) is deliberately NOT patched — see ADR-0051 addendum (C7) and HG-P15-UX.

## Why this patch exists

The overlay core decides the prompt's presentation as data: for sensitive
capabilities (geolocation, camera, microphone) the one-time button comes first
(plan line 808; `ButtonOrder` in xr-core permissions/core/present.cc, asserted
by test_present.cc and by docs/qa/keyboard-tasks-permissions.yaml). Upstream
stacks "Allow while visiting" above "Allow this time" in the one-time branch,
so keyboard focus lands on the longer grant. There is no public API to reorder
the bubble's buttons: they are built inside a private method of the view, so a
component cannot change the order (Plan §1.2 intake order: components before
patches — no component seam exists here). The patch is guarded by
`ENABLE_XR_PERMISSIONS`; with it undefined, stock Chromium compiles the upstream
order unchanged.

## Upstream drift risk

Medium: the permission bubble views are refactored a few times a year. If the
three AddChildView lines change (rename, reorder, a fourth button), the anchor
is not found and the round-trip gate reddens at the pin — never a fuzzy apply.
Detection: `build/webui/permission_patch_roundtrip.py` in run_checks; the
planted-drift negative is tools/negatives/p15c_c7.sh.
