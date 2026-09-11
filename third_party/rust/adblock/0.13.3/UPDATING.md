# UPDATING.md — how this pin moves (P11-T1)

A pin update is a deliberate, recorded event — never a side effect.

## Bumping adblock (or re-vendoring at the same version)

1. **Research first** (xr-browser `docs/state/research-log-P11.md` R1):
   new version + date, license delta, RustSec/GHSA advisories, MSRV, API
   surface changes. Update `docs/dependencies/adblock-rust.yaml`
   (`evaluated_on`, sources).
2. **Pin the new crate sha256** from crates.io (the version page's checksum)
   and edit `tools/vendor_rust.py`: `ROOT_CRATE`, `ROOT_SHA256`. The lock
   travels inside the tarball — there is nothing else to pin.
3. `python3 tools/vendor_rust.py --plan` — closure + licenses + sizes; a
   license with no allowed branch or an unknown crate is a STOP (DR-04).
4. `python3 tools/vendor_rust.py --vendor --as-of YYYY-MM-DD` — writes the
   NEW version directory (refuses to overwrite an existing pin), refreshes
   `vendor/`, `Cargo.lock`, `supply-chain/`. GHSA hits abort the vendor.
5. Update `.cargo/config.toml` consumers, the DEPS pin, and the
   `shield-vendor` lane; run `tools/vendor_check.py --check`; run the T8
   parity gate (±2 % agreement, FP ≤ 0.5 %) — a pin bump without parity is
   a red, not a shrug.
6. Retire the old version directory only in the SAME commit that moves every
   consumer (no dangling pins), and append the bump to the research log.

## Extending the vendored feature set

`css-validation` (cosmetic filtering; pulls cssparser/selectors and their
closure) is OFF at this pin because the T1 build graph is default-features.
To extend: `--vendor --features css-validation --out <tmp>` and copy the
additional `vendor/<crate>-<ver>/` directories in (they are lock-verified
the same way), then re-run `vendor_check.py --check` and refresh
`supply-chain/`. The root crate directory never changes with features —
feature selection happens in the CONSUMER's `Cargo.toml` (the T2 FFI shim).

## What is NOT here, deliberately

* dev-dependency subtrees (criterion/reqwest/tokio/aws-lc/plotters/clap…):
  ~150 crates / tens of MB, some C sources, and upstream's network-fetching
  tests — the hosted `shield-vendor` lane runs `cargo build --offline` on
  the vendored graph and the crate's unit tests with a runner-side cargo
  fetch (the sanctioned-lane pattern, like pip installs in other lanes).
* fuzz/neon workspace members (`adblock-fuzz`, `adblock-rs`): the published
  crate tarball is library-only; the tarball's own lock already excludes
  their subtrees.
