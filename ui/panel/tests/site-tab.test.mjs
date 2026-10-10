// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The Site tab core's tests (P13-T2). Run by xr-browser/build/webui/
// panel-tests.sh, which esbuild-bundles ui/panel/site-tab.ts into scratch and
// points this file at it with XR_PANEL_SITE_BUNDLE.
//
// What is proved here, and why each one is a law rather than a detail:
//   * the trust dial is a FUNCTION of the scope set — the same scopes give the
//     same dial, an empty set gives `standard`, and there is no state to get
//     out of sync because there is no store (the brief's "reuse the single
//     scope object, never a second store" made testable: the module exposes no
//     setter, and every control returns a REQUEST);
//   * shields-down is exactly one bit, and both consumers are named, so the UI
//     cannot quietly grow its own opinion;
//   * the WHY DRILL covers every code in the reason-code table, and an unknown
//     code renders a TYPED FALLBACK — a dropped row would be indistinguishable
//     from "nothing was blocked";
//   * the certificate rows are verbatim or explicitly absent: no invented
//     detail, and "no cert observed" is a row, not silence;
//   * the isolation card is data-driven, row for row;
//   * ordering is deterministic (scope_id, permission), because a panel whose
//     rows reshuffle between renders is a panel nobody can read.
//
// `detail` strings are machine tokens (l10n_extract R4). node:test only.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const bundle = process.env.XR_PANEL_SITE_BUNDLE;
const whyTablePath = process.env.XR_PANEL_WHY_TABLE;
if (!bundle || !whyTablePath) {
  throw new Error('XR_PANEL_SITE_BUNDLE / XR_PANEL_WHY_TABLE not set (run build/webui/panel-tests.sh)');
}
const site = await import(bundle);
const WHY = JSON.parse(readFileSync(whyTablePath, 'utf8'));

function scope(over = {}) {
  return {
    expiry_mono: 1000, identity: 'default', list_id: '', reason: 'user',
    rule_id: '', scope_id: 'rule:x', site: 'example.test', workspace: '', ...over,
  };
}

test('scope id is the canonical site-toggle form', () => {
  assert.equal(site.siteToggleScopeId('example.test'), 'site-toggle:example.test');
});

test('the dial is a function of the scope set, not a store', () => {
  const empty = [];
  assert.equal(site.trustDial(empty, 'example.test').state, 'standard');
  const down = [scope({ scope_id: 'site-toggle:example.test' })];
  assert.equal(site.trustDial(down, 'example.test').state, 'shields-down');
  // A different site's bit is not this site's bit.
  assert.equal(site.trustDial(down, 'other.test').state, 'standard');
  // …and the same input twice gives the same answer (no hidden state).
  assert.deepEqual(site.trustDial(down, 'example.test'), site.trustDial(down, 'example.test'));
});

test('the module exposes no mutator: every control returns a request', () => {
  const t = site.toggleRequest('example.test', true);
  assert.equal(t.method, 'site-toggle');
  assert.equal(t.args.scope_id, 'site-toggle:example.test');
  const a = site.exceptionRequest('example.test', 'rule:r1', 'exception-add', 42, 'default');
  assert.equal(a.method, 'exception-add');
  assert.deepEqual(Object.keys(a.args.scope).sort(), [
    'expiry_mono', 'identity', 'list_id', 'reason', 'rule_id', 'scope_id',
    'site', 'workspace',
  ]);
  assert.equal(a.args.scope.expiry_mono, 42);
  for (const name of Object.keys(site)) {
    assert.ok(!/^set[A-Z]/.test(name), `the core must not expose a setter: ${name}`);
  }
});

test('both consumers of the shields-down bit are named', () => {
  const d = site.trustDial([], 'example.test');
  assert.deepEqual(d.read_by, ['xr-core/shield', 'xr-core/cosmetic']);
  assert.equal(d.by, 'site-toggle');
});

test('exception rows are the site scopes, ordered and complete', () => {
  const rows = site.exceptionRows(
    [scope({ scope_id: 'rule:b' }), scope({ scope_id: 'rule:a' }),
     scope({ scope_id: 'rule:z', site: 'other.test' })],
    'example.test',
  );
  assert.deepEqual(rows.map((r) => r.scope_id), ['rule:a', 'rule:b']);
  assert.equal(rows[0].remove, 'exception-remove');
});

