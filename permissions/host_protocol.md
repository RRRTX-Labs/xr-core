# permissions-host-protocol v1 — the permissions host stdio JSON protocol (P15-CLOSE C2)

The permissions host (`permissions/host/permissions_host.cc`, C++20, std-only
plus POSIX `fsync`) is the **clock-owning** layer over the clock-free core
(`permissions/core/`, ADR-0051 DRAFT). Every result is ONE canonical JSON line
(sorted keys, compact separators, non-ASCII `\uXXXX`). Exit `0` = typed result,
`1` = typed error, `2` = usage. Unknown methods are refused with
`kUnknownMethod`, never a silent default.

The Python fake `fakes/permissions.py` replays `fakes/fixtures/permission-v1.json`
and `xr-browser/tools/parity/corpus-permissions.json` BYTE-IDENTICALLY against
this host at `--as-of` (the P9 parity law; `tools/tests/test_p15c_permissions_host.py`).

## The clock (F3)

- The host reads the wall clock **once per mutating call** (`ObservedNowMs`), and
  only when `--as-of MS` is absent. It is the only clock read under
  `permissions/`; `tools/permission_write_path_check.py` holds the core
  clock-free (a planted clock read in `permissions/core/` reddens it).
- `now_ms = EffectiveNow(last_seen_ms, observed)` (the core's monotone fold). In
  store-dir mode `last_seen_ms` is persisted in `permissions-clock.json`, so a
  wall clock rolled back after a landed sweep cannot resurrect an expired grant.
- `view` needs no clock and reads none.
- The fake reads NO clock (the `fakes/_base.py` law): a mutating call without
  `--as-of` is a usage error there (exit `2`). That is the one documented
  asymmetry; every parity case passes `--as-of`.

## Invocation forms

```
permissions_host [--store-dir DIR] [--as-of MS] '<json {method,args}>'
permissions_host [--store-dir DIR] [--as-of MS] <method> ['<json-args>']
permissions_host [--store-dir DIR] [--as-of MS]          # request JSON on stdin
```

- `--store-dir DIR`: the store is `DIR/permissions-store.json` (the strict
  `permission-store` v1 bytes, no trailing newline) and the clock high-water mark
  is `DIR/permissions-clock.json` (`{"last_seen_ms":N}`, canonical). Both are
  written tmp → `fsync` → `rename`; a store that did not change is not rewritten.
- Absent `--store-dir` ⇒ **disposable**: the input store rides in `args.store`
  (a string; absent ⇒ the empty store) and nothing is written anywhere (zero
  bytes; `tools/kill_matrix.py` proves it). `args.store` with `--store-dir` is a
  `kMalformedInput`.
- Requests over 1 MiB are `kMalformedInput`.

## Methods

Every mutating method returns `{"ok":{"now_ms":N,"rows":[…],"store":"<text>"}}`:
`rows` are `permission-audit-event` v1 objects (exactly one per change, none for a
no-op), and `store` is the canonical store text after the call.

| method | args | result |
| --- | --- | --- |
| `view` | `{store?}` | `{"ok":{"view":{…}}}` — the resolver's `permission_overlay` projection (`ProjectViewJson`); a corrupt store projects `{"contract_version":1,"corrupt":true}` (fully denying) |
| `set-default` | `{identity, capability, state, store?}` | the identity default for one of the frozen four (`kDeny`/`kAsk`/`kAllow`); same value ⇒ no row |
| `grant` | `{identity, domain, capability, scope, session_id?, store?}` | a temporary grant (`once` / `session` / `7d`); the domain must be a bare registrable domain; ids are `g<seq>`, skipping taken ids |
| `set-denied` | `{identity, denied, store?}` | the Fortress deny-list flag; four rows on change, none otherwise |
| `consume-once` | `{grant_id, store?}` | spends a live `once` grant (one `expiry`/`used` row) |
| `sweep` | `{session_id, store?}` | **the clock-owning expiry sweep**: removes `7d` grants at `expires_at <= now_ms` (fail-closed at `==`), session grants not bound to `session_id`, and spent `once` grants; idempotent |
| `revoke-site` | `{identity, domain, store?}` | tombstones every live grant of the identity on that domain |
| `revoke-all` | `{identity, store?}` | clears the identity's defaults and tombstones every live grant |

## Error model

| code | when | exit |
| --- | --- | --- |
| `kMalformedInput` | bad frame, unknown arg, missing arg, wrong arg type, `store` misuse, oversize | 1 |
| `kUnknownMethod` | a method not in the table above | 1 |
| `kRefused` | the core refused the op; `detail` is its token (`corrupt-store`, `bad-identity`, `bad-domain`, `bad-capability`, `bad-state`, `bad-scope`, `bad-session`, `bad-time`, `unknown-grant`) | 1 |
| `kFailClosed` | `permissions-clock.json` is not the canonical `{"last_seen_ms":N}`: refused and left untouched | 1 |
| `kIoError` | a read or an atomic write failed | 1 |

Errors are `{"error":"<code>","detail":"<text>"}`. Usage errors (unknown option,
bad `--as-of`, too many arguments) print to stderr and exit `2`.

## Laws honored

- **The core stays clock-free**: time enters `permissions/core/ops` only as a
  parameter.
- **A corrupt store is never repaired**: every mutation refuses it, and the file is
  left as it was (the kill matrix corrupt-load cell checks this through the process
  boundary).
- **Kill -9 safety**: `permissions/tests/test_kill9.cc` SIGKILLs the built host
  mid-sweep at seeded delays and asserts that the store holds the whole pre- or
  post-sweep generation, that the next start leaves zero grants past expiry, and
  that a rolled-back clock cannot bring any back.
- **No new network, crypto, or deps**: JSON is the shared `common/core` copy.
