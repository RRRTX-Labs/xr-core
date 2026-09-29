// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The panel's focus-containment tests (P13-T1) — a REAL test, not a docstring.
//
// Run by xr-browser/build/webui/panel-tests.sh: that script esbuild-bundles
// ui/panel/focus-trap.ts into a scratch dir OUTSIDE both repos (tests never
// write into the tree) and points this file at it with
// XR_PANEL_TRAP_BUNDLE. node:test + node:assert only — the toolchain is
// dependency-frozen and this sandbox has no browser, so the DOM is a
// hand-rolled focus world of ~40 lines with the same semantics the adapter
// uses (document order, hidden/disabled/tabindex filtering).
//
// The last test is the planted leak: the containment invariant is asserted to
// FAIL for a handler that never intercepts an escaping Tab. Without it, a
// green run would prove nothing about the trap — it would only prove that the
// assertions ran. tools/negatives/p13_t1.sh goes one step further and plants
// the leak in the SOURCE, requiring this whole suite to redden.

import test from 'node:test';
import assert from 'node:assert/strict';

const bundle = process.env.XR_PANEL_TRAP_BUNDLE;
if (!bundle) {
  throw new Error('XR_PANEL_TRAP_BUNDLE is not set (run build/webui/panel-tests.sh)');
}
const trap = await import(bundle);

/** A miniature document: elements in document order, one focus ring. */
function world(order, insideIds, overrides = {}) {
  const els = new Map(order.map((id) => [id, { id, tabindex: 0, ...(overrides[id] ?? {}) }]));
  let active = null;
  return {
    elements: els,
    active: () => active,
    focus(id) {
      if (els.has(id)) active = id;
    },
    inside: (root) => (root === 'case-panel' ? insideIds.map((id) => els.get(id)) : []),
    all: () => order.map((id) => els.get(id)),
  };
}

const ORDER = ['opener', 'panel-a', 'panel-b', 'page-thing', 'footer-link'];
const INSIDE = ['panel-a', 'panel-b'];
const ROOT = 'case-panel';

test('Tab from the last focusable wraps to the first — and never leaves', () => {
  const w = world(ORDER, INSIDE);
  w.focus('panel-b');
  const action = trap.trapKeydown(w, ROOT, 'Tab', false);
  assert.deepEqual(action, { kind: 'focus', id: 'panel-a' });
  w.focus(action.id);
  assert.equal(w.active(), 'panel-a');
});

test('Shift+Tab from the first wraps to the last — and never leaves', () => {
  const w = world(ORDER, INSIDE);
  w.focus('panel-a');
  const action = trap.trapKeydown(w, ROOT, 'Tab', true);
  assert.deepEqual(action, { kind: 'focus', id: 'panel-b' });
});

test('a Tab that would land outside the frame is intercepted, not trusted', () => {
  const w = world(ORDER, INSIDE);
  w.focus('panel-b'); // next in document order is page-thing, OUTSIDE
  const action = trap.trapKeydown(w, ROOT, 'Tab', false);
  assert.equal(action.kind, 'focus');
  assert.ok(INSIDE.includes(action.id), `${action.id} must be inside the frame`);
});

test('the re-open race: focus outside the frame is pulled in, in the direction of travel', () => {
  const w = world(ORDER, INSIDE);
  w.focus('footer-link'); // host moved focus after we opened
  assert.deepEqual(trap.trapKeydown(w, ROOT, 'Tab', false), { kind: 'focus', id: 'panel-a' });
  assert.deepEqual(trap.trapKeydown(w, ROOT, 'Tab', true), { kind: 'focus', id: 'panel-b' });
  const w2 = world(ORDER, INSIDE);
  w2.focus('footer-link');
  assert.deepEqual(trap.trapKeydown(w2, ROOT, 'Tab', true), { kind: 'focus', id: 'panel-b' });
});

test('the browser move inside the frame is LEFT ALONE (the trap does not fight the tab order)', () => {
  const w = world(ORDER, INSIDE);
  w.focus('panel-a'); // next inside is panel-b: no interception needed
  assert.deepEqual(trap.trapKeydown(w, ROOT, 'Tab', false), { kind: 'ignore' });
});

test('Escape closes; other keys are ignored', () => {
  const w = world(ORDER, INSIDE);
  w.focus('panel-a');
  assert.deepEqual(trap.trapKeydown(w, ROOT, 'Escape', false), { kind: 'close' });
  assert.deepEqual(trap.trapKeydown(w, ROOT, 'Enter', false), { kind: 'ignore' });
  assert.deepEqual(trap.trapKeydown(w, ROOT, 'a', false), { kind: 'ignore' });
});

test('focusability: hidden, disabled and tabindex<0 elements are not reachable targets', () => {
  const w = world(ORDER, INSIDE, {
    'panel-b': { disabled: true },
    'page-thing': { hidden: true },
  });
  assert.deepEqual(
    trap.focusables(w, ROOT).map((t) => t.id),
    ['panel-a'],
  );
  const w2 = world(ORDER, INSIDE, { 'panel-a': { tabindex: -1 } });
  assert.deepEqual(
    trap.focusables(w2, ROOT).map((t) => t.id),
    ['panel-b'],
  );
});

test('an empty frame contains nothing and asks the caller to decide', () => {
  const w = world(ORDER, []);
  w.focus('opener');
  assert.deepEqual(trap.trapKeydown(w, ROOT, 'Tab', false), { kind: 'ignore' });
});

test('THE INVARIANT: twelve tabs in both directions never leave the frame', () => {
  for (const backward of [false, true]) {
    const w = world(ORDER, INSIDE);
    w.focus('opener');
    const visited = trap.walkTabs(w, ROOT, 12, backward);
    assert.equal(visited.length, 12);
    assert.deepEqual(
      trap.escapes(w, ROOT, visited),
      [],
      `focus escaped (backward=${backward}): ${visited.join(' -> ')}`,
    );
  }
});

// --- the planted leak ------------------------------------------------------
// The A/B on the SAME measurement. Above, the trapped walk is asserted to
// report zero escapes; here the identical walk is run with the trap bypassed
// (focus moved the way a browser would move it, with nothing intercepting),
// and the invariant MUST report the escapes. If this ever stops failing to be
// empty, the suite above is proving nothing about the trap.
// tools/negatives/p13_t1.sh plants exactly this leak in the SOURCE
// (containedTarget -> null) and requires this whole suite to redden.
test('planted leak: with containment bypassed the SAME invariant reports escapes', () => {
  const trapped = world(ORDER, INSIDE);
  trapped.focus('opener');
  const trappedVisited = trap.walkTabs(trapped, ROOT, 4, false);
  assert.deepEqual(trap.escapes(trapped, ROOT, trappedVisited), []);

  const leaky = world(ORDER, INSIDE);
  leaky.focus('opener');
  const visited = [];
  for (let i = 0; i < 4; i += 1) {
    const all = leaky.all().map((el) => el.id);
    const at = all.indexOf(leaky.active() ?? '');
    leaky.focus(all[(at + 1 + all.length) % all.length]);
    visited.push(leaky.active());
  }
  const escaped = trap.escapes(leaky, ROOT, visited);
  assert.notDeepEqual(
    escaped,
    [],
    'containment was bypassed and the invariant still reported no escape',
  );
  // The walk starts ON the opener and steps four times, so the escapes are the
  // two outside ids past the frame's block: page-thing, then footer-link.
  assert.deepEqual(
    escaped,
    ['page-thing', 'footer-link'],
    `the leak must walk out through the page, in order: ${visited.join(' -> ')}`,
  );
});
