# supply-chain/PROVENANCE.md — the pin chain for third_party/rust (P11-T1)

Every byte here traces to a sha256 that upstream published, fetched through
the ONE network chokepoint (`xr-browser/build/upstream/fetch.py`; host
allowlisted by ADR-0044 ceremony on 2026-09-11).

## The chain (each link machine-checked by tools/vendor_check.py)

1. crates.io publishes `adblock 0.13.3` with tarball sha256
   `f44b96a666a23c12acad7c688bfe8638a7094e7eabe765b09a6864ab991c676d`
   (tag `v0.13.3` = commit `886d45dcf5`, published 2026-08-20).
2. `tools/vendor_rust.py` fetched that tarball from `static.crates.io`
   through the chokepoint and REFUSED to unpack until the bytes hashed to
   the pinned sha256.
3. The tarball contains the crate's OWN `Cargo.lock` (crate-specific,
   sha256 `see licenses.json root.sha256_lock`) — every registry package in
   it carries a `checksum` = the sha256 of ITS `.crate` file. Resolution
   truth therefore never required a live index query (index.crates.io and
   crates.io stay OFF the allowlist by design).
4. Each of the 59 build-closure crates was fetched, verified against its
   lock checksum (mismatch = abort), extracted to
   `vendor/<name>-<version>/`, and sealed with cargo's own
   `.cargo-checksum.json` (per-file sha256 + the lock's package checksum).
5. The root crate's files are sealed in `adblock/0.13.3/MANIFEST.sha256`;
   xr-authored files (README.xr.md, UPDATING.md) in `MANIFEST.xr.sha256`.
   `Cargo.lock` at this level is a byte-identical copy of the tarball's.

## Closure rule (what is vendored and why)

Feature-aware BUILD closure from `default` features: non-dev dependencies
only (build-deps and all target platforms included), optional deps only
where reachable from enabled features AND resolved by the lock at a version
satisfying the edge's semver requirement. Dev-dep-only subtrees are
excluded — the honest `--offline` boundary (see adblock/0.13.3/UPDATING.md).
`cargo build --offline` in the hosted `shield-vendor` lane is the empirical
gate on the closure's completeness; `cargo test` there fetches dev-deps
runner-side (sanctioned-lane network, like pip), which is a RECORDED
DEVIATION from a literal all-offline test run.

## Advisory + license posture (research item 7's substitute)

`cargo vet`/`cargo audit` cannot run in this sandbox (no cargo) and
cargo-vet's imports/criteria model presumes a live index; the honest
substitutes, all executed 2026-09-11 through the chokepoint:

* **advisories.json** — every reviewed GHSA advisory affecting a vendored
  crate name (`affects=<crate>` queries; bulk listing is deep-pagination
  capped): 14 advisories, 17 crate-range checks, **0 hits, 0 NEEDS-REVIEW**
  (an unparseable range would be recorded as NEEDS-REVIEW, never a silent
  miss). Re-run at every pin bump (UPDATING.md step 3-4 aborts on hits).
* **licenses.json** — per-crate SPDX expression + the CHOSEN allowed branch
  (disjunctions are a decision, recorded; AND-groups need every part
  allowed; unknown identifiers are red). Root: MPL-2.0 (our own license).
* xr-browser `tools/license_audit.py` keeps its copyleft-marker law over
  xr-browser; this tree's per-crate record is the xr-core-side substitute
  until a farm machine can run cargo-vet against the vendored layout
  (recorded as an open follow-up, not a claimed check).