test('certificate rows are verbatim, and absence says so', () => {
  assert.deepEqual(site.certRows(null), [
    { subject: '', issuer: '', note: 'no-certificate-observed' },
  ]);
  const rows = site.certRows({
    subject: 'example.test', issuer: 'test-ca', valid_from: '2026-01-01',
    valid_to: '2027-01-01', protocol: 'TLS1.3', verified: true,
  });
  assert.equal(rows[0].verified, 'chain-verified');
  assert.equal(rows[0].protocol, 'TLS1.3');
  const bad = site.certRows({
    subject: 'example.test', issuer: 'test-ca', valid_from: '2026-01-01',
    valid_to: '2027-01-01', protocol: 'TLS1.3', verified: false,
  });
  assert.equal(bad[0].verified, 'chain-unverified');
});

test('the why drill covers EVERY code in the reason-code table', () => {
  const codes = WHY.codes.map((c) => c.why_code);
  assert.ok(codes.length >= 13, 'the reason-code table shrank');
  const rows = site.blockedDrill(
    codes.map((why_code, i) => ({ why_code, count: i + 1 })), WHY.codes);
  assert.equal(rows.length, codes.length);
  for (const r of rows) {
    assert.equal(r.rendered, 'table', `${r.why_code} did not render from the table`);
    assert.ok(r.text_key.startsWith('shield.why.'), r.text_key);
  }
});

test('an unknown why_code is a TYPED FALLBACK, never a dropped row', () => {
  const rows = site.blockedDrill([{ why_code: 'not-a-real-code', count: 7 }], WHY.codes);
  assert.equal(rows.length, 1);
  assert.equal(rows[0].rendered, 'typed-fallback');
  assert.equal(rows[0].text_key, 'shield.why.unknown.not-a-real-code');
  assert.equal(rows[0].count, 7);
});

test('the isolation card is data-driven, row for row', () => {
  const card = [{ measure: 'cookies', value: 'partitioned', source: 'evidence/P4/measured.json' }];
  assert.deepEqual(site.isolationCardRows(card), card);
  assert.deepEqual(site.isolationCardRows([]), []);
});

test('permissions summary sorts and names where grants live', () => {
  const out = site.permissionsSummary([
    { permission: 'notifications', state: 'denied' },
    { permission: 'camera', state: 'ask' },
  ]);
  assert.deepEqual(out.items.map((i) => i.permission), ['camera', 'notifications']);
  assert.equal(out.grants_live_in, 'P15-permissions');
});

test('the always-on generic set is not exception-able, and says why', () => {
  const rows = site.genericHideSetRows({
    count: 33, rule_ids: ['gen-1', 'gen-2'],
    reason_key: 'shield.generic.alwaysOn',
  });
  assert.equal(rows.length, 2);
  for (const r of rows) {
    assert.equal(r.exceptionable, false);
    assert.equal(r.remove, '', 'a non-exception-able rule must emit NO remove action');
    assert.ok(r.reason_key.length > 0, 'a locked row without a reason is a dead end');
    assert.equal(r.set_size, '33');
  }
});

test('the scriptlet registry is present and INERT, and an unknown flag is never "off"', async () => {
  const rows = site.scriptletRows({ rules: 12 }, 'off');
  assert.deepEqual(rows.find((r) => r.row === 'scriptlet_registry').state, 'present');
  assert.deepEqual(rows.find((r) => r.row === 'scriptlet_execution').state, 'INERT');
  assert.equal(rows.find((r) => r.row === 'scriptlet_execution').detail, 'flag:off');
  const unknown = site.scriptletRows({ rules: 12 }, 'unknown');
  assert.equal(unknown.find((r) => r.row === 'scriptlet_execution').state, 'unknown');
  assert.equal(unknown.find((r) => r.row === 'scriptlet_execution').detail,
               'state-not-observed');
  assert.equal(site.scriptletRows(null, 'off')[0].state, 'absent');
});

