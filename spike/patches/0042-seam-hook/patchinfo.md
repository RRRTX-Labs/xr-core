# 0042 — identity seam hook (CANDIDATE)

manifest-entry: NOT-YET (candidate; promotion ⇒ budget accounting + S0 review)

- **status:** CANDIDATE — deliberately **absent** from `patches/manifest.yaml`
- **category (on promotion):** `hook_points` (+ `content_seams` if the network
  half is promoted)
- **owner:** @xr/platform
- **phase:** P4 spike (ADR-0042)
- **pin:** Chromium `d04cdb24d67b081f6cf80200ffc5233f44b61109` (M152.0.7977.82)
- **round-trip proof:** `./scripts/build spike genpatch`
  (`work/spike-checkout/spike-patch-roundtrip.json`)

## What it does

Routes a new tab's **initial** SiteInstance through
`xr::SiteInstanceForIdentity()`, which pins it to a per-identity
`StoragePartitionConfig` (`xr:<uuid>`) before the `WebContents` exists.

Files touched — all inside `chrome/browser/**` (§12.7 never-list enforced by
`build/spike/seam_spec.py`, which refuses `content/`, `third_party/`, `v8/`, …):

| path | change |
|---|---|
| `chrome/browser/ui/navigator/browser_navigator.cc` | +2 includes; the initial-SiteInstance selection in `CreateTargetContents()` consults the identity, guarded by `ENABLE_XR_SPIKE` |
| `chrome/browser/ui/navigator/BUILD.gn` | wires the spike sources behind `enable_xr_spike` |
| `chrome/browser/xr/xr_identity.{h,cc}` | new — identity struct, `SiteInstanceForIdentity()`, `XRIdentityTabData` (WebContentsUserData) |
| `chrome/browser/xr/xr_seam_override.{h,cc}` | new — the `GetStoragePartitionConfigForSite()` consult, with the measured finding that it **cannot** carry per-tab identity |

## Why it is not in the manifest

A candidate patch is not yet a decision. Entering it in `patches/manifest.yaml`
would spend budget and apply an S0-path change on the strength of a
static measurement. `xr-patch lint` therefore **permits** candidate dirs under
`spike/` with this header and **fails** on any other unmanifested directory —
so a candidate can never quietly become a shipped patch.

## Known non-production behaviour (must be fixed before promotion)

1. **Fail-open.** If `SiteInstanceForIdentity()` returns null the tab opens in
   the profile default partition with a `LOG(ERROR)`. Plan §8.3 requires
   **fail-closed**; the spike logs so the probes can measure the path.
2. **Spike-only identity resolution.** `xr::IdentityForNewTab()` is driven by
   process-local test state. Production resolution is the P5
   `IdentityProvisioning` surface.
3. **No identity persistence.** `IdentityRegistry` is in-memory and
   process-local by design; a spike must not invent credential storage.

## Promotion checklist

- [ ] runtime probe results from the farm (HG-21) confirm the measured rows
- [ ] fail-closed on bind failure
- [ ] `allowed_roots` in `patches/manifest.yaml` extended to `chrome/browser/`
- [ ] budget accounting: files counted against `hook_points` (cap 45)
- [ ] S0 review (the navigator is on `docs/process/s0-paths.yaml`)
