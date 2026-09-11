# shield/engine — the adblock-rust FFI shim (HOSTED-LANE SKELETON, P11-T2)

## What this is

The Rust side of the `BlockingEngine` injection point
(`shield/core/engine.h`): a cdylib exposing the C ABI in
`xr_shield_engine.h`, implemented over the VENDORED adblock-rust pin
(`third_party/rust/adblock/0.13.3`, T1 — sha256-pinned, advisory-checked,
offline-buildable).

## What this is NOT

* **Not locally built.** There is no cargo toolchain requirement anywhere
  in the off-tree lanes; this crate compiles in the HOSTED lane
  (`.github/workflows/core-hardening.yml`, the `shield-vendor` job's
  toolchain, source replacement via `third_party/rust/.cargo/config.toml`
  — the shim's own `.cargo/config.toml` points at the same vendor dir).
* **Not wired into the decision path.** `shield_host` and every local
  test bind the TEST-ONLY `TableEngine` (`shield/core/fake_engine.h` —
  the v1 reference matcher). `xr_shield_engine_match` deliberately
  returns "no opinion" until T8 completes the request mapping: an
  unfinished binding that under-blocks is honest and measurable; one
  that half-matches is neither.
* **Not a GN target.** `shield/BUILD.gn` says so explicitly — a GN row
  claiming in-tree compilation would be a lie until the network-service
  seam lands (farm glue).

## T8 completion list (the parity-measured binding)

1. `xr_shield_engine_match`: build `adblock::Request` from the
   caller-redacted parts (`scheme://host/path`, host, path,
   registrable_domain — the same redaction the events ledger uses),
   resolve first/third-party from the resolver snapshot, and map the
   `Blocker` result back through `rule_table` to `(list_index,
   rule_index)` so the C++ side recovers `rule_id`/`list_id`/filter text.
2. Redirect/replace resources: the `resources` table + adblock-rust's
   redirector, with the borrow contract in `xr_shield_engine.h`
   (`redirect_resource` valid until the next match/free).
3. Death semantics: a caught panic in any export flips `alive` to false
   (`kill_for_test` is the observable seam the C++ posture property tests
   drive — fail-OPEN + amber for engine death; route loss stays
   fail-CLOSED no matter what this shim does).
4. `tools/shield_parity.py` (xr-browser): the vendored corpus (≥1,500
   cases, `shield/tests/corpus/`) replayed through BOTH engines —
   agreement ±2%, false-positive rate ≤0.5% — recorded in evidence with
   the hosted run id.

## Files

| file | role |
|---|---|
| `xr_shield_engine.h` | the C ABI (layout law; both sides cite it) |
| `lib.rs` | the Rust implementation (skeleton stage, see markers) |
| `Cargo.toml` | cdylib + path deps into the vendored pin |
| `.cargo/config.toml` | source replacement to `third_party/rust/vendor` (offline) |
