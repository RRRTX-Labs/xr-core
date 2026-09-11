# README.xr.md — adblock-rust 0.13.3, vendored verbatim (P11-T1)

This directory is the **unmodified** published crate `adblock 0.13.3`
(crates.io tarball), vendored on 2026-09-11 through the xr-browser network
chokepoint (`build/upstream/fetch.py`, ADR-0044 allowlist ceremony for
`static.crates.io`).

**Pin facts** (all machine-checked by `tools/vendor_check.py --check`):

| fact | value |
|---|---|
| crate tarball sha256 | `f44b96a666a23c12acad7c688bfe8638a7094e7eabe765b09a6864ab991c676d` |
| upstream tag / commit | `v0.13.3` / `886d45dcf5` (brave/adblock-rust) |
| published | 2026-08-20 (crates.io) |
| license | MPL-2.0 (`LICENSE` here is the verbatim upstream file) |
| per-file digests | `MANIFEST.sha256` (upstream bytes), `MANIFEST.xr.sha256` (xr-authored: this file + UPDATING.md) |
| dependency truth | the tarball's OWN `Cargo.lock` (crate-specific, inside the sha256-pinned bytes), copied verbatim to `../../Cargo.lock` |

**Zero local modifications.** Not one byte of upstream code is edited here —
a modified vendored crate would be indistinguishable from a supply-chain
attack, so the house rule is: patches go upstream or into `xr-core/patches/`
with a manifest entry, never into `third_party/`. `MANIFEST.sha256` proves it.

**Building.** `cargo build --offline` from this directory resolves every
default-feature dependency from `../../vendor/` (source replacement in
`../../.cargo/config.toml`). The `css-validation` feature (cosmetic
filtering: cssparser/selectors) is NOT vendored at this pin — see
UPDATING.md for the extension path. Dev-dependencies (criterion, reqwest,
tokio, …) are deliberately not vendored: the honest `--offline` boundary is
the build graph (`../../supply-chain/PROVENANCE.md`).

**Supply chain.** `../../supply-chain/` carries the per-crate license record
(SPDX branch choices recorded per crate), the GHSA advisory scan (14 reviewed
advisories affecting vendored names: 17 range checks, 0 hits), and the full
provenance chain. Evaluation: `xr-browser/docs/dependencies/adblock-rust.yaml`.
