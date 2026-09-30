// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// xr-observatory-tab — the Tracker Observatory's PURE CORE (P13-T3).
//
// A ring buffer of block events, rendered as a virtualized list. The core owns
// the two things that are easy to get wrong and impossible to see on screen:
//
//   1. THE WINDOW. `windowFor()` turns (scrollTop, rowHeight, viewportHeight,
//      overscan) into [first, last) over the FILTERED length, clamping at both
//      ends so the last screenful is never short. A virtualization bug is
//      invisible in a screenshot and obvious here.
//
//   2. THE A11Y CONTRACT. `a11yRowMeta()` gives every RENDERED row its
//      aria-rowindex/aria-rowcount/aria-posinset, with rowcount = the whole
//      filtered length, not the window. This is the "no row present in the DOM
//      but invisible to the a11y tree" law made mechanical: a screen reader is
//      told the list is longer than the DOM, and a row that is on screen but
//      carries no position is a bug the renderer cannot make, because it does
//      not get to invent the attributes — it is handed them.
//
// Plus the two data laws:
//   * the ring is CAPPED at 2000 (RING_CAP). Past the cap the OLDEST rows are
//     dropped and the count of dropped rows is RETURNED, so the UI can say
//     "2000 of 3141" instead of quietly showing a window that looks complete;
//   * filters (type / origin) are applied BEFORE the window is computed, so
//     filter-then-scroll cannot produce an empty screen at a scroll position
//     that belonged to the unfiltered list — the clamp above is what makes
//     that true rather than likely.
//
// No DOM, no I/O, no clock: the rendered half is NOT-RUN
// (method: docs/qa/browser-harness.md).

/** The ring's hard cap. 2000 rows, and the drop count is returned, never hidden. */
export const RING_CAP = 2000;

export interface RingRow {
  seq: number;
  ts_millis: number;
  type: string;
  origin: string;
  why_code: string;
  rule_id: string;
  list_id: string;
}

export interface Window {
  first: number;
  last: number;
  /** Rows actually rendered (last - first). */
  count: number;
  /** Total rows the window is a view of, AFTER filtering. */
  total: number;
}

/** Push a batch into the ring, newest-last, dropping the oldest past the cap. */
export function mergeIntoRing(ring: RingRow[], batch: RingRow[], cap: number = RING_CAP): {
  rows: RingRow[];
  dropped: number;
} {
  const rows = ring.concat(batch);
  const dropped = Math.max(0, rows.length - cap);
  return { rows: dropped > 0 ? rows.slice(dropped) : rows, dropped };
}

/** Filter by type and/or origin. Absent means "no filter", not "match nothing". */
export function applyFilters(rows: RingRow[], f: { type?: string; origin?: string }): RingRow[] {
  return rows.filter((r) =>
    (f.type === undefined || r.type === f.type) &&
    (f.origin === undefined || r.origin === f.origin));
}

/**
 * The window over `total` rows for a viewport at `scrollTop`.
 *
 * Clamped so the LAST screenful is full: scrolling to the end must not leave a
 * short window (the classic off-by-one that makes the final rows unreachable).
 * `total === 0` yields an empty window rather than a negative index.
 */
export function windowFor(
  total: number,
  scrollTop: number,
  rowHeight: number,
  viewportHeight: number,
  overscan = 2,
): Window {
  if (total <= 0 || rowHeight <= 0) {
    return { first: 0, last: 0, count: 0, total: Math.max(0, total) };
  }
  const visible = Math.max(1, Math.ceil(viewportHeight / rowHeight));
  const rawFirst = Math.floor(Math.max(0, scrollTop) / rowHeight);
  const first = Math.max(0, Math.min(rawFirst, Math.max(0, total - visible)));
  // overscan extends the window for scroll momentum, then both ends clamp.
  const last = Math.min(total, first + visible + overscan);
  return { first: Math.max(0, first - overscan), last, count: last - Math.max(0, first - overscan), total };
}

/** Every rendered row's a11y position: given, never invented by the renderer. */
export function a11yRowMeta(w: Window): Array<{
  aria_rowindex: number;
  aria_rowcount: number;
  aria_posinset: number;
}> {
  const out = [];
  for (let i = w.first; i < w.last; i += 1) {
    out.push({ aria_rowindex: i + 1, aria_rowcount: w.total, aria_posinset: i + 1 });
  }
  return out;
}

/** The honest header line: shown/total, plus what the cap and the filter cost. */
export function ringSummary(total: number, filtered: number, dropped: number): {
  shown_of_total: string;
  dropped_by_cap: number;
  filtered_out: number;
} {
  return {
    shown_of_total: `${filtered} of ${total}`,
    dropped_by_cap: dropped,
    filtered_out: total - filtered,
  };
}
