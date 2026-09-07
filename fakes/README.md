# xr.mojom behavioral fakes — consumption contract (P5 freeze)

These Python fakes let dependent tracks (P6–P10 and later) code against the
frozen `xr.mojom` contracts **from day one**, before any C++ binding compiles.
They are the "fake usable by dependent tracks at freeze time" required by
§1.11. They are NOT the mojom bindings — those are farm-gated (HG-9/HG-27).

## What these are / are not

- **Are:** deterministic, pure-where-required, deny-default reference behaviors
  for each interface, driveable over a documented stdio JSON protocol.
- **Are not:** wire-compatible bindings, a resolver implementation (P6), a fuzz
  target (P9), or a network service. No fake claims C++ works.

## stdio JSON protocol (how tracks drive fakes in CI)

Every fake exposes `python3 <fake>.py '<json>'` (or JSON on stdin) and prints a
single **canonical JSON** line (sorted keys, no spaces, no timestamps), then
exits 0. The result is `{"ok": <value>}` or `{"error": "<ErrorCode>"}`.

Two request shapes are used:

1. **PolicyResolver** takes the Resolve args directly:

   ```
   python3 policy_resolver.py '{"identity":{"value":"xr:...-001"},
     "origin":{"scheme":"https","registrable_domain":"example.com"},
     "request_class":"kNavigation"}'
   ```

2. **All method-style interfaces** take `{"method": "...", "args": {...}}`:

   ```
   python3 route_manager.py '{"method":"LoseAllFailClosed","args":{}}'
   python3 identity.py '{"method":"Create","args":{"grade":"kStandard"}}'
   ```

Some fakes accept helper keys in `args` to set up deterministic state in a
single shot (documented per file): `_seed` (identity, activity_log),
`_seq` (route_manager), `_unlocked` (vault). These are test affordances, not
part of the frozen contract surface.

## Driving fakes via xrctl

The dev CLI wraps this protocol:

```
tools/xrctl.py call policy_resolver Resolve '{...}'      # (xr-browser/tools)
tools/xrctl.py call route_manager LoseAllFailClosed '{}'
```

`xrctl` resolves the interface name → fake module, forwards the JSON, and
prints the canonical result with a non-zero exit only on a usage error.

## Interface → fake map

| §1.11 interface   | fake module            |
|-------------------|------------------------|
| PolicyResolver    | `policy_resolver.py`   |
| IdentityManager   | `identity.py`          |
| Shield            | `shield.py`            |
| RouteManager      | `route_manager.py`     |
| VaultService      | `vault.py`             |
| GuardLedger       | `guard.py`             |
| DownloadSafety    | `downloads.py`         |
| ActivityLog       | `activity_log.py`      |

`EffectivePolicy`, the command/list-bundle/update/theme/settings/schema and
Isolation-Card contracts are schemas/docs, not interfaces; their fakes/fixtures
are the golden vectors and example files under xr-browser/docs/contracts.

## Ground truth

- PolicyResolver output is pinned by
  `xr-browser/docs/contracts/vectors/policy-resolver-v1.json`
  (`tools/vectors_check.py` asserts byte-stable parity).
- RouteManager fail-closed is pinned by
  `xr-browser/docs/contracts/vectors/route-manager-v1.json`.
- Per-interface parity tests live in `xr-browser/docs/contracts/tests/`
  (P9 inherits this shape).
