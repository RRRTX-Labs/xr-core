# settings-host-protocol v1 — the settings host stdio JSON protocol (P8-T1)

Registered post-freeze in `docs/contracts/registry-post-freeze.md` (P8 row;
contract consumption: `settings-schema-v1.md`, no redefinition).

The settings host (`settings/host/settings_host.cc`, C++20) and the reference
fake (`fakes/settings.py`, Python stdlib) speak ONE protocol, byte-identical:
every result is ONE canonical JSON line (sorted keys, compact separators,
non-ASCII `\uXXXX`), exit `0` = typed result, `1` = typed error, `2` = usage.

## Invocation forms (both backends)

```
settings_host <method> ['<json-args>'] [options]
settings_host '<json-with-method>' [options]
settings_host [options]                 # request JSON on stdin
```

Options:

| option | meaning |
| --- | --- |
| `--store-dir DIR` | store dir for the counters ledger; ABSENT/empty ⇒ disposable (counters in memory, zero bytes written) |
| `--schema PATH` | `settings_schema_v1.json` path (C++ host has a compiled-in default) |
| `--state PATH` | `xr-settings-state` v1 doc — the PINNED P6 policy snapshot the views display (absent ⇒ all defaults, no active identity: deny-safe) |
| `--flag xr_settings_v0=on\|off` | the P8 feature flag (default on) |

## Methods

| method | args | result |
| --- | --- | --- |
| `flag-status` | `{}` | `{"xr_settings_v0":"on"\|"off"}` — never touches the store |
| `sections` | `{}` | `{sections:[{id,title_id,anchor,available,availability,unavailable_reason?,settings:[key…]}],count}` — registry GENERATED from the schema data |
| `search` | `{"query":str}` | `{results:[{key,kind,score}…],count}` — ranking is core C++; a non-empty result set with setting hits records a day-granular query counter |
| `get` | `{"key":str}` | typed view of one setting: `{key,value,source(default\|resolver\|enterprise),preempted,writable,section,attention_tier,scope}` |
| `set` | `{"key":str,"value":<typed>}` | `{key,status:"set",persisted}` — v0 rejects every policy-owned row with `kRejected` + the surfaced-by-policy reason (no user write path yet); unknown keys rejected |
| `router-resolve` | `{"anchor":str}` | `{ok,kind(home\|section\|setting\|unknown),section?,setting?,canonical?,error?,suggestions?}` — opens the resolved section's counter |
| `counters-dump` | `{}` | the full ledger `{schema,schema_version,days,in_memory}` (diagnostics; also `xrctl settings counters`) |
| `schema-dump` | `{}` | typed schema summary (version, anchor root, keys, count) |

## Error model

Typed codes, never bare failures: `kMalformedInput` (bad frame/args),
`kRejected` (unknown key / flag-off / policy-owned row), `kUnknownMethod`,
`kStoreError` (unreadable schema/state/corrupt ledger), `kIoError` (counter
persist failure). Malformed JSON on stdin/argv ⇒ exit 1 + `kMalformedInput`;
bad `--flag` value ⇒ exit 2 (usage).

## Laws honored (same in both backends)

- Sections/anchors/rows/aliases all come from `settings_schema_v1.json` —
  there is no hand-maintained list anywhere in either backend.
- Availability reads the PINNED `--state` snapshot (deny unknown predicates);
  settings display the policy truth they are GIVEN (never a live Resolve()).
- Flag off ⇒ nothing renders/writes: `sections`/`search` are empty or typed
  `kRejected`, no counter byte is ever written.
- Counters are local-only: day granularity (UTC date keys), no identifiers,
  no network path, disposable stores write zero bytes, and a ledger that
  fails to load is preserved (Save refuses to overwrite — deny-preserve).
- Canonical JSON formatting is identical across backends (the parity gate
  asserts byte-equality over the whole surface).