test('an isolation row with no measurement says not-run, never an empty cell', async () => {
  const notRun = site.isolationRowLabel({ prop: 'process-isolation' });
  assert.deepEqual(notRun, { prop: 'process-isolation', state: 'not-run',
                             method: 'docs/qa/browser-harness.md' });
  assert.equal(site.isolationRowLabel({ prop: 'p', measured: true, holds: true }).state,
               'holds');
  assert.equal(site.isolationRowLabel({ prop: 'p', measured: true, holds: false }).state,
               'violated');
});

test('every isolation row value is a machine token (no space-bearing prose in ui/**)', () => {
  for (const row of [{ prop: 'process-isolation' },
                     { prop: 'p', measured: true, holds: true },
                     { prop: 'p', measured: true, holds: false }]) {
    for (const [k, v] of Object.entries(site.isolationRowLabel(row))) {
      assert.ok(!/\s/.test(String(v)), `${k}=${v} carries whitespace`);
    }
  }
});

// P14-CLOSE C-5: the Isolation Card's identity rows come from DATA. The lane
// points XR_PANEL_IDENTITY_CARD at the REAL generated file
// (xr-core/test/isolation/identity-card-rows.json), so the card is proved
// against what the matrix generator wrote, not against a fixture.
function realCard() {
  const p = process.env.XR_PANEL_IDENTITY_CARD;
  assert.ok(p, 'XR_PANEL_IDENTITY_CARD not set (run build/webui/panel-tests.sh)');
  return JSON.parse(readFileSync(p, 'utf8'));
}

test('the identity card renders every generated row, row for row', () => {
  const doc = realCard();
  const view = site.identityCardView(doc);
  assert.equal(view.refused, false, `generated card refused: ${view.reason}`);
  assert.equal(view.rows.length, doc.rows.length);
  assert.deepEqual(view.rows, doc.rows.map((r) => ({ prop: r.prop, state: r.state,
    measured: r.measured, pairs: r.pairs, method: r.method })));
  // The identity mechanisms the P14 core proves are on the card as data.
  for (const prop of ['process-isolation', 'disposable-zero-residue',
                      'identity-derivation-probe', 'session-restore-no-bleed']) {
    const row = view.rows.find((r) => r.prop === prop);
    assert.ok(row, `${prop} missing from the card`);
    assert.equal(row.state, 'holds');
    assert.equal(row.measured, row.pairs);
  }
});

test('the identity card REFUSES prose — the whole card, typed, never a fallback sentence', () => {
  const doc = realCard();
  const plant = (i, over) => ({ ...doc, rows: doc.rows.map((r, j) => (j === i ? { ...r, ...over } : r)) });
  assert.deepEqual(site.identityCardView(plant(0, { prop: 'Your identities are fully isolated' })),
                   { refused: true, reason: 'card-prose:0' });
  assert.deepEqual(site.identityCardView(plant(1, { method: 'trust us' })),
                   { refused: true, reason: 'card-prose:1' });
  assert.deepEqual(site.identityCardView(plant(2, { state: 'Isolated' })),
                   { refused: true, reason: 'card-prose:2' });
  assert.deepEqual(site.identityCardView(plant(2, { state: 'mostly' })),
                   { refused: true, reason: 'card-state:2' });
  assert.deepEqual(site.identityCardView(plant(3, { note: 'extra copy' })),
                   { refused: true, reason: 'card-row-shape:3' });
  assert.deepEqual(site.identityCardView(plant(0, { measured: 4 })),
                   { refused: true, reason: 'card-unmeasured-hold:0' });
  assert.deepEqual(site.identityCardView(plant(0, { measured: 6 })),
                   { refused: true, reason: 'card-count:0' });
  const nr = doc.rows.findIndex((r) => r.state === 'not-run');
  assert.deepEqual(site.identityCardView(plant(nr, { measured: 1 })),
                   { refused: true, reason: `card-not-run-measured:${nr}` });
  assert.deepEqual(site.identityCardView({ ...doc, contract: 'prose' }),
                   { refused: true, reason: 'card-contract' });
  assert.deepEqual(site.identityCardView({ ...doc, rows: [] }),
                   { refused: true, reason: 'card-no-rows' });
  assert.deepEqual(site.identityCardView(null), { refused: true, reason: 'card-not-object' });
});
