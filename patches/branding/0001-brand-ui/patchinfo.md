# patchinfo — 0001-brand-ui

- **id:** 0001-brand-ui
- **title:** De-brand the primary product-name strings to XR Browser
- **owner:** @xr/platform
- **category:** branding
- **files:** chrome/app/theme/chromium/BRANDING, chrome/app/chromium_strings.grd
- **upstream-bug-if-any:** none
- **retirement plan:** these two files are the canonical Chromium brand inputs;
  they are overridden for as long as XR ships a distinct brand (no planned
  upstreaming — a distinct brand is the point). If HG-5 trademark outcomes
  change the brand token, this patch is regenerated, never hand-hacked.
- **rebase-notes:** the BRANDING file is 10 lines and stable across milestones;
  chromium_strings.grd is regenerated per train — the hunk context is the
  IDS_PRODUCT_NAME / IDS_SHORT_PRODUCT_NAME `<else>` branch. If the grd is
  restructured (e.g. the chrome-for-testing `<if>` block moves), regenerate the
  patch from the pinned file via `buildsys/branding/gen_version.py`-style
  scripted rewrite, never by hand.

## Why this patch exists

The product is "XR Browser", not "Chromium" (brand: RRRTX Labs, Plan §4 fix
F1 / ADR-0003). Chromium's brand inputs live in two places: the BRANDING
key-value file (PRODUCT_FULLNAME, bundle id, installer names, copyright) and
the primary product-name GRD strings. De-branding is a build-time property
(P2-T6): these files are patched at application time rather than forked, so
the diff stays tiny and rebases stay cheap — the whole point of the
overlay-repo model (Plan §1.2).

## Upstream drift risk

- `chrome/app/theme/chromium/BRANDING`: low (rarely touched; a schema change
  would show as a full-file conflict).
- `chrome/app/chromium_strings.grd`: moderate — regenerated each milestone and
  the surrounding `<if expr="_is_chrome_for_testing_branded">` block moves.
  Detect: `xr-patch verify` failing against a new pin = rebase needed (P3 bot
  files the conflict to the owner).
