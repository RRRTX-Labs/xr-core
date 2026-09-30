// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The update tab core's tests (P13-T5). Run by
// xr-browser/build/webui/panel-tests.sh with XR_PANEL_UPDATE_BUNDLE.
//
// The T1 tie is not "we did not write an installer": it is that the tab CANNOT
// SAY the wrong thing. So these tests enumerate the whole closed surface —
// every state, every verb — and assert that nothing in it can read as progress.

import test from 'node:test';
import assert from 'node:assert/strict';

const bundle = process.env.XR_PANEL_UPDATE_BUNDLE;
if (!bundle) {
  throw new Error('XR_PANEL_UPDATE_BUNDLE not set (run build/webui/panel-tests.sh)');
}
const up = await import(bundle);

const CURRENT = { channel: 'stable', running_version: '1.2.0.0',
                  available_version: '1.2.0.0', checked_at: '2026-09-30T00:00:00Z' };
const NEWER = { ...CURRENT, available_version: '1.3.0.0' };

test('update availability is a comparison of two version strings', () => {
  assert.equal(up.updateAvailable(null), false);
  assert.equal(up.updateAvailable({ ...CURRENT, available_version: null }), false);
  assert.equal(up.updateAvailable(CURRENT), false);
  assert.equal(up.updateAvailable(NEWER), true);
});

test('the verb set is CLOSED and contains no install/restart/apply', () => {
  const verbs = new Set();
  for (const s of [null, CURRENT, NEWER, { ...CURRENT, available_version: null }]) {
    verbs.add(up.actionFor(s).verb);
  }
  assert.deepEqual([...verbs].sort(), ['none', 'open-update-page']);
  for (const v of verbs) {
    assert.ok(!/install|restart|apply|download|update-now/i.test(v), v);
  }
});

test('the only action offered is a link to the update page', () => {
  const a = up.actionFor(NEWER);
  assert.equal(a.verb, 'open-update-page');
  assert.equal(a.label_key, 'panel.update.action.openPage');
});

test('states that would imply the product updates itself are FORBIDDEN, and throw', () => {
  for (const bad of up.FORBIDDEN_STATES) {
    assert.throws(() => up.assertKnownState(bad), /forbidden-state/, bad);
  }
  assert.throws(() => up.assertKnownState('making-coffee'), /unknown-state/);
  for (const good of up.UPDATE_STATES) {
    assert.equal(up.assertKnownState(good), good);
  }
});

test('the rows come from P10 channel state and nothing else', () => {
  const rows = up.updateRows(NEWER);
  const keys = rows.map((r) => r.key);
  assert.deepEqual(keys, ['state', 'channel', 'running_version', 'checked_at', 'available_version']);
  assert.equal(up.updateRows(null).find((r) => r.key === 'state').value, 'channel-unknown');
  // no row may claim a download, a progress figure, or a completion
  for (const r of rows) {
    assert.ok(!/progress|percent|download|installed/i.test(r.key + r.value), JSON.stringify(r));
  }
});

test('the summary says out loud that it implies nothing', () => {
  const s = up.updateSummary(NEWER);
  assert.equal(s.implies_automatic_update, false);
  assert.deepEqual(s.verbs, ['open-update-page']);
});
