// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The breakage tab core's tests (P13-T4), run by
// xr-browser/build/webui/panel-tests.sh with XR_PANEL_BREAKAGE_BUNDLE.
//
// Two laws, and the second one is the one a reviewer would not think to check:
// the row set cannot carry page content (there is no parameter for it), and the
// confirmation cannot become a modal (the core throws before a renderer sees it).

import test from 'node:test';
import assert from 'node:assert/strict';

const bundle = process.env.XR_PANEL_BREAKAGE_BUNDLE;
if (!bundle) {
  throw new Error('XR_PANEL_BREAKAGE_BUNDLE not set (run build/webui/panel-tests.sh)');
}
const bk = await import(bundle);

const CTX = { origin: { scheme: 'https', registrable_domain: 'shop.example' },
              browser_version_tag: 'xr-1.2.0.0', rule_id: 'r-1', list_id: 'l-1',
              bundle_version: 3, action: 'kBlocked' };
const SLA = { label: 'corpus-linked-48h', hours: 48, class: 'corpus-linked' };

test('the row set is CLOSED: context, queue and SLA — and nothing else', () => {
  const keys = bk.reportRows(CTX, SLA).map((r) => r.key);
  assert.deepEqual(keys, ['origin.scheme', 'origin.registrable_domain',
    'browser_version_tag', 'rule_id', 'list_id', 'bundle_version', 'action',
    'queue', 'sla.label', 'sla.hours', 'sla.class']);
});

test('no row may carry page content, a cookie, a URL, a UA or a selector', () => {
  for (const r of bk.reportRows(CTX, SLA)) {
    assert.ok(!/cookie|user-agent|<div|selector|\?|#/i.test(r.key + r.value),
      JSON.stringify(r));
  }
});

test('the queue says fixture, in the rows, with no live mode to choose', () => {
  assert.equal(bk.reportRows(CTX, SLA).find((r) => r.key === 'queue').value, 'fixture');
  assert.ok(!JSON.stringify(bk.reportRows(CTX, SLA)).includes('live'));
});

test('the SLA is DATA: label, hours and class also appear verbatim in the payload schema', () => {
  const rows = bk.reportRows(CTX, SLA);
  assert.equal(rows.find((r) => r.key === 'sla.hours').value, '48');
  assert.equal(rows.find((r) => r.key === 'sla.class').value, 'corpus-linked');
});

test('the confirmation is inline and one click — a MODAL is refused, not rendered', () => {
  assert.deepEqual(bk.confirmShape(), { surface: 'inline', clicks: 1,
                                        attention: 'inline', forgets: false });
  for (const bad of bk.ATTENTION_VOCABULARY) {
    assert.throws(() => bk.assertConfirmable({ attention: bad }),
      /attention-budget/, bad);
  }
  assert.deepEqual(bk.assertConfirmable({}), { attention: 'inline' });
  assert.throws(() => bk.assertConfirmable({ attention: 'loud' }), /unknown-attention/);
});

test('a forbidden piece of context is NAMED when refused', () => {
  assert.deepEqual(bk.refusalFor('cookie'), { code: 'refused-pre-send:cookie' });
  assert.equal(bk.refusalFor('rule_id'), null);
  for (const f of bk.FORBIDDEN_CONTEXT) {
    assert.match(bk.refusalFor(f).code, /^refused-pre-send:/, f);
  }
});
