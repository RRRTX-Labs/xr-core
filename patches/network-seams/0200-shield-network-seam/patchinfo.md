# patchinfo — 0200-shield-network-seam

- **id:** 0200-shield-network-seam
- **title:** XR Shield v1 network-seam consult in the network service (pre-first-packet blocking; engine injected by //xr glue)
- **owner:** @xr/platform
- **category:** network_seams
- **files:** services/network/BUILD.gn · services/network/url_loader.cc · services/network/network_context.cc (upstream hooks) + services/network/xr/xr_shield_gate.h · services/network/xr/xr_shield_gate.cc (XR-owned payload) — 5 files total, within DoD-10's ≤8 budget
- **upstream-bug-if-any:** none (embedder feature seam; no upstream defect involved)
- **retirement plan:** superseded when the farm-side //xr NetworkService glue lands as a component-level integration (Plan §1.2 intake order: components before patches — the gate payload is the component seed); obsolete at the milestone where an embedder-blocking seam upstreams. Retirement goes through `xr-patch retire` (the only sanctioned removal path, P3-T7).
- **rebase-notes:** all five transforms are anchored to lines live-read at pin d04cdb24d67b081f6cf80200ffc5233f44b61109 (research-log-P11.md R3/D11): `URLoader::ScheduleStart()`'s TRACE_EVENT head (url_loader.cc), the `#endif  // BUILDFLAG(IS_WIN) && DCHECK_IS_ON()` line in the PassKey NetworkContext ctor (network_context.cc), and the tail of the `component("network_service")` sources list (`"web_transport.h",` — BUILD.gn). `build/webui/shield_seam_roundtrip.py` re-fetches and re-proves every run (governance lane), so drift is caught by the gate, not by memory. Deliberate v1 scope: the consult sits BEFORE the ResourceScheduler defer/resume branch, so deferred-then-resumed requests cannot slip past it; `URLLoader::ResumeStart()`'s own `url_request_->Start()` is NOT separately hooked (per-hop redirect re-consult is future work — a redirect that changes the URL re-enters ScheduleStart, which IS hooked).

## Why this patch exists

XR Shield v1 (P11) blocks network requests with the vendored adblock-rust
engine BEFORE any renderer exists — the plan's ordering law. The decision
core lives in xr-core (`shield/core`, std-only C++20, zero Chromium
includes) behind the `BlockingEngine` injection contract; the vendored
engine rides the `xr_shield_engine` C ABI (`shield/engine/xr_shield_engine.h`).
What xr-core cannot own is the CALL SITE inside Chromium's network service:
the one place where a load can still be stopped before the first packet is
`URLLoader::ScheduleStart()`. A patch is the chosen mechanism because the
seam is three guarded hook blocks plus two XR-owned files — no upstream
behavior changes when `ENABLE_XR_SHIELD` is undefined (stock Chromium
compiles byte-identical logic), and the whole consult is a thin data path
(redact → ask the injected engine → map the verdict). Policy — lists,
scopes, exceptions, posture, events — never crosses the seam.

## Upstream drift risk

High-churn file: `services/network/url_loader.cc` (~2.7k lines at the pin)
is regularly refactored (scheduler, keepalive, ORB work). The ScheduleStart
TRACE_EVENT anchor and the `NotifyCompleted(int)` completion path are the
two contracts this patch relies on; if either renames, the round-trip gate
reddens at the pin (anchor-not-found is a hard error, never a fuzzy apply)
and the rebaser re-reads the seam live. `network_context.cc`'s ctor anchor
is structurally stable (receiver binding). The BUILD.gn sources-list tail
anchor moves with any new `w*`/`x*` source file — again caught, never
guessed. Detection: `build/webui/shield_seam_roundtrip.py` in run_checks +
the governance workflow (real fetch through the choke point, no fixtures).
