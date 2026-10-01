# identity-host-protocol v1 — the identity host stdio JSON protocol (P14-T1)

The identity host (`identity/host/identity_host.cc`, C++20, std-only) speaks
TWO surfaces over stdio. Every result is ONE canonical JSON line (sorted
keys, compact separators, non-ASCII `\uXXXX`), exit `0` = typed result,
`1` = typed error, `2` = usage. Unknown methods and unknown subcommands are
refused with `kUnknownMethod` (never a silent default).

## Surfaces

**The FROZEN mojom surface** (`mojom/identity.mojom` v1, frozen at P5) is the
`{method,args}` JSON form — dispatched on the method string exactly like
`fakes/identity.py`, keeping the fake envelope `{"ok":…}` / `{"error":…}` and
the frozen enum spellings. `fakes/fixtures/identity-v1.json` and
`tools/parity/corpus-identity.json` replay BYTE-IDENTICALLY against this host
and the fake (the P9 parity law; `tools/tests/test_p14_identity.py` asserts
it). Frozen `Create` is the fake's deterministic shape: it mints from the
frozen fixture table (the fake's `_MINTS`) and accepts `_seed` rows — it is
the CONTRACT surface, not production provisioning.

**The LIVING subcommand surface** (the `policy_host` precedent — argv[1] is
the subcommand, argv[2] (or stdin via `-`) the args object) carries the
P14 core: provisioning with real entropy, templates, ceremony, the binding
model, the scheduler, attribution. It is NOT the frozen contract and has no
fake counterpart; its coverage is the C++ suite (`identity/tests/`) plus
`tools/tests/test_p14_identity.py` (the policy-precedent split — accounted
for as data in `tools/parity/manifest.json`, never silently omitted).

## Invocation forms

```
identity_host <frozen-method> ['<json-args>']     # frozen {method,args} surface
identity_host '<json-with-method>'                # frozen surface
identity_host                                     # request JSON on stdin
identity_host <subcommand> ['<json-args>' | -]    # living surface
```

## Methods

The frozen `mojom/identity.mojom` v1 set (byte-parity with
`fakes/identity.py`; args exactly as the fake reads them):

| method | args | result |
| --- | --- | --- |
| `Create` | `{grade: kStandard\|kFortress, ephemeral?: bool, _seed?: [{grade, ephemeral}]}` | `{"ok":{id:{value},state:"kActive",grade,in_memory}}` — mint from the frozen fixture table; table exhausted ⇒ `{"error":"kFailClosed"}` |
| `Activate` | `{id:{value}}` (+`_seed`) | `{"ok":{…info…}}` · unknown ⇒ `{"error":"kUnknownIdentity"}` |
| `Hibernate` | `{id:{value}}` (+`_seed`) | `{"ok":{…info, state:"kHibernated"}}` · unknown ⇒ `kUnknownIdentity` |
| `PromoteToFortressProfile` | `{id:{value}}` (+`_seed`) | `{"ok":{…info, grade:"kFortress"}}` · unknown ⇒ `kUnknownIdentity` |
| `Destroy` | `{id:{value}}` (+`_seed`) | `{"ok":{zero_residual_verified:true}}` · unknown ⇒ `kUnknownIdentity` |
| `MoveTab` | `{from, to, tab_id, after_nav}` | `after_nav` ⇒ `{"error":"kNotPermitted"}` (the P4 timing finding — a non-destructive attach to a navigated tab does not exist); `to` known ⇒ `{"ok":{…info…}}`; unknown `to` ⇒ `kUnknownIdentity` |

## Subcommands (the living surface)

| subcommand | args | result |
| --- | --- | --- |
| `flag-status` | `{}` | `{"xr_identity_v1":"on"}` |
| `templates` | `{}` | `{count, templates:[{id,label,color,glyph,disposable,route,route_bound}]}` — all seven, menu order |
| `ceremony` | `{template_id}` | `{lines:[… every applied default …]}` · unknown ⇒ `kRejected` (the §3.9 machine-checked inventory) |
| `provision` | `{entropy, display_name?, template_id?, in_memory?, replay?, prefs?}` | the record `{domain,display_name,template_id,grade,in_memory,state,color,glyph}` — entropy REQUIRED (`kMalformedInput` without it: the host cannot accidentally mint from the fixture table); template rows applied as the overlay |
| `resolve-pref` | `{domain, key}` | `{key, value}` — `value:null` = NO overrides (the fail-safe: unreadable ⇒ closed, never all-defaults-allowed) |
| `activate` / `hibernate` / `wake` | `{domain, tick}` | `{ok, domain, state}` — the scheduler's soft cap of 5; LRU evictions recorded; `wake` REFUSES purged identities (never resurrects); typed `kRejected` refusals |
| `destroy` | `{domain}` | `{ok, zero_residual_verified}` — purge-AND-verify; residual bytes ⇒ `kInternal` typed error exit 1 (§1.4: a destroy that leaves bytes is a FAILURE) |
| `plant-residual` | `{domain, kind?, bytes?}` | `{ok, test_hook}` — TEST HOOK, named as such (the §1.4 negative's planter) |
| `bind-open` | `{window, tab_id, opener?}` | `{ok, domain}` — born at the window default |
| `bind-move` | `{from, to, tab_id, after_nav, group_peers?}` | `{ok}` · `kRejected` with the reason (after-nav ⇒ confirm+reload; group split names the peer) |
| `suggest` | `{site, domain}` | `{ok, surfaced:true}` — recorded + surfaced, NEVER applied |
| `autoswitch-probe` | `{tab_id, to}` | MUST `kRejected` (the planted auto-switch; if it ever succeeds the host says `kInternal` "the never-auto-switch law is broken") |
| `restore` | `{tab_id, domain}` | `{ok}` — audited with the restore cause |
| `audit` | `{}` | `{changes:[{tab_id,from,to,cause}], evictions:[…], suggestions:[…]}` — no entry may carry cause `suggestion` |
| `attribute` | `{samples:[{pid,rss_kb,serves:[…]}], total_kb}` | `{rows:[{domain,kb,processes}], sum_kb, total_kb}` — even split for shared, `(unattributed)` closes the sum; samples exceeding the total ⇒ `kRejected` (no table, never a wrong one) |
| `scenario` | `{ops:[{op: provision\|activate\|hibernate\|wake\|destroy\|plant-residual\|suggest\|autoswitch, …}]}` | `{steps:[…], final:{…audit…}}` — deterministic multi-step runs (cap evictions, wake refusals) |

## Error model

Typed codes, never bare failures: `kMalformedInput` (bad frame/args),
`kRejected` (typed refusal with `reason`, exit 0), `kUnknownMethod` /
unknown subcommand (exit 1), `kInternal` (the two red-is-red cases: residual
bytes after a purge; an autoswitch-probe that succeeds), `kFailClosed`
(frozen fixture table exhausted). Exit `2` = usage.

## Laws honored

- **Opaque domains only**: the mint is sha256(caller entropy) formatted as
  UUID v4 — never a name, site, or URL; the store REFUSES name-keyed or
  name-embedding domains (`LooksOpaque`); the host's `provision` DEMANDS
  entropy (OS entropy per the production host's duty; no clock, no RNG, no
  counter anywhere in the core).
- **Purge-and-verify (§1.4)**: destroy asserts zero residual bytes by
  WALKING THE STORE, never by trusting the purge's return code; the
  `plant-residual` test hook exists to prove the negative.
- **Never auto-switch**: a suggestion can never change a binding — the
  refusal is structural (`autoswitch-probe` pins it; the audit trail cannot
  contain a suggestion-caused change).
- **Fail-safe prefs**: an unreadable per-identity namespace yields
  "no overrides" (`value:null`), never the global permissive set.
- **No new network, crypto, or deps**: sha256 + JSON are the shared
  `common/core` single copies; the host touches no sockets, no files.
