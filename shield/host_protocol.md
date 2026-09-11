# shield-host-protocol v1 — the shield host stdio JSON protocol (P11-T2)

Registered post-freeze in `docs/contracts/registry-post-freeze.md`
(`shield-host-protocol` row). Written from the dispatch as built
(`shield/host/shield_host.cc` + `fakes/shield.py`, byte-parity locked by the
157 golden vectors in `docs/contracts/vectors/shield-v1.json`, replayed
against BOTH backends by `tools/shield_vectors_check.py` and pinned from the
compiled side by `shield/tests/test_golden_vectors.cc`).

The shield host (`shield/host/shield_host.cc`, C++20) and the reference fake
(`fakes/shield.py`, Python stdlib) speak ONE protocol, byte-identical: every
result is ONE canonical JSON line (sorted keys, compact separators, non-ASCII
`\uXXXX`), exit `0` = typed result (including typed `kRejected` refusals and
the frozen-envelope errors), `1` = typed error, `2` = usage. No exceptions
cross this boundary. Any method outside the table below is refused with
`{"detail":<method>,"error":"kUnknownMethod"}` (exit 1) — deny-on-unknown,
both backends.

## Two vocabularies in one protocol

* **FROZEN mojom surface** (`Status`, `RecentEvents` — mojom/shield.mojom,
  `kContractVersion=1`): the P5 fake envelope `{"ok":…}` / `{"error":"<ErrorCode>"}`
  with the frozen enum spellings (`kBlocked`, `kScript`, …). The frozen
  fixture case (`fakes/fixtures/shield-v1.json`) and the deterministic fixed
  sample event are byte-identical to the P5 freeze. `ring` is an accepted
  HELPER key (fakes/README.md test-affordance convention) that replaces the
  fixed sample with a view over explicit state; absent `ring`, the legacy
  behavior is preserved exactly.
* **LIVING host surface** (everything else): bare canonical result objects,
  `{"error","detail"}` typed errors, `{"error":"kRejected","reason"}` typed
  refusals, lowercase living enum names. This document is that surface's
  living contract; changes land as dated rows in the post-freeze registry,
  never as rewrites of frozen shapes.

## Invocation forms

```
shield_host <method> ['<json-args>'] [options]
shield_host '<json-with-method>' [options]
shield_host [options]                 # request JSON on stdin

fakes/shield.py — same three forms (Python mirror)
```

Options:

| option | meaning |
| --- | --- |
| `--flag xr_shield_v1=on\|off` | feature flag (default `on`); BOTH states are tested (vectors + `test_shield_host`). `off` surfaces through `flag-status` and `Status.enabled` only — the decision methods are flag-independent by design: the kill-switch law lives in the posture inputs (`kill_switch_on`), where it is visible and testable, not in a CLI switch that would silently change verdicts |

There is **no `--store-dir` in v1**: the host is STATELESS — every state
(bundle, apply state, scopes, ring, posture inputs, `now_mono`) rides in the
request, which is what makes the vectors deterministic. T5's ledger
persistence adds storage by contract change (dated registry row), not by
quietly growing this table.

## Methods

