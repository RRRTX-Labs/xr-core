# shield/engine — the adblock-rust FFI shim (HOSTED-LANE, P11-T8 binding)

## What this is

The Rust side of the `BlockingEngine` injection point
(`shield/core/engine.h`): a cdylib exposing the C ABI in
`xr_shield_engine.h`, implemented over the VENDORED adblock-rust pin
(`third_party/rust/adblock/0.13.3`, T1 — sha256-pinned, advisory-checked,
offline-buildable). T8 completed the binding: `xr_shield_engine_match`
maps the caller-redacted URL parts to `adblock::request::Request`, runs
`Engine::check_network_request`, recovers the matched rule through the
debug-`raw_line` → `filter_index` → `rule_table` chain, enforces the v1
rule-option law (exact `domains`/`exclude_domains` membership against the
FFI-passed rd/host), and maps redirect-rule hits to action 2 + resource
name. A caught panic in any export flips `alive` to false — the death
observable the posture core fail-opens on (the release profile keeps
`panic = "unwind"` on purpose; `"abort"` would make `catch_unwind` a
no-op).

## What this is NOT

* **Not locally built.** There is no cargo toolchain requirement anywhere
  in the off-tree lanes; this crate compiles in the HOSTED lane
  (`.github/workflows/core-hardening.yml`, the `shield-vendor` job's
  toolchain, source replacement via `third_party/rust/.cargo/config.toml`
  — the shim's own `.cargo/config.toml` points at the same vendor dir).
  The lib.rs signatures were written against the vendored 0.13.3 source
  with path:line citations in its header comment; first compilation is
  the hosted lane's.
* **Not wired into the local decision path.** `shield_host` and every
  local test bind the TEST-ONLY `TableEngine` (`shield/core/fake_engine.h`
  — the v1 reference matcher). The real binding is measured by the T8
  parity job and the real-engine perf lane, both hosted.
* **Not a GN target.** `shield/BUILD.gn` says so explicitly — a GN row
  claiming in-tree compilation would be a lie until the network-service
  seam lands (farm glue).

## T8 status (was: completion list)

1. ✅ `xr_shield_engine_match` request mapping + rule recovery — done in
   lib.rs (division of labor: adblock-rust decides filter SHAPE, the shim
   enforces the v1 OPTION law; `$domain=` is deliberately not emitted —
   ABP subdomain semantics are broader than v1 exact membership).
2. ✅ Death semantics — `alive` is an `AtomicBool`; a caught panic flips
   it; `kill_for_test` is the observable seam the C++ posture property
   tests drive (fail-OPEN + amber for engine death; route loss stays
   fail-CLOSED no matter what this shim does).
3. ⚠️ Redirect/replace resources — v1 bundles carry resource NAMES only
   (no bodies): a redirect hit reports action 2 + the name; actual
   resource substitution is a documented v1 limit, not a shim gap.
4. `tools/shield_parity.py` (xr-browser) replays the ≥1,500-case corpus
   (`shield/tests/corpus/`) through the fake/TableEngine lane locally and
   through THIS shim (ctypes, hosted lane) with agreement ±2% / FP
   ≤0.5%; divergence classes are recorded in xr-browser
   `docs/shield/parity-divergences.md`.

## Files

| file | role |
|---|---|
| `xr_shield_engine.h` | the C ABI (layout law; both sides cite it) |
| `lib.rs` | the Rust implementation (T8 parity-measured binding) |
| `Cargo.toml` | cdylib + path deps into the vendored pin; unwind profile |
| `.cargo/config.toml` | source replacement to `third_party/rust/vendor` (offline) |
