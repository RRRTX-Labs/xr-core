# permissions/ — the P15 permission firewall core (ADR-0051, DRAFT)

- `core/` — S0, std-only C++20, pure. The overlay store (`store.*`: strict load,
  canonical save, the resolver's projection), the state-machine ops (`ops.*`:
  every mutation returns a new store plus its audit rows), the audit rows
  (`audit.*`: permission-audit-event-v1 and the frozen ActivityRow), the labelled
  site merge and prompt suppression (`merge.*`), prompt presentation and the T3
  attention ceiling (`present.*`), and the deny-only extras envelope (`envelope.*`).
  No clock, no RNG, no I/O. Time is always a parameter.
- `tests/` — the suite (`make test`). Nine suites, all deterministic. `test_cross_core`
  runs the 33 additive overlay vectors (`docs/contracts/vectors/permission-overlay-v1.json`)
  through the REAL `policy/core` parser and resolver.
- `bench/` — `make bench` (MEASURED on one host; the hardware caveat is printed).
- `host/` — the CLOCK-OWNING layer (P15-CLOSE C2): `permissions_host` reads the clock,
  passes `now_ms` into `core/ops`, and writes the store atomically. Protocol:
  `host_protocol.md`; byte-parity fake: `fakes/permissions.py`. `tests/test_kill9.cc`
  SIGKILLs the built host mid-sweep.
- `BUILD.gn` — the gn-side wiring (core source_set + host executable); gn/ninja are
  absent here, so it is exercised on the rig lane only.

The seam to the resolver is one JSON object, `permission_overlay` (see
`docs/contracts/permission-overlay-v1.md`). This core never includes policy headers,
and policy never includes this core.

## Not built in P15 (NOT-RUN or deferred; reasons in the research log)

Panel surface, the one-time patch half of T2, live propagation to open tabs, running
the GN build (no `gn` in the sandbox; the file exists), the libFuzzer run (no clang),
the isolation-matrix Chromium cells, and the Chromium-side host glue.
