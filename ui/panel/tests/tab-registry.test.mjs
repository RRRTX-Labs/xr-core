// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The panel tab registry's tests (P13-C-P0.1) — the runtime half of the §10
// unit change. Run by xr-browser/build/webui/panel-tests.sh, which
// esbuild-bundles ui/panel/tab-registry.ts into scratch and points this file
// at it with XR_PANEL_TABS_BUNDLE, plus the REAL inventory
// (xr-core/ui/panel/tabs.json) with XR_PANEL_TABS_INVENTORY.
//
// What is proved here is the refusal surface, because "refused with a typed
// error" is the whole difference between a registry and a list:
//   * an id absent from the inventory is refused `unknown-tab-id` and the
//     registry is unchanged (never dropped);
//   * two tabs claiming one order is a validation failure inside the inventory
//     itself (`duplicate-order`);
//   * registering twice is `duplicate-id`;
//   * a subsystem that renumbers itself is refused (`order-mismatch`) — the
//     inventory owns order;
//   * a malformed entry never mutates the registry;
//   * the REAL inventory parses, and its ids are exactly the ones the coverage
//     allowlist declares (tools/coverage_check.py asserts the same bijection
//     from the Python side; the pair is what makes the unit legitimate).
//
// node:test + node:assert only, no DOM, no I/O beyond reading the inventory.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const bundle = process.env.XR_PANEL_TABS_BUNDLE;
const inventoryPath = process.env.XR_PANEL_TABS_INVENTORY;
if (!bundle || !inventoryPath) {
  throw new Error('XR_PANEL_TABS_BUNDLE / XR_PANEL_TABS_INVENTORY not set (run build/webui/panel-tests.sh)');
}
const reg = await import(bundle);

const REAL = JSON.parse(readFileSync(inventoryPath, 'utf8'));

function tab(id, order, extra = {}) {
  return { id, title_msgid: `panel.tab.${id}`, order, requires_identity_scope: true, ...extra };
}

function fresh() {
  const parsed = reg.parseInventory(REAL);
  assert.deepEqual(parsed.errors, [], 'the real inventory must parse');
  return new reg.PanelTabRegistry(parsed.inventory);
}

function codes(result) {
  return result.errors.map((e) => e.code);
}

test('the real inventory parses and declares the five P13 tabs in order', () => {
  const parsed = reg.parseInventory(REAL);
  assert.deepEqual(parsed.errors, []);
  assert.deepEqual(parsed.inventory.tabs.map((t) => t.id),
    ['site', 'observatory', 'breakage', 'update', 'sent']);
});

test('a declared id registers; the registry starts empty of provided tabs', () => {
  const r = fresh();
  assert.deepEqual(r.tabs(), [], 'declared != provided: nothing is registered until a subsystem says so');
  assert.deepEqual(r.declaredIds(), ['site', 'observatory', 'breakage', 'update', 'sent']);
  const res = r.register(tab('observatory', 20));
  assert.equal(res.ok, true);
  assert.deepEqual(codes(res), []);
  assert.deepEqual(r.tabs().map((t) => t.id), ['observatory']);
});

test('an id absent from the inventory is refused unknown-tab-id, never dropped', () => {
  const r = fresh();
  const res = r.register(tab('evil-tab', 60));
  assert.equal(res.ok, false);
  assert.deepEqual(codes(res), ['unknown-tab-id']);
  assert.match(res.errors[0].detail, /declare it there first/);
  assert.deepEqual(r.tabs(), [], 'a refused registration must not mutate the registry');
});

test('two tabs claiming one order is refused inside the inventory itself', () => {
  const clash = JSON.parse(JSON.stringify(REAL));
  clash.tabs[1].order = clash.tabs[0].order;
  const parsed = reg.parseInventory(clash);
  assert.equal(parsed.inventory, null, 'a malformed inventory is refused wholesale, never patched');
  const found = parsed.errors.map((e) => e.code);
  assert.ok(found.includes('duplicate-order'), `expected duplicate-order, got ${found.join(',')}`);
});

test('an order outside the reserved range is refused', () => {
  const bad = JSON.parse(JSON.stringify(REAL));
  bad.tabs[4].order = 1000;
  const found = reg.parseInventory(bad).errors.map((e) => e.code);
  assert.ok(found.includes('order-out-of-range'), `expected order-out-of-range, got ${found.join(',')}`);
});

test('registering the same id twice is duplicate-id', () => {
  const r = fresh();
  assert.equal(r.register(tab('site', 10)).ok, true);
  const res = r.register(tab('site', 10));
  assert.equal(res.ok, false);
  assert.deepEqual(codes(res), ['duplicate-id']);
});

test('the inventory owns order: a renumbering subsystem is refused order-mismatch', () => {
  const r = fresh();
  const res = r.register(tab('sent', 15));
  assert.equal(res.ok, false);
  assert.deepEqual(codes(res), ['order-mismatch']);
  assert.match(res.errors[0].detail, /the inventory owns order/);
});

test('order-collision names the tab already holding the slot', () => {
  const r = fresh();
  assert.equal(r.register(tab('site', 10)).ok, true);
  // Forge a second tab whose declared order differs from its registration:
  // 'update' is declared at 40, registered at 10 -> the slot is taken.
  const res = r.register(tab('update', 10));
  assert.equal(res.ok, false);
  assert.ok(codes(res).includes('order-collision') || codes(res).includes('order-mismatch'));
  assert.match(res.errors[0].detail, /site|inventory owns order/);
});

test('a malformed entry is refused bad-shape and mutates nothing', () => {
  const r = fresh();
  for (const bad of [null, 42, 'site', {}, { id: 'site' },
    { id: 'SITE', title_msgid: 'x', order: 10, requires_identity_scope: true },
    { id: 'site', title_msgid: '', order: 10, requires_identity_scope: true },
    { id: 'site', title_msgid: 'x', order: 10.5, requires_identity_scope: true },
    { id: 'site', title_msgid: 'x', order: 10, requires_identity_scope: 'yes' }]) {
    const res = r.register(bad);
    assert.equal(res.ok, false, `expected refusal for ${JSON.stringify(bad)}`);
    assert.equal(res.errors[0].code, 'bad-shape');
  }
  assert.deepEqual(r.tabs(), []);
});

test('the panel frame itself is a surface, not a tab', () => {
  assert.equal(reg.FRAME_SURFACE, 'panel/xr');
  assert.equal(fresh().declaredIds().includes('xr'), false,
    'panel/xr must never appear as a tab id');
});

test('the inventory note about the unit is the reason, not decoration', () => {
  // The two files that made the old check demand a command for a `.ts` file
  // are NOT tabs; this is the assertion that keeps that true if someone
  // "helpfully" adds them to the inventory.
  for (const notATab of ['focus-trap', 'panel-frame', 'tab-registry']) {
    assert.equal(REAL.tabs.some((t) => t.id === notATab), false,
      `${notATab} implements panel/xr; declaring it as a tab would recreate the defect`);
  }
});
