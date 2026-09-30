// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// xr-sent-tab — the "what would be sent" viewer's PURE CORE (P13-T6).
//
// This is where the product's privacy guarantee #1 begins: before anything
// leaves the machine, the user can see exactly what would leave. The guarantee
// is only as good as its weakest link, and the weakest link in every such UI is
// the SECOND implementation — a preview that enumerates fields itself, drifts
// from the serializer, and quietly stops showing the field that was added last.
//
// So there is ONE serializer here. `serialize()` flattens a payload into
// {path, value} pairs, and `viewerRows()` is a pure function OF THAT OUTPUT. The
// viewer cannot enumerate fields independently, because it never sees the
// payload's shape — it sees the serialized field list and nothing else.
//
// Totality is the other half: every field the serializer emits MUST produce a
// row. A value the viewer cannot render (an object it does not know, a
// structure deeper than it flattens) still produces a row, marked
// `value-unrenderable`, with the field's PATH — because an unrendered field that
// silently disappears is exactly the defect this file exists to prevent. The
// test plants an extra payload field and asserts it becomes a visible row.
//
// No DOM, no I/O, no clock. The rendered half is NOT-RUN
// (method: docs/qa/browser-harness.md).

/** One serialized field: the only shape the viewer is allowed to consume. */
export interface SerializedField {
  path: string;
  value: string | number | boolean | null;
  /** Present when the value could not be flattened to a scalar. */
  unrenderable?: true;
}

export interface ViewerRow {
  path: string;
  value: string;
  marker: 'value' | 'null' | 'value-unrenderable';
}

const SCALAR = new Set(['string', 'number', 'boolean']);

/** THE serializer: payload -> a flat, ordered field list. Deterministic. */
export function serialize(payload: Record<string, unknown>, prefix = ''): SerializedField[] {
  const out: SerializedField[] = [];
  for (const key of Object.keys(payload).sort()) {
    const path = prefix === '' ? key : `${prefix}.${key}`;
    const value = payload[key];
    if (value === null || value === undefined) {
      out.push({ path, value: null });
      continue;
    }
    if (SCALAR.has(typeof value)) {
      out.push({ path, value: value as string | number | boolean });
      continue;
    }
    if (Array.isArray(value)) {
      // An array of scalars is flattened; anything else is a STRUCTURE the
      // viewer declines to guess at, and it says so instead of hiding it.
      if (value.every((v) => v === null || SCALAR.has(typeof v))) {
        value.forEach((v, i) => out.push({ path: `${path}[${i}]`, value: v as string | number | boolean | null }));
      } else {
        out.push({ path, value: null, unrenderable: true });
      }
      continue;
    }
    // Unknown structure: still a field, still visible, never silently dropped.
    out.push({ path, value: null, unrenderable: true });
  }
  return out;
}

/** The viewer's rows: a pure function of the serializer's output. */
export function viewerRows(payload: Record<string, unknown>): ViewerRow[] {
  return serialize(payload).map((f) => ({
    path: f.path,
    value: f.value === null ? '' : String(f.value),
    marker: f.unrenderable ? 'value-unrenderable' : f.value === null ? 'null' : 'value',
  }));
}

/**
 * Every serialized field must have a row. Returns the paths that would be
 * HIDDEN — an empty array is the only acceptable answer, and the test plants a
 * field to prove the function can find one.
 */
export function hiddenFields(payload: Record<string, unknown>,
                             rows: ViewerRow[] = viewerRows(payload)): string[] {
  const shown = new Set(rows.map((r) => r.path));
  return serialize(payload).filter((f) => !shown.has(f.path)).map((f) => f.path);
}

/** The preview's honest header: how many fields, and how many are unrenderable. */
export function viewerSummary(payload: Record<string, unknown>): {
  fields: number;
  unrenderable: number;
  complete: boolean;
} {
  const fields = serialize(payload);
  const unrenderable = fields.filter((f) => f.unrenderable).length;
  return { fields: fields.length, unrenderable,
           complete: hiddenFields(payload).length === 0 };
}
