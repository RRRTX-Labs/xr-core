# xr-core

The XR Browser product repository — the `//xr` tree that will be checked out
at `chromium/src/xr` inside a pinned Chromium checkout (overlay-repo model,
Plan §1.2 / §7.1; Brave-pattern).

**License:** MPL-2.0 (see `LICENSE`; choice recorded in
`xr-browser/docs/adr/0001-repo-topology-and-licensing.md`).

## Current state (2026-09-07 — end of Phase P1)

**Governance files only.** This repository intentionally contains:

- `LICENSE` (MPL-2.0)
- `README.md` (this file)
- `OWNERS` + `CODEOWNERS` (S0 path set per Plan §7.2 / L13; CODEOWNERS is
  generated from `OWNERS` + `xr-browser/docs/process/s0-paths.yaml` by
  `xr-browser/tools/owners_sync.py` — never edit by hand)
- `CONTRIBUTING.md`
- `.clang-format` (Chromium-style base; final args reviewed in P2)
- `.gitignore`

**Deliberately absent (anti-fabrication):** no source directories, no
`BUILD.gn`, no `DEPS`, no `patches/manifest.yaml`. Those are created in
P2 (build system), P3 (patch ledger) and P5 (contract freeze / `mojom/`) per
Plan §4 — premature stubs that imply they exist violate the orchestrator's
anti-fabrication rule and Plan L24.

## Planned topology (Plan §7.1)

`common/ mojom/ policy/ identity/ shield/ net/ fingerprint/ permissions/
extensions/ vault/ downloads/ commands/ tools/ ui/ components/ test/`
— directories land as their owning phases (P5+) create them; every directory
gets an OWNERS entry at creation time.

## Governance pointers (meta repo `xr-browser/`)

| Artifact | Path |
|---|---|
| Master implementation plan (hash-pinned) | `xr-browser/docs/XR_BROWSER_MASTER_IMPLEMENTATION_PLAN.md` |
| Decision register (DR-01..DR-30) | `xr-browser/docs/register/decisions.yaml` |
| ADRs | `xr-browser/docs/adr/` |
| Threat model v0 | `xr-browser/docs/threat-model.md` |
| Security policy / disclosure | `xr-browser/SECURITY.md`, `xr-browser/docs/process/disclosure-policy.md` |
| Dependency evaluations (L9) | `xr-browser/docs/dependencies/` |
