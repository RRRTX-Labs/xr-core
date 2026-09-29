// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The panel's surrogate bench harness (P13-T7). NOT a browser measurement.
//
// What this can honestly measure in a sandbox with no browser: the FRAME'S PURE
// CORE — the code that decides where focus may go — over a synthetic document.
// That is a SUBSET of "panel open ≤150 ms" (the real number needs layout, style
// and paint), so the comparator treats it as a `trend` row that may never read
// MET; the browser-side halves are NOT-RUN with their method in
// docs/qa/browser-harness.md#panel-open-150ms.
//
// Two rows:
//   * panel_open_core_us      — median µs for the open-path decision (contain
//                               the current focus into the frame) over a
//                               4096-focusable synthetic document;
//   * panel_open_subtree_scans — how many times the open path reads a
//                               subtree, and which one. This is the lazy-tab
//                               property as a deterministic COUNT rather than a
//                               timing: the frame must read its OWN subtree,
//                               never walk the document's 4096 focusables. A
//                               count cannot be flaky, so the claim does not
//                               depend on the machine it ran on.
//
// Run by tools/panel_bench.py (xr-browser), which bundles focus-trap.ts with
// the pinned toolchain into a scratch dir outside both repos and passes the
// bundle path in XR_PANEL_TRAP_BUNDLE. Prints one JSON object on stdout.

const bundle = process.env.XR_PANEL_TRAP_BUNDLE;
if (!bundle) {
  throw new Error('XR_PANEL_TRAP_BUNDLE is not set (run tools/panel_bench.py)');
}
const trap = await import(bundle);

const DOC_TARGETS = 4096;
const FRAME_TARGETS = 64;
const ITERS = 200;

function document_() {
  // Document order: page elements first, the frame's subtree in the middle,
  // more page elements after — the shape that makes an escaping Tab real.
  const el = (id) => ({ id, tabindex: 0 });
  const before = Array.from({ length: DOC_TARGETS - FRAME_TARGETS }, (_, i) => el(`page-${i}`));
  const frame = Array.from({ length: FRAME_TARGETS - 2 }, (_, i) => el(`tab-${i}`));
  const inside = ['frame-heading', 'frame-close', ...frame];
  const head = before.slice(0, 100);
  const tail = before.slice(100);
  const all = [...head, ...inside, ...tail].map(el);
  const byId = new Map(all.map((e) => [e.id, e]));
  let active = 'page-50'; // focus starts OUTSIDE the frame: the open path runs
  let scans = 0;
  return {
    world: {
      active: () => active,
      focus(id) {
        if (byId.has(id)) active = id;
      },
      inside: (root) => {
        if (root !== 'xr-panel-frame-1') return [];
        scans += 1;
        return inside.map((id) => byId.get(id));
      },
      all: () => all,
    },
    root: 'xr-panel-frame-1',
    scanCalls: () => scans,
  };
}

/** Median of `iters` timed runs of the open-path decision. */
function timeOpenPath() {
  const world = document_();
  // Warm up: the first call pays the JIT's compilation, which is not the
  // frame's cost in a real session (the bundle is parsed once, long before).
  for (let i = 0; i < 20; i += 1) trap.containedTarget(world.world, world.root, 'page-50', false);
  const samples = [];
  for (let i = 0; i < ITERS; i += 1) {
    const t0 = process.hrtime.bigint();
    trap.containedTarget(world.world, world.root, 'page-50', false);
    samples.push(Number(process.hrtime.bigint() - t0) / 1000);
  }
  samples.sort((a, b) => a - b);
  return samples[Math.floor(samples.length / 2)];
}

/** How many focusables the open path touches, and where focus lands. */
function scanMeasure() {
  // `scans` counts inside()-reads; `landed` proves the frame's own first
  // focusable took focus. Together they are the locality claim.
  const world = document_();
  const action = trap.trapKeydown(world.world, world.root, 'Tab', false);
  return {
    scans: world.scanCalls(),
    landed: action.kind === 'focus' ? action.id : null,
  };
}

const openCoreUs = timeOpenPath();
const scan = scanMeasure();

process.stdout.write(
  JSON.stringify(
    {
      panel_open_core_us: { unit: 'us', value_us: openCoreUs, n: ITERS },
      panel_open_subtree_scans: { unit: 'scans', value_us: scan.scans },
      doc_targets: DOC_TARGETS,
      frame_targets: FRAME_TARGETS,
      landed_on_open: scan.landed,
    },
    null,
    1,
  ) + '\n',
);
