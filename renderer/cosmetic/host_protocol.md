# cosmetic_host protocol (P12)

JSON over stdio. One request object on `argv[1]` or stdin, one canonical JSON
line on stdout (sorted keys, no whitespace, `\uXXXX` non-ASCII). Same
conventions as `shield_host` and `update_host` (`fakes/README.md`).

## Exit law

- **0** — ok **or** a typed rejection. A refusal is a normal result, not a
  failure: the cosmetic surface refuses far more often than it accepts, and
  treating every refusal as a non-zero exit would make the golden vectors
  indistinguishable from crashes.
- **1** — typed error: the request itself was unusable (malformed JSON, a
  missing required field, an unknown method). A caller that misspells a method
  must find out rather than get a silent no-op.
- **2** — usage: bad argv.

Rejections carry `{"error":"kRejected","reason":"<closed vocabulary>"}` and
exit 0. Errors carry `{"error":"<ErrorCode>","detail":...}` and exit 1.

## Methods

| method | args | result |
| --- | --- | --- |
| `flag-status` | `{}` | `{"xr_shield_cosmetic_v1":"on\|off","xr_shield_scriptlets":"on\|off","scriptlet_registry_state":…}` — `scriptlet_registry_state` is `"inert: flag off"` verbatim when the scriptlet flag is off, produced here rather than in the UI so the two cannot drift; both flags default off; flag-independent |
| `selector-parse` | `{selector}` | `{canonical, compounds, pseudos}` — parsed by the SAME parser the renderer uses, so a rule this host accepts is a rule the renderer can compile; refusal ⇒ typed `kRejected` with a `reason` from `SelectorErrorName()` (closed 24-value vocabulary); `canonical` is the dedup key, so text variants collapse |
| `scope-key` | `{frame_site, frame_identity, trust?, navigation?, url_class, embedder_site?}` | `{hex, partition}` — the cache/identity key. **`embedder_site` is REFUSED, not ignored**: a non-empty embedder ⇒ `kRejected` `embedder-refused`, which makes "key on the top-level site only" structurally impossible rather than merely discouraged; `trust` ∈ `anonymous\|authenticated\|enterprise`, `navigation` ∈ `initial\|cross-document\|same-document`, an unknown value is a typed error not a default because silently defaulting would collapse two partitions |
| `blob-build` | `{blob_id, generated_epoch, scope, rules[]}` | `{blob, bytes, sha256}` — the producer path: builds a `cosmetic-blob-v1` blob and signs it with the single shared SHA-256 (ADR-0043); `blob` is the exact canonical byte string `blob-check` verifies |
| `blob-check` | `{blob, frame_scope?}` | `{blob_id, compiled_selectors, duplicates_removed, bytes, page_modifying, producer_refusals, sha256}` — the consumer path. Three strictness laws, all refusals: unknown field deny, digest verified before use (tamper ⇒ `kRejected` `sha256-mismatch`), schema id/version const (mismatch ⇒ `kRejected` `schema-mismatch`, never migrated). `frame_scope` is the FRAME's own scope supplied by the caller, never derived from the blob and never from an embedder; site mismatch ⇒ `kRejected` `scope-mismatch`. `page_modifying` counts `remove` rules separately so the Observatory can label them honestly |
| `key-set` | `{rules[]}` | `{compiled_selectors, duplicates_removed, bytes}` — compiles without a blob envelope, for the perf benchmark which must not have to build and sign a blob to measure compilation cost. Dedup is SEMANTIC: the key is canonical selector + action + style, so `div>.ad` and `div > .ad` collapse and `.a.b`/`.b.a` do too, while hiding `.ad` and removing `.ad` stay distinct |
| `degrade-apply` | `{condition}` | `{condition, outcome, page_effect, reported, reason}` — reads the SINGLE degrade truth table (14 conditions, `DegradeCondition` in `core/degrade.h`) so the debug page and the tests cannot restate it and drift; an unknown condition ⇒ `kRejected` `unknown-degrade-condition` rather than a plausible default |
| `page-states` | `{keyset_rules?}` | `{cosmetic_enabled, observer_installed, generic_set_applies, keyset_rules, refused_selectors, refused_pseudos, degrade_events, blob_cache_entries, blob_cache_occupancy}` — `observer_installed`/`generic_set_applies` come from the degrade module's two laws, so the page cannot report a state the renderer would not be in. `blob_cache_occupancy` is the string `"NOT-RUN (network-service side)"`: it is a network-service value this host does not own, and reporting a number would be inventing one |

Response law: one canonical JSON line on stdout — sorted keys, no whitespace,
non-ASCII as `\uXXXX`. Exit `0` for ok or a typed rejection
(`{"error":"kRejected","reason":…}`), `1` for a typed error
(`{"error":"kUnknownMethod"|"kMalformedInput","detail":…}`), `2` for a usage
error. A rejection is a normal result and not a failure: this surface refuses
far more often than it accepts, and treating every refusal as non-zero would
make the golden vectors indistinguishable from crashes.

## Determinism

No clock, no RNG, no environment, no I/O beyond reading the request. Every input
rides in the request, which is what lets the golden vectors be byte-stable
across machines. The host is stateless: no store, no cache, no persisted scope.

## Parity

`fakes/cosmetic.py` is the Python reference consumer of the same contract.
`tools/cosmetic_vectors_check.py` replays every golden vector against **both**
and requires identical stdout and identical exit codes. A divergence is a red
vector, not a quiet drift — one was already found and fixed this way (the fake
omitted zeroed summary fields on a refusal, so every refusal case diverged).

## Not implemented

The engine binding, the DOM path, and any Chromium integration. `gn` and `ninja`
are absent from the environment this was built in, so nothing here claims a
rendered result: this host validates and decides, and the page-level assertions
are HG-31.