| method | args | result |
| --- | --- | --- |
| `flag-status` | `{}` | `{"xr_shield_v1":"on\|off"}` |
| `Status` | `{identity, origin, ring?}` | FROZEN envelope. `identity.value` missing/empty ⇒ `{"error":"kUnknownIdentity"}`; `origin.scheme` ∉ http/https ⇒ `{"error":"kUnknownOrigin"}`; else `{"ok":{"blocked_count":N,"enabled":E}}` — `E` = flag on, `N` = kBlocked events for THIS identity in the optional `ring` (absent ⇒ 0, the frozen bytes). Unknown keys ⇒ frozen `{"error":"kMalformedInput"}` (exit 0 — envelope law) |
| `RecentEvents` | `{identity, max_events?, ring?}` | FROZEN envelope `{"ok":[BlockEvent…]}`. With `ring`: the identity-filtered view, NEWEST FIRST, capped by `max_events` AND the 64 KiB canonical-byte chunk budget (the first matching event is always emitted whole). Without `ring`: the frozen fixed sample ×`max(0,min(max_events,1))`, identity echoed as received. Bad shapes ⇒ frozen `{"error":"kMalformedInput"}` (exit 0) |
| `bundle-load` | `{bundle}` | strict parse of the living `xr-list-bundle-v1` doc ⇒ summary `{"name","bundle_version","digest","lists":[{name,rules}],"refusals":[{directive,reason,count}]}`. `digest` = sha256 over `"[" + ",".join(canonical per-list bytes) + "]"` — the manifest binding target. Shape violations ⇒ `kMalformedInput` (exit 1); a filter outside the v1 grammar ⇒ typed `kRejected` with the grammar token (exit 0) |
| `bundle-check` | `{bundle, manifest}` | parse + bind against a frozen `list-bundle-manifest-v1` document: per entry, `name` equal, optional `attribution` equal to the bundle list's attribution (the frozen schema leaves entry shape free-form — T3's attribution pipeline embeds it, and a manifest may not claim attribution other than the one the bytes it pins carry), `rules` count equal, `sha256` == digest of that list's canonical bytes. Bound ⇒ `{"bound":true,"bundle_version","digest","list_count"}`; any mismatch ⇒ `kRejected` with the binding token; malformed manifest ⇒ `kMalformedInput` |
| `match` | `{context, bundle?, scopes?, engine_alive?, engine_poisoned?, route_bound?, kill_switch_on?, now_mono?}` | the decision pipeline: `{"fail_closed":bool,"posture":{mode,chip,reason},"verdict":{action,why_code,rule_id,list_id,bundle_version,engine_decision}}`. Order is LAW: posture (fail-closed ⇒ blocked verdict + `fail_closed:true`; fail-open ⇒ allowed + the posture why) → bundle presence → engine → exception scopes over blocking hits. `context` is the strict request context (required `identity/origin/url/request_class`; optional `first_party`/`tab_type`/`workspace` with documented defaults) |
| `posture` | `{engine_alive?, engine_poisoned?, route_bound?, kill_switch_on?}` | `{"mode","chip","reason"}` — the pure truth table: route loss ⇒ `fail-closed`/`red`/`route-loss` ABSOLUTELY (nothing un-fail-closes it); else engine death ⇒ `fail-open`/`amber`/`engine-dead`; else poison ⇒ `engine-poisoned`; else kill switch ⇒ `kill-switch`; else `normal`/`green` |
| `apply` | `{bundle, state, now_mono}` | candidate activation ⇒ `{"state":…new}`. Monotonic per `bundle_id`: downgrade AND equal re-offer ⇒ `kRejected`; the displaced active becomes `lkg`; `pins` keep the last TWO slots and MUST contain the active slot (violation ⇒ `kRejected` `pins-missing-active` — the hot-pin-out refusal, never a repair); `now_mono` < recorded `last_apply_mono` ⇒ `kRejected` `stale-apply-clock`; `now_mono` REQUIRED (determinism — no wall clock anywhere) |

## Error model

Typed codes, never bare failures:

- `kMalformedInput` (exit 1, living surface) — bad frame, args not an
  object, unknown arg keys at ANY level, wrong types, empty required
  strings, unparsable URLs. `detail` carries the closed-vocabulary token
  (`unknown-field:X`, `unparsable-url`, `bad-slot-fields:active`, …).
- `kUnknownMethod` (exit 1) — the unknown-method refusal.
- `kRejected` (exit 0) — a typed REFUSAL, not a protocol error. Reason
  vocabulary (closed set, both backends byte-identical):
  - apply: `bundle-version-downgrade`, `bundle-version-equal-reoffer`,
    `stale-apply-clock`, `pins-missing-active`, `too-many-pins`,
    `lkg-duplicates-active`;
  - filter grammar v1: `empty-filter`, `unsupported-directive:comment`,
    `unsupported-directive:regex`, `unsupported-directive:options-in-filter`,
    `unsupported-directive:cosmetic-hash`, `unsupported-directive:at-syntax`,
    `unsupported-directive:interior-pipe`,
    `unsupported-directive:empty-segment`;
  - manifest binding: `manifest-list-count`, `manifest-list-name:<name>`,
    `manifest-attribution:<name>`, `manifest-rule-count:<name>`,
    `manifest-sha256:<name>`.
