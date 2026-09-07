# //xr/policy — the one brain

The policy resolver for XR Browser (Plan §4 P6): a **pure, total,
deterministic** function from (identity, origin/site, trust context,
request class) × (layers) to `EffectivePolicy` v1 — plus the persistence,
cache, snapshot-distribution, event, and enterprise seams around it.

## Laws (what makes this module what it is)

- **L3 — one resolver.** Unknown/absent policy ⇒ **deny, never guess**.
  Nothing outside `//xr/policy` may make trust/mode decisions
  (`tools/mode_lint.py` in the meta repo enforces; see `mode_lint.cfg`).
- **Total.** Every input produces an `EffectivePolicy` (the deny policy is
  a *value*, not an error). The single error arm is `kVersionMismatch`,
  surfaced deny-safe.
- **Pure.** `Resolve()` reads only its inputs: no clock, no RNG, no
  environment, no I/O. `now_ms` and `session_id` are plain input fields —
  expiry is evaluated *at input*, inside the pure function.
- **Frozen-core parity.** The (identity, origin, trust, class) core is
  byte-parity with `fakes/policy_resolver.py` (the P5-frozen behavioral
  reference) on all 66 golden vectors, proven in C++
  (`tests/test_vectors.cc`) and cross-language (`tools/vectors_check.py`
  parity mode via the stdio façade). Vectors are the arbiter on any
  disagreement.

## Layout

| Path | What |
|---|---|
| `core/json.{h,cc}`, `core/json_parse.cc` | strict JSON (RFC 8259, depth-capped, canonical output byte-compatible with the Python reference form) |
| `core/sha256.{h,cc}` | FIPS 180-4 SHA-256 (integrity fields; FIPS-vector-tested) |
| `core/effective_policy.{h,cc}` | EffectivePolicy v1 value type; deny-safe defaults; strict (de)serialization |
| `core/resolve.{h,cc}`, `core/resolve_io.cc` | the resolver + request parsing |
| `core/cache.{h,cc}` | (identity, site, trust) cache; generational invalidation; pinned reads (TOCTOU law) |
| `core/snapshot.{h,cc}` | versioned-blob codec: full + incremental diff, ≤32 KB budget, hash-verified, deny-on-corrupt |
| `core/store.{h,cc}` | JSON pref docs (identity-list/trust-bindings/exceptions) + forward migrations; downgrade = data-preserving no-op |
| `core/events.{h,cc}` | policy-change events: concrete deltas, undo affordance, no scores, no auto-reload |
| `core/service.{h,cc}` | service: store loading + merging, cache, snapshot sequencing, dump |
| `core/service_mojom.h` | farm-gated (HG-29) mojom adapter — `XR_HAVE_MOJOM_BINDINGS` |
| `enterprise/managed_source.{h,cc}` | file-based managed policy: signed-or-ignored (minisign scaffold) |
| `host/policy_host.cc` | stdio JSON façade (fake-protocol-compatible) |
| `tests/`, `bench/` | C++ test binaries + microbenchmark (`make test`, `make bench`) |

## Precedence ladder (documented truth, generated into docs/contracts/policy.md)

1. **Enterprise floor/forces** — clamps tier upward, forces fields (a
   signed *layer*, never a second brain; unsigned ⇒ ignored + ledger row)
2. **Explicit request trust** (the ⌥ dial for this context)
3. **Active exception** (in-flow, expiring: once / session / 7d; permanent
   only via Settings — never "forever" in-flow)
4. **Per-identity site binding** (⌥-stored)
5. **Per-site default binding**
6. `kStandard`

Invalid layer entries are **dropped, never widened** (fail-closed per
entry, ledger row per drop); malformed core requests ⇒ deny policy.

## What is farm-gated and how parity protects us

Real mojom bindings, PrefService/profile plumbing, and the in-tree
snapshot distribution are **HG-29** (first farm CI after HG-9). Until
then: the C++ core here is compiled and exhaustively tested **off-tree**
with pinned g++ flags (`policy/tests/Makefile`), the stdio façade speaks
the frozen fake protocol so `xrctl --backend fake|cpp` drives either
implementation interchangeably, and the golden vectors pin behavior
byte-for-byte. The mojom adapter (`core/service_mojom.h`) compiles only
under `XR_HAVE_MOJOM_BINDINGS`; nothing pretends otherwise.

## Dev quick-start

```sh
cd policy/tests
make test                        # build + run all C++ suites
XR_BROWSER_ROOT=../../.. make test   # explicit vectors path
make bench                       # p50/p99/p99.9 vs budgets
../tests/build/policy_host '{"identity":{"value":"xr:00000000-0000-4000-8000-000000000001"},"origin":{"scheme":"https","registrable_domain":"example.com"},"request_class":"kNavigation"}}'
```
