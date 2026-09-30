// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The what-would-be-sent viewer's tests (P13-T6), run by
// xr-browser/build/webui/panel-tests.sh with XR_PANEL_SENT_BUNDLE.
//
// The law: the viewer may not hide a field. The way to honour it is to make
// hiding structurally impossible — the viewer enumerates nothing, it renders
// what the serializer emitted — and then to PROVE it by planting a field the
// viewer has no special case for and asserting it still becomes a visible row.

import test from 'node:test';
import assert from 'node:assert/strict';

const bundle = process.env.XR_PANEL_SENT_BUNDLE;
if (!bundle) {
  throw new Error('XR_PANEL_SENT_BUNDLE not set (run build/webui/panel-tests.sh)');
}
const sent = await import(bundle);

const PAYLOAD = {
  origin: 'https://example.test',
  rule_id: 'r-1',
  bundle_version: 3,
  tags: ['a', 'b'],
};

test('one serializer drives the preview: the field sets are identical', () => {
  const fields = sent.serialize(PAYLOAD).map((f) => f.path);
  const rows = sent.viewerRows(PAYLOAD).map((r) => r.path);
  assert.deepEqual(rows, fields);
  assert.deepEqual(sent.hiddenFields(PAYLOAD), []);
});

test('a PLANTED extra field becomes a visible row (the viewer cannot hide it)', () => {
  const planted = { ...PAYLOAD, cookie: 'session=9f3a' };
  const rows = sent.viewerRows(planted);
  assert.ok(rows.some((r) => r.path === 'cookie'),
    'an unhandled field must still render — silently dropping it is the defect');
  assert.deepEqual(sent.hiddenFields(planted), []);
  assert.equal(sent.viewerSummary(planted).complete, true);
});

test('a structure the viewer cannot render is MARKED, not dropped', () => {
  const deep = { nested: { a: { b: 1 } } };
  const rows = sent.viewerRows(deep);
  assert.equal(rows.length, 1);
  assert.equal(rows[0].marker, 'value-unrenderable');
  assert.equal(sent.viewerSummary(deep).unrenderable, 1);
  assert.equal(sent.viewerSummary(deep).complete, true);
});

test('nulls are rows with the null marker, not absences', () => {
  const rows = sent.viewerRows({ available_version: null });
  assert.equal(rows[0].marker, 'null');
  assert.equal(rows.length, 1);
});

test('serialization is deterministic and ordered', () => {
  const a = sent.serialize(PAYLOAD).map((f) => f.path);
  const b = sent.serialize({ bundle_version: 3, rule_id: 'r-1',
                             origin: 'https://example.test', tags: ['a', 'b'] })
                 .map((f) => f.path);
  assert.deepEqual(a, b, 'field order must not depend on insertion order');
});

test('arrays of scalars flatten with indices, so nothing hides in them', () => {
  const rows = sent.viewerRows({ tags: ['a', 'b'] });
  assert.deepEqual(rows.map((r) => r.path), ['tags[0]', 'tags[1]']);
});
