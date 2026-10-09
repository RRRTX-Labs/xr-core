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

The seam to the resolver is one JSON object, `permission_overlay` (see
`docs/contracts/permission-overlay-v1.md`). This core never includes policy headers,
and policy never includes this core.

## Not built in P15 (NOT-RUN or deferred; reasons in the research log)

Panel surface, the one-time patch half of T2, the settings section (T8), live
propagation to open tabs, the GN build wiring (no `gn` in the sandbox), the libFuzzer
row (no clang), the isolation-matrix Chromium cells, and the Chromium-side host glue.