- Frozen-surface errors (exit 0, envelope): `kUnknownIdentity`,
  `kUnknownOrigin`, `kMalformedInput` — per mojom/shield.mojom ErrorCode.

Exit 2 = usage (no method, unknown option — including `--store-dir` in v1,
bad `--flag k=v`).

## Decision vocabularies (closed sets)

- `verdict.action`: `block` · `allow` · `redirect` · `replace` (living; the
  mojom `kUpgraded` action has NO v1 list-rule producer — documented
  deviation, reserved for the HTTPS-upgrade seam).
- `verdict.why_code`: `rule-blocked` · `rule-allowed` · `rule-redirected` ·
  `rule-replaced` · `no-match` · `no-bundle` · `exception-scope` ·
  `engine-dead-fail-open` · `engine-poisoned-fail-open` · `kill-switch` ·
  `route-loss-fail-closed`.
- `posture.mode`/`chip`/`reason`: `normal|fail-open|fail-closed` ·
  `green|amber|red` · `normal|engine-dead|engine-poisoned|kill-switch|route-loss`.
- `request_class` (FROZEN, xr_types.mojom): `kNavigation` `kSubresource`
  `kScript` `kPermission` `kStorage` `kNetwork`.
- `tab_type` (living): `normal` (default) · `incognito` · `workspace`;
  `workspace` string `""` = the default workspace.
- BlockEvent (FROZEN shape): `ts_millis`, `identity{value}`, `tab_id`,
  `origin{scheme,registrable_domain}`, `target`, `rule`, `list_provenance`,
  `action` ∈ `kBlocked|kAllowed|kRedirected|kUpgraded`, `request_class`.

## Filter grammar v1 (network rules)

`||` domain anchor (dot-boundary host suffix) · `|` left/right anchor ·
`*` wildcard (ordered segment containment, earliest-start greedy — complete
for containment, no backtracking needed) · `^` separator class (matches one
of `/:` or end-of-string; the match surface is `scheme://host/path`,
lowercased, port stripped, query+fragment stripped) · literals. Everything
else (`$options` in filter text, `/regex/`, `!` comments, `#` cosmetic
syntax, `@` syntax, interior pipes) is a REFUSAL token — the compiler's
refusal table owns the text syntax; the engine never guesses. `domains` /
`exclude_domains` are structured rule options with EXACT set membership
against the request's registrable domain or host. Cosmetic rules never match
in the network engine. Among engine hits, an `allow` rule returns
immediately (ABP exception semantics); otherwise the first non-allow hit in
list-then-rule order wins.

## Laws honored (same in both backends)

- **TEST-ONLY engine binding:** the host binds the deterministic
  `TableEngine` (`shield/core/fake_engine.h` — the v1 reference matcher).
  The vendored adblock-rust binding lands behind the SAME `BlockingEngine`
  interface via the `shield/engine/` FFI shim (hosted lane); T8's parity job
  measures the two. This host never claims the Rust engine runs.
- **Fail-open/fail-closed asymmetry:** route loss is fail-CLOSED and
  absolute; engine death/poison and the kill switch are fail-OPEN with an
  AMBER chip — degraded is never invisible, and browsing never breaks
  because the blocker died. Property-tested by exhaustive enumeration
  (`test_posture`: all 16 input combinations).
- **Resolver coupling:** an exception scope bound to identity A never
  covers identity B; site and workspace dimensions are exact, with no
  inheritance (the resolver's domain hierarchy stays the resolver's
  business).
- **Redaction at creation:** `target` is `scheme://host/path` BEFORE an
  event is written — query strings and fragments never reach the ledger.
  The match surface uses the same redaction.
- **Determinism:** no wall clock, no RNG, no environment, no I/O; every
  time input is a caller-supplied monotonic integer (`now_mono`,
  `ts_millis`, `expiry_mono`, `last_apply_mono`).
- Strict deny-on-unknown parsing at every nesting level; monotonic apply
  with LKG + last-two pins; both flag states tested.
