# themes-host-protocol v1 — the themes host stdio JSON protocol (P8-T3)

Registered post-freeze in `docs/contracts/registry-post-freeze.md` (P8 row;
contract consumption: `theme-tokens-v1.md`, no redefinition).

The themes host (`themes/host/themes_host.cc`, C++20) and the reference fake
(`fakes/themes.py`, Python stdlib) speak ONE protocol, byte-identical: every
result is ONE canonical JSON line (sorted keys, compact separators, non-ASCII
`\uXXXX`), exit `0` = typed result, `1` = typed error, `2` = usage.

## Invocation forms (both backends)

```
themes_host <method> ['<json-args>'] [options]
themes_host '<json-with-method>' [options]
themes_host [options]                 # request JSON on stdin
```

Options:

| option | meaning |
| --- | --- |
| `--store-dir DIR` | store dir for the applied-theme state file; ABSENT/empty ⇒ disposable (session state in memory, zero bytes written) |
| `--tokens PATH` | `ui/themes/tokens.json` source path (C++ host has a compiled-in default) |

## Methods

| method | args | result |
| --- | --- | --- |
| `flag-status` | `{}` | `{"xr_themes_v0":"on"}` — themes carry the settings flag in P8 (symmetry); never touches the store |
| `list` | `{}` | `{themes:[{name,kind:builtin,waivers}…,{name:"system",kind:resolver,default}],count,current,resolved,mode}` — built-ins name-sorted, from the token data only |
| `current` | `{}` | the applied-theme event: `{ok,applied,resolved,mode,values}` — the full 40-token map |
| `apply` | `{"name":str}` | validate → audit (with the built-in's waiver rows) → REFUSE; ok ⇒ applied event, persisted for durable stores |
| `system-mode` | `{"mode":light\|dark\|high-contrast}` | re-resolves the system resolver (light/dark/high-contrast modes); emits the applied event |
| `import` | `{"theme-doc":str}` | hostile-input custom theme (T4): 64 KiB cap, raw duplicate-key scan, strict parse, schema, contrast audit, per-token deltas; atomic refusal; session-scoped custom (never durable — recorded) |
| `validate-doc` | `{"theme-doc":str}` | audit only, state untouched: `{ok,deltas,delta_count}` |

## Error model

Typed codes, never bare failures: `kMalformedInput` (bad frame/args),
`kRejected` (unknown theme / unknown token / schema violation / contrast
audit failure / duplicate key / hostile value), `kUnknownMethod`,
`kStoreError` (tokens source unreadable / corrupt on-disk state), `kIoError`
(state write/rename failure — a store dir that does not exist is refused
here, never auto-created). Malformed JSON on stdin/argv ⇒ exit 1 +
`kMalformedInput`; a host method against a flag-off build state renders
nothing (typed refusal). Exit 2 = usage.

## Laws honored (same in both backends)

- Built-ins, token types, pairings, waiver rows and the system resolver all
  come from `ui/themes/tokens.json` — there is no hand-maintained list in
  either backend; unknown tokens are rejected (frozen `theme-tokens-v1.md`).
- Apply = validate → WCAG 2.1 contrast audit → **refuse**: a failing theme
  never applies; the refusal reason is a typed, human-readable string
  surfaced in the UI. Imported custom themes may NOT carry waivers (v1:
  hostile-by-default — an import must pass the audit in full).
- `critical-red` is RESERVED: any value outside the recorded alarming-family
  law is refused in data, generator, C++ core and fake alike.
- Waiver rows are exact data: a row on a passing pair is refused as
  unnecessary, a stale `best` is refused as mismatched (data-hygiene law).
- Durability: store-dir/xr-themes-state.json `{schema,schema_version:1,
  applied,mode}` written tmp→fsync→rename; a state that fails to load is
  preserved, never rewritten (deny-preserve); "custom" is never persisted.
- Canonical JSON formatting is identical across backends (the parity gate
  asserts byte-equality over the whole surface).
