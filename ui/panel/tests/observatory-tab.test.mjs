// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The Tracker Observatory core's tests (P13-T3), run by
// xr-browser/build/webui/panel-tests.sh with XR_PANEL_OBSERVATORY_BUNDLE.
//
// Windowing math is the whole point: a virtualization bug is invisible on a
// screenshot and obvious in a table of first/last indices. So the cases walk the
// edges — empty, exactly one screenful, the last screenful (which must not be
// short), a scroll past the end (which must clamp, not blank), a filter applied
// before scrolling, and the cap.
//
// The a11y half is mechanical: every RENDERED row carries aria_rowindex AND
// aria_rowcount, and rowcount is the FILTERED total, not the window — a screen
// reader must be told the list is longer than the DOM. A row on screen without a
// position is the defect the brief names ("no row present in the DOM but
// invisible to the a11y tree"), and it is asserted row by row.

import test from 'node:test';
import assert from 'node:assert/strict';

const bundle = process.env.XR_PANEL_OBSERVATORY_BUNDLE;
if (!bundle) {
  throw new Error('XR_PANEL_OBSERVATORY_BUNDLE not set (run build/webui/panel-tests.sh)');
}
const obs = await import(bundle);

function row(seq, over = {}) {
  return {
    seq, ts_millis: 1000 + seq, type: 'kSubresource', origin: 'a.test',
    why_code: 'rule-blocked', rule_id: 'r1', list_id: 'l1', ...over,
  };
}

test('the cap is 2000 and the drop count is returned, never hidden', () => {
  assert.equal(obs.RING_CAP, 2000);
  const batch = Array.from({ length: 2500 }, (_, i) => row(i));
  const out = obs.mergeIntoRing([], batch);
  assert.equal(out.rows.length, 2000);
  assert.equal(out.dropped, 500);
  // the OLDEST go: the ring keeps the newest 2000
  assert.equal(out.rows[0].seq, 500);
  assert.equal(out.rows[1999].seq, 2499);
  const more = obs.mergeIntoRing(out.rows, [row(2500)]);
  assert.equal(more.dropped, 1);
  assert.equal(more.rows[0].seq, 501);
});

test('an empty list yields an empty window, not a negative index', () => {
  const w = obs.windowFor(0, 0, 24, 240);
  assert.deepEqual([w.first, w.last, w.count, w.total], [0, 0, 0, 0]);
});

test('one screenful: first/last cover it exactly', () => {
  const w = obs.windowFor(10, 0, 24, 240, 0);   // 10 visible, no overscan
  assert.equal(w.first, 0);
  assert.equal(w.last, 10);
  assert.equal(w.count, 10);
  assert.equal(w.total, 10);
});

test('scrolling to the END yields a FULL screenful (the off-by-one that hides rows)', () => {
  const w = obs.windowFor(100, 100 * 24, 24, 240, 0);
  assert.equal(w.last, 100);
  assert.equal(w.last - w.first, 10, 'the last screenful must not be short');
});

test('scrolling PAST the end clamps instead of blanking', () => {
  const w = obs.windowFor(100, 999999, 24, 240, 0);
  assert.equal(w.last, 100);
  assert.equal(w.last - w.first, 10);
  assert.equal(w.count, 10);
});

test('overscan extends the window on both sides but never past the list', () => {
  const top = obs.windowFor(100, 0, 24, 240, 2);
  assert.equal(top.first, 0, 'no negative overscan above row 0');
  assert.equal(top.last, 12);
  const mid = obs.windowFor(100, 50 * 24, 24, 240, 2);
  assert.equal(mid.first, 48);
  assert.equal(mid.last, 62);
  const end = obs.windowFor(100, 90 * 24, 24, 240, 2);
  assert.equal(end.last, 100, 'no overscan past the end');
});

test('filter-THEN-scroll: the window is computed over the filtered length', () => {
  const rows = Array.from({ length: 100 }, (_, i) => row(i, { type: i % 2 ? 'kB' : 'kA' }));
  const kept = obs.applyFilters(rows, { type: 'kB' });
  assert.equal(kept.length, 50);
  // scrolling where the UNFILTERED list ended would blank a naive list
  const w = obs.windowFor(kept.length, 999999, 24, 240, 0);
  assert.equal(w.last, 50);
  assert.equal(w.last - w.first, 10);
  assert.equal(w.total, 50);
  const none = obs.applyFilters(rows, { type: 'kZ' });
  assert.deepEqual(obs.windowFor(none.length, 0, 24, 240, 0), { first: 0, last: 0, count: 0, total: 0 });
});

test('an absent filter means NO filter, not "match nothing"', () => {
  const rows = [row(1), row(2, { type: 'kScript' })];
  assert.equal(obs.applyFilters(rows, {}).length, 2);
  assert.equal(obs.applyFilters(rows, { origin: 'a.test' }).length, 2);
  assert.equal(obs.applyFilters(rows, { origin: 'b.test' }).length, 0);
});

test('every RENDERED row carries its a11y position, and rowcount is the WHOLE list', () => {
  const w = obs.windowFor(500, 100 * 24, 24, 240, 2);
  const meta = obs.a11yRowMeta(w);
  assert.equal(meta.length, w.count);
  for (const [i, m] of meta.entries()) {
    assert.equal(m.aria_rowindex, w.first + i + 1);
    assert.equal(m.aria_posinset, m.aria_rowindex);
    assert.equal(m.aria_rowcount, 500, 'rowcount must be the list, not the window');
  }
  assert.ok(meta[0].aria_rowcount > meta.length, 'the DOM is shorter than the list, and says so');
});

test('the header is honest about what the cap and the filter cost', () => {
  assert.deepEqual(obs.ringSummary(3141, 2000, 1141), {
    shown_of_total: '2000 of 3141', dropped_by_cap: 1141, filtered_out: 1141,
  });
});
