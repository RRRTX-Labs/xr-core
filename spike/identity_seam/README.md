# P4 spike kit — the identity seam

**Status:** compile-ready, NOT compiled here. Every claim in this directory is
either a `file:line@pin` citation or explicitly `PENDING-FARM`.

**Pin:** Chromium `d04cdb24d67b081f6cf80200ffc5233f44b61109` (152.0.7977.82)

## What this kit is for

Plan §1.4 claims XR identities can be per-`WebContents` `StoragePartitionConfig`
domains inside ONE profile. P4's job is to validate or falsify that at source.
This kit is the instrument: hook + probes + drivers, ready for the farm to
execute (HG-21).

## The seam, as measured at the pin

**Use `SiteInstance::CreateForFixedStoragePartition()` — not the embedder
override.**

| step | evidence (`file:line@pin`) |
|---|---|
| the factory | `content/public/browser/site_instance.h:252-256` — *"create a SiteInstance in a new BrowsingInstance with a custom StoragePartition that is preserved across navigations"* |
| it needs a non-default config | `content/browser/site_instance_impl.cc:248-255` (`CHECK(!partition_config.is_default())`) |
| a new BrowsingInstance is created | `content/browser/site_instance_impl.cc:160-165` |
| the partition is pinned on first registration | `content/browser/browsing_instance.cc:178-183` (`CHECK_EQ`) |
| later SiteInstances inherit it | `content/browser/browsing_instance.cc:264-267` |
| renderer reuse requires the same partition | `content/browser/renderer_host/render_process_host_impl.cc:4947-4955` |
| one RPH ↔ one partition | `content/browser/renderer_host/render_process_host_impl.h:197-200` |

The seam must be taken **before `WebContents::Create()`** — which is why the
candidate patch hooks `CreateTargetContents()` in
`chrome/browser/ui/navigator/browser_navigator.cc:479`.

## What this spike disproves

1. **§1.4's named API does not exist at the pin.** There is no
   `GetStoragePartitionConfigForSiteInstance`. The override is
   `ContentBrowserClient::GetStoragePartitionConfigForSite(BrowserContext*,
   const GURL&)` (`content/public/browser/content_browser_client.h:1208`),
   consulted from `content/browser/site_info.cc:819`.
2. **…and it could not carry identity even if the name were right.** It is
   keyed on `(profile, site)`, so two identities visiting the same site in one
   profile are indistinguishable. `xr_seam_override.cc` is in the kit
   precisely so the farm can measure that collision rather than trust prose.
3. **The `WebContentsUserData` timing race is not the real risk.** A UserData
   consulted *during* partition selection would race the first navigation; the
   correct hook runs before the WebContents exists, so no race exists.
   `XRIdentityTabData` is therefore a *record* for later lookups only.

## Contents

```
spike/
  BUILD.gn                      # xr_spike buildflag + identity_seam + probes
  identity_seam/
    xr_identity.{h,cc}          # Identity, SiteInstanceForIdentity(), XRIdentityTabData
    xr_seam_override.{h,cc}     # the (profile, site)-keyed consult + why it's not the seam
    probes/*.cc                 # 5 browsertests -> spike-result-v1 rows
  patches/0042-seam-hook/       # CANDIDATE patch (manifest-entry: NOT-YET)
```

## Drop-in on a farm branch

```bash
# 1. mount xr-core (from the meta repo)
./scripts/build sync --checkout ./chromium

# 2. apply the candidate patch to the Chromium checkout
git -C chromium/src apply path/to/spike/patches/0042-seam-hook/0042-seam-hook.patch

# 3. build + run the probes with isolation ON (the driver refuses otherwise)
./scripts/build spike --farm --checkout ./chromium
```

The patch round-trips cleanly today without a Chromium checkout:

```bash
./scripts/build spike genpatch     # fetch at pin -> apply -> verify -> revert
```

## Non-production behaviour (must change before promotion)

See `patches/0042-seam-hook/patchinfo.md`: fail-open on bind failure,
spike-only identity resolution, and no identity persistence.
