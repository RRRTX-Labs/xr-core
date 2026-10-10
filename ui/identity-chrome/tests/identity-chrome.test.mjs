// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Identity chrome core tests (P14-T4, P14-CLOSE C-1), run by
// xr-browser/build/webui/identity-chrome-tests.sh against an esbuild bundle of
// chrome-core.ts, with the REAL states.json, tokens.json and committed
// snapshots pointed in.
//
// Laws: every state value and every layout value gets a case. Color never
// stands alone. A low-contrast mark carries an edge. Refusals carry no
// identity value. The core reproduces EVERY committed per-layout x per-theme
// structural snapshot byte-for-byte as parsed JSON, so the Python generator
// and this core cannot drift.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

const env = (k) => {
  const v = process.env[k];
  if (!v) throw new Error(`${k} not set (run build/webui/identity-chrome-tests.sh)`);
  return v;
};
const core = await import(env('XR_IDC_CORE_BUNDLE'));
const table = JSON.parse(readFileSync(env('XR_IDC_STATES'), 'utf-8'));
const themes = JSON.parse(readFileSync(env('XR_IDC_TOKENS'), 'utf-8')).themes;
const snapDir = env('XR_IDC_SNAPSHOTS');

const sampleFor = (state) => table.samples.find((s) => s.state === state);

test('every state value gets a case: class, SR description, pill name, border', () => {
  const states = Object.keys(table.states);
  assert.ok(states.length >= 3, 'standard, disposable and tor-unbound at least');
  for (const sid of states) {
    const spec = table.states[sid];
    const s = sampleFor(sid);
    assert.ok(s, `state ${sid} has no sample`);
    const n = core.structure(table, themes.light, s, 'top');
    assert.deepEqual(n.class, ['xr-idc', spec.class]);
    assert.ok(spec.sr && n.tab.description.msg === spec.sr, `state ${sid} has no SR string`);
    assert.equal(n.tab.description.params.NAME, s.name);
    assert.equal(n.pill.role, 'button');
    assert.equal(n.pill.name.msg, spec.accessible_name);
    assert.equal(n.pill.name.params.ROUTE.msg, spec.route);
    assert.equal(n.window_border === null, spec.window_border === null);
  }
});

test('disposable = amber dashed full-window border; tor-unbound = solid in its color', () => {
  const d = core.structure(table, themes.light, sampleFor('disposable'), 'top');
  assert.equal(d.window_border.style, 'dashed');
  assert.equal(d.window_border.px, 2);
  assert.equal(d.window_border.color, themes.light['danger-caution']);
  const t = core.structure(table, themes.dark, sampleFor('tor-unbound'), 'top');
  assert.equal(t.window_border.style, 'solid');
  assert.equal(t.window_border.color, sampleFor('tor-unbound').color);
  assert.equal(core.structure(table, themes.light, sampleFor('standard'), 'top').window_border, null);
});

test('every layout value gets a case, and color is never alone', () => {
  const s = sampleFor('standard');
  const expect = {
    top: ['bar', 'block-start', true], vertical: ['bar', 'inline-start', true],
    compact: ['bar', 'block-start', false], rail: ['dot', undefined, false],
  };
  assert.deepEqual(Object.keys(table.layouts).sort(), Object.keys(expect).sort());
  for (const [lid, [mark, side, label]] of Object.entries(expect)) {
    const kids = core.structure(table, themes.light, s, lid).tab.children;
    assert.equal(kids[0].part, mark, lid);
    assert.equal(kids[0].side, side, lid);
    if (mark === 'bar') assert.equal(kids[0].px, 2, `${lid}: the bar is 2 px`);
    const glyph = kids.find((k) => k.part === 'glyph');
    assert.ok(glyph && glyph.text.length > 0, `${lid}: a glyph/initial accompanies the color`);
    assert.equal(kids.some((k) => k.part === 'label'), label, lid);
  }
  const rail = core.structure(table, themes.light, s, 'rail').tab.children;
  assert.equal(rail[1].text, s.name.slice(0, 1).toUpperCase(), 'rail = dot + first letter');
});

test('a mark below 3:1 carries a text-token edge that reaches 3:1; above, none', () => {
  let low = 0;
  for (const [tid, theme] of Object.entries(themes)) {
    for (const s of table.samples) {
      const n = core.structure(table, theme, s, 'top');
      const bar = n.tab.children[0];
      const ratio = core.contrast(s.color, theme[table.tokens.strip_surface]);
      assert.equal(n.contrast.mark, ratio);
      if (ratio < table.contrast_min.non_text) {
        low += 1;
        assert.ok(bar.edge && bar.edge.ratio >= table.contrast_min.non_text, `${tid}/${s.template}`);
      } else {
        assert.equal(bar.edge, null, `${tid}/${s.template}`);
      }
      assert.ok(n.contrast.pill_text >= table.contrast_min.text, `${tid}: pill text`);
    }
  }
  assert.ok(low > 0, 'the shipped palette has low-contrast cases; the fallback must be exercised');
});

test('refusals are typed and carry no identity value', () => {
  const secret = { ...sampleFor('standard'), name: 'Secret-Name-7f3a', state: 'nope' };
  assert.throws(() => core.structure(table, themes.light, secret, 'top'),
    (e) => e instanceof core.ChromeRefusal && !e.message.includes('Secret-Name-7f3a'));
  assert.throws(() => core.structure(table, themes.light, sampleFor('standard'), 'sideways'),
    (e) => e instanceof core.ChromeRefusal && e.message === 'unknown-layout');
  assert.throws(() => core.structure(table, { surface: '#fff' }, sampleFor('standard'), 'top'),
    (e) => e instanceof core.ChromeRefusal);
});

test('the SR announcement resolves through the string map', () => {
  const s = sampleFor('disposable');
  const strings = { [table.states.disposable.sr]: 'Disposable identity: {NAME}' };
  assert.equal(core.srAnnouncement(table, s, strings), 'Disposable identity: Disposable');
  assert.equal(core.srAnnouncement(table, s, {}), table.states.disposable.sr);
});

test('the core reproduces EVERY committed per-layout x per-theme snapshot', () => {
  const files = readdirSync(snapDir).filter((f) => f.endsWith('.json')).sort();
  const want = [];
  for (const lid of Object.keys(table.layouts)) {
    for (const tid of Object.keys(themes)) want.push(`${lid}.${tid}.json`);
  }
  assert.deepEqual(files, want.sort(), 'one snapshot per layout x theme, no stale files');
  for (const f of files) {
    const doc = JSON.parse(readFileSync(join(snapDir, f), 'utf-8'));
    const mine = core.snapshot(table, themes[doc.theme], doc.theme, doc.layout, table.samples);
    assert.deepEqual(mine, doc, `${f}: the TypeScript core drifted from the generator`);
  }
});
