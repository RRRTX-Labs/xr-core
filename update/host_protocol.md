# update-host-protocol v1 — the update host stdio JSON protocol (P10-T1; doc published P11-T0-a)

Registered post-freeze in `docs/contracts/registry-post-freeze.md` (P10 row,
`update-host-protocol`). **Provenance note (P11-T0-a):** the P10 registry row
cited this path before the file existed — the gate `tools/parity_completeness.py`
counted 4 pairs and could not see a fifth host that shipped no protocol doc.
This document is written from the dispatch as built
(`update/host/update_host.cc` + `fakes/update.py`, byte-parity locked by the
73 golden vectors in `docs/contracts/vectors/update-v1.json`, run against BOTH
backends by `tools/update_vectors_check.py`); the registry carries a dated
correction line, appended — closed P10 rows are never rewritten.

The update host (`update/host/update_host.cc`, C++20) and the reference fake
(`fakes/update.py`, Python stdlib) speak ONE protocol, byte-identical: every
result is ONE canonical JSON line (sorted keys, compact separators, non-ASCII
`\uXXXX`), exit `0` = typed result (including typed `kRejected` verdicts),
`1` = typed error, `2` = usage. No exceptions cross this boundary.

## Invocation forms

```
update_host <method> ['<json-args>'] [options]
update_host '<json-with-method>' [options]
update_host [options]                 # request JSON on stdin

fakes/update.py '<json-with-method>' [--store-dir DIR]   # stdin when no arg
```

Options:

| option | meaning |
| --- | --- |
| `--store-dir DIR` | `seen` persistence dir for replay refusal; ABSENT/empty ⇒ disposable (session state in memory, zero bytes written); a persist write failure is a typed `seen-persist-failed` deny reason, never a silent drop |
| `--flag xr_updater_v0=on\|off` | host-only build-channel flag (default `on`); the fake's flag law is fixed `on` (the flag is a host-CLI surface; both states are tested on the host) |

## Methods

| method | args | result |
| --- | --- | --- |
| `flag-status` | `{}` | `{"xr_updater_v0":"on\|off"}` — never touches the store; flag-independent |
| `about-states` | `{}` | `{"states":["idle","checking","available","downloading","ready","failed","refused"]}` — the fixed 7-state About state-machine alphabet; flag-independent |
| `verify` | `{envelope, channel, current_version, epoch, keys, seen, transport}` | strict deny-on-unknown envelope parse (frozen `update-manifest-31` subset + the living `xr-update-envelope-v1` wrapper) → verdict line `{verifier, verdict: "accept"\|"deny", reason, manual_path, manifest_id?, seen_size}`; unknown arg keys ⇒ `kMalformedInput`; TLS-independence law: the `transport` echo is accepted and IGNORED (no transport datum reaches the decision); flag-off ⇒ typed `kRejected` |
| `epoch-apply` | `{notice, keys, epoch?}` | `notice` is an envelope-style `{"body":{…},"signature":{…}}` wrapper (unknown WRAPPER keys refused); the signature is checked over the canonical body bytes against the pinned `keys[]` (verification failure ⇒ typed `kRejected` `signature-invalid`); ok ⇒ `{applied, epoch}` — revocation is sticky (seq monotonic); flag-off ⇒ typed `kRejected` |
| `cohort` | `{install_id, channel, buckets}` | `{"bucket":N}` — deterministic staged-rollout bucketing, `0 ≤ N < buckets ≤ 100000`; flag-independent |
| `backoff` | `{state:{last_check_mono, next_allowed_mono, fail_streak}, now_mono, outcome?}` | `{may_check_now, seconds_until_next, state:{fail_streak, last_check_mono, next_allowed_mono}}` — caller-supplied monotonic integers only (NO wall clock anywhere); `outcome` ∈ `success\|failure` records an attempt first; flag-off ⇒ typed `kRejected` |
| `about-state` | `{state, event}` | one step of the About state machine: events `check` `offer` `noupdate` `download-start` `download-done` `error` `install` `reset` `epoch-revoked`; unknown transition ⇒ typed `kRejected`; `failed` carries `reason` + `manual_download` + `check_again`; `refused` carries `manual_download` (no silent failures); flag-independent |

## Error model

Typed codes, never bare failures:

- `kMalformedInput` (exit 1) — bad stdin/argv frame, args not an object,
  unknown arg keys, oversize documents (> 64 KiB), malformed envelope/notice/
  keys/epoch/state shapes.
- `kUnknownMethod` (exit 1) — the unknown-method refusal: any method outside
  the table above is refused with `{"detail":<method>,"error":"kUnknownMethod"}`;
  deny-on-unknown, both backends, byte-identical.
- `kRejected` (exit 0) — a typed VERDICT, not a protocol error: feature-flag
  off (`feature flag xr_updater_v0 is off — stock chrome update behavior`),
  `signature-invalid`, `unknown transition`.
- `verify` deny verdicts (exit 0) — reason vocabulary (closed set, shared by
  both backends): `ok`, `malformed-manifest`, `unknown-field`,
  `wrong-protocol`, `non-canonical-version`, `oversize`, `insecure-url`,
  `bad-digest`, `unknown-appid`, `no-update-offered`, `downgrade-refused`,
  `equal-version-reoffer`, `replay-refused`, `epoch-revoked`,
  `unknown-epoch-key`, `unknown-signing-key`, `signature-missing`,
  `signature-invalid`, `test-verifier-refused`, `seen-persist-failed`.

Exit 2 = usage (no method, unknown option, bad `--flag k=v`).

## Laws honored (same in both backends)

- **TEST-ONLY verifier binding:** the host binds the deterministic stub
  verifier (`update/tests/test_verifier_testonly.h`; the fake mirrors it:
  `"sig:" + first 16 hex of sha256(canonical response)`). It is the default
  and ONLY selection here; the production binding is the farm row
  (`docs/release/updater-integration.md`). Refusing a release channel under a
  test verifier is CORE POLICY (`AllowedForChannel`), enforced by
  `test_verify_policy` + the golden vectors — never a comment.
- Strict deny-on-unknown parsing everywhere (envelope, notice wrapper, arg
  keys); canonical-version law; monotonic version ordering (downgrade and
  equal-version reoffer refused); replay refusal via the persisted `seen`
  set; epoch kill switch with sticky revocation.
- **Both flag states are tested** (`test_update_host`, golden vectors, the
  `about-state` matrix) — the P10 both-states convention.
- No wall clock: every time input is a caller-supplied monotonic integer, so
  every response is deterministic and reproducible byte-for-byte.
