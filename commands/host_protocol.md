# command-host-protocol v1

The stdio JSON protocol spoken by the C++ `commands_host`
(`commands/host/commands_host.cc`) **and** the Python reference fake
(`fakes/commands.py`). The two are **byte-identical** over the whole surface
(proven by `xr-browser/tools/tests/test_p7_commands_tools.py` — P6 parity
pattern). `xrctl commands --backend cpp|fake` drives either interchangeably, so
P8/P13 tracks can develop without a C++ toolchain.

## Invocation

```
commands_host <method> ['<json-args>'] [options]     # method positional
commands_host '{"method":M,"args":{...}}' [options]  # JSON request positional
commands_host [options]                              # request JSON on stdin
```

Options: `--store-dir DIR` (empty/absent ⇒ ephemeral, seeded roster, in-memory
state, no persistence) · `--flag xr_command_registry_v1=on|off` (default `on`) ·
`--roster PATH` (seed registry when `<store-dir>/registry.json` is absent;
default `commands/core/roster_v1.json`).

**Every result is a single canonical JSON line** — sorted keys, compact
separators, non-ASCII as `\uXXXX` (byte-stable across both backends). Exit `0`
= typed result, `1` = typed error, `2` = usage. No exceptions cross the
boundary.

Canonical wrappers: `{"ok": <value>}` or `{"error": "<Code>"}` (optionally
`+ "detail": "<msg>"`). Error codes: `kMalformedInput`, `kUnknownMethod`,
`kStoreError`, `kRejected`, `kIoError`.

## Methods

| method | args | `ok` shape |
| --- | --- | --- |
| `flag-status` | `{}` | `{"xr_command_registry_v1":"on"\|"off"}` |
| `list` | `{group?}` | `{"commands":[{descriptor,registry}...],"count":N}` (flag off ⇒ `commands:[]`) |
| `query` | `{query}` | `{"results":[{"available","id","kind","reason","score","title"}...],"count":N}` (ranked; `available`/`reason` from the pinned snapshot; flag off ⇒ `results:[]`) |
| `invoke` | `{id,source,confirmed?,site?="*"}` | `{"effect":{...},"handler","id","source","status":"rejected\|confirmation-required\|authorized","reason","danger_class","ledger":[<canonical row>...]}` (flag off ⇒ `status:"rejected"`, reason "feature flag … is off (stock chrome)") |
| `bindings-set` | `{command_id,accelerator}` | `{"bound":bool,"command_id","conflict","persisted":bool,[conflicting_command],[error]}` (unknown id ⇒ `bound:false`,`conflict:"none"`,`error`) |
| `bindings-list` | `{}` | `{"bindings":[{"accelerator","command_id"}...],"count":N}` (sorted by accelerator,command_id) |
| `bindings-clear` | `{command_id?}` | `{"cleared":id\|null,"persisted":bool}` or `{"cleared_all":true,"persisted":bool}` |
| `menu-model` | `{}` | `{"flag","menus":[{"kind":"tools","items":[{available,danger_class,group,id,reason,tier,title}...]}],"tier1":{"count":N,"items":[...]}}` (tier1 separated; flag off ⇒ empty) |
| `register` | `{descriptor,registry}` | `{"id","order","status":"registered","tier1_count":N}` or `{"error":"kMalformedInput\|kRejected","detail":...}` |

### `invoke` handler effects (dials toggle P6 policy state end-to-end)

`action.dial.standard|shield|fortress` ⇒ writes the P6 `trust-bindings-v1` doc
(validated by `policy_core` in the dial test) and returns
`{"prev","site","trust"}`. `action.dial.reset` ⇒ removes the binding and returns
`{"prev","removed":true,"site"}`. All other authorized handlers return
`effect:{}`. A policy write failure is a **typed** `status:"rejected"` (never a
silent no-op).

### `invoke` security (the dispatch edge — `commands/core/dispatch.cc`)

Ordered, defense-in-depth (page-originated is rejected **before** the id is
looked up):
1. **source allowlist** `{ui-chrome,palette,menu,shortcut,test}` — `page` and
   any unknown source ⇒ `rejected` + ledger row (cross-process origin check; the
   palette never executes page-originated commands).
2. **id whitelist** — unknown id ⇒ `rejected` + ledger row (never reaches a handler).
3. **danger gate** — `destructive` & `!confirmed` ⇒ `confirmation-required`
   (L7 friction is a feature; modeled as predicate-output data).
4. else ⇒ `authorized`.

Ledger rows are canonical `{"event","id","reason","seq","source"}`; `seq` is a
monotonic counter (no clock ⇒ deterministic bytes).

## Availability predicate contract (P7 "Contracts out")

`query` / `menu-model` evaluate each command's `predicate_id` against the
**pinned** P6 snapshot (`PolicyState::Snapshot()` — a versioned copy, not a live
`Resolve()`). Registered predicates: `always`, `policy.trust-dial-writable`,
`tor.engine-ready` (disabled placeholder until P31 — **registered, disabled
WITH a reason, never absent**), `identity.active`. Unknown predicate ⇒
**deny-default** (L3, never a guess). TOCTOU law: an in-flight evaluation keeps
the pinned version's verdict even if the live cache is invalidated mid-flight
(`commands/tests/test_availability.cc`).

## Flag semantics

`xr_command_registry_v1` off ⇒ **stock chrome**: `list`/`query`/`menu-model`
serve empty, `invoke` is `rejected` ("feature flag … is off (stock chrome)"),
`register` is `rejected`. On ⇒ the seeded 20-command roster. The flag is kept
until P13 (expiry note in `build/gn/argsets/flags.yaml` + the
`registry-post-freeze.md` host-protocol entry).
