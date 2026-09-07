# Mounting xr-core into a Chromium checkout

xr-core is the **product** repo (upstream patches + `//xr` sources). It is not
buildable on its own: it is *mounted* into a Chromium checkout at `src/xr`, and
everything in it is then addressed from Chromium as `//xr/...`.

This is the Brave overlay pattern (Plan §7.1): one Chromium checkout, one
overlay directory, no fork of Chromium's own tree.

## How the mount happens (do not hand-roll)

```
./scripts/build sync --checkout ./chromium       # in xr-browser (meta repo)
```

`build/sync.py` synthesizes a gclient config whose solution declares the mount
via gclient's documented `custom_deps` field:

```python
solutions = [
  {
    "name": "src",
    "url": "https://chromium.googlesource.com/chromium/src.git",
    "deps_file": "DEPS",
    "custom_deps": {
      "src/xr": "https://github.com/RRRTX-Labs/xr-core.git",
    },
  },
]
```

and checks **xr-core out at the `xr_core_rev` recorded in the meta repo's
`DEPS`** — never at `main`. After syncing it verifies the mount:

```
mount mismatch: src/xr HEAD=<sha> expected <xr_core_rev>
```

is a hard failure (exit 1), not a warning.

## Layout after sync

```
chromium/
  src/                      # Chromium at DEPS.chromium_rev
    xr/                     # <-- xr-core, at DEPS.xr_core_rev
      BUILD.gn              # defines //xr:xr_all
      patches/manifest.yaml # patch budget ledger (read by xr-patch)
      patches/branding/0001-brand-ui/
      spike/identity_seam/  # P4 spike kit (not part of xr_all)
  .xr/egress.json           # egress manifest (zero-egress property)
```

## Building

```
./scripts/build gen      --checkout ./chromium --argset xr_release.gn
./scripts/build compile  --checkout ./chromium --argset xr_release.gn
```

`compile` defaults to the target `//xr:xr_all` — the group defined in this
repo's root `BUILD.gn`.

## Rules

1. **Never edit `src/xr` inside a checkout and expect it to persist.** It is a
   checkout of this repo; commit here, then bump `DEPS.xr_core_rev` in the
   meta repo (same day — see `docs/process/cross-repo-pin.md`).
2. **Never add a `src/xr` patch that lives outside `patches/`.** Patches are
   accounted in `patches/manifest.yaml` against the §1.2 budget; the spike kit
   under `spike/` is excluded from the ledger by design and is lint-enforced
   (`xr-patch lint`).
3. **Never re-pin Chromium or xr-core anywhere but the meta repo's `DEPS`**
   (Plan: "do not re-pin inside pins").

## Verifying the pin is alive

```
./scripts/build sync check-pin-alive    # in the meta repo
```

Fails (exit 1) if `DEPS.xr_core_rev` is not fetchable or is not an ancestor of
origin `xr-core` main — i.e. if a fresh clone could not reproduce the tree.
