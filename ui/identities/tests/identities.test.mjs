// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// node:test suite for the xr://identities pure core (P14-T7, P14-CLOSE C-3).
// Run by xr-browser build/webui/identities-page-tests.sh against an esbuild
// bundle of identities-core.ts (XR_IDS_CORE_BUNDLE) with the core's page
// states read from identity/core/manager_page.h (XR_IDS_HOST_STATES, a JSON
// list) and the real host replies (XR_IDS_HOST_REPLIES, a JSON object the
// lane captured from the compiled identity_host), so the view's vocabulary
// cannot drift from the host's.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

const core = await import(process.env.XR_IDS_CORE_BUNDLE);
const hostStates = JSON.parse(process.env.XR_IDS_HOST_STATES ?? '[]');
const replies = process.env.XR_IDS_HOST_REPLIES
  ? JSON.parse(readFileSync(process.env.XR_IDS_HOST_REPLIES, 'utf-8'))
  : null;
const UNKNOWN = 'IDS_XR_IDENTITIES_STATE_UNKNOWN';

test('the view union equals the host page-state list', () => {
  assert.ok(hostStates.length > 0, 'host states were supplied');
  assert.deepEqual([...core.IDENTITIES_PAGE_STATES], hostStates);
});

test('every page state renders its own line; others are an honest unknown', () => {
  const seen = new Set();
  for (const state of core.IDENTITIES_PAGE_STATES) {
    const m = core.stateText({ state, detail: 'release', rows: [], purges: [] });
    assert.notEqual(m.msg, UNKNOWN, `state ${state} has an arm`);
    seen.add(m.msg);
  }
  assert.equal(seen.size, core.IDENTITIES_PAGE_STATES.length, 'no two states share a line');
  const u = core.stateText({ state: 'brand-new', detail: '', rows: [], purges: [] });
  assert.equal(u.msg, UNKNOWN);
  assert.equal(u.params.STATE, 'brand-new');
});

test('routes: list, detail, reset-all, and not-found (never a guessed page)', () => {
  const d = 'xr:7a43ffae-3a4f-4c6e-8938-e5f47f15687a';
  assert.deepEqual(core.parseRoute('xr://identities'), { kind: 'list' });
  assert.deepEqual(core.parseRoute('xr://identities/'), { kind: 'list' });
  assert.deepEqual(core.parseRoute(`xr://identities/${d}`), { kind: 'detail', domain: d });
  assert.deepEqual(core.parseRoute('xr://identities/reset-all'), { kind: 'reset-all' });
  for (const bad of ['xr://identitiesx', 'xr://shield', 'xr://identities/Work',
    'xr://identities/xr:7A43FFAE-3a4f-4c6e-8938-e5f47f15687a', 'xr://identities/reset-all/x',
    `xr://identities/${d}x`, 'https://identities']) {
    assert.deepEqual(core.parseRoute(bad), { kind: 'not-found' }, bad);
  }
});

test('the host refusal maps to dev-refused with the channel verbatim', () => {
  const m = core.pageModel({ error: 'kRejected', reason: 'build-channel-not-dev:release' });
  assert.equal(m.state, 'dev-refused');
  assert.equal(m.detail, 'release');
  assert.deepEqual(m.rows, []);
  const t = core.stateText(m);
  assert.equal(t.msg, 'IDS_XR_IDENTITIES_STATE_DEV_REFUSED');
  assert.equal(t.params.CHANNEL, 'release');
  const other = core.pageModel({ error: 'kMalformedInput', reason: 'identities must be an array' });
  assert.equal(other.state, 'unknown');
  assert.equal(core.stateText(other).msg, UNKNOWN);
  assert.equal(core.pageModel(null).state, 'unknown');
  assert.equal(core.pageModel([1]).state, 'unknown');
});

test('rows: numbers verbatim, a missing permission count is "not reported"', () => {
  const v = core.rowView({ domain: 'xr:a', display_name: 'Work', color: '#5f6b7a', glyph: 'W',
    lifecycle: 'kActive', tab_count: 2, storage_bytes: 80896, permission_count: null });
  assert.equal(v.tabs, '2');
  assert.equal(v.storage, '80896');
  assert.equal(v.permissions.msg, 'IDS_XR_IDENTITIES_NOT_REPORTED');
  assert.equal(v.lifecycle.msg, 'IDS_XR_IDENTITIES_LIFECYCLE_ACTIVE');
  assert.equal(v.label.params.NAME, 'Work');
  const z = core.rowView({ domain: 'xr:b', permission_count: 0, lifecycle: 'kHibernated' });
  assert.equal(z.permissions.msg, 'IDS_XR_IDENTITIES_PERMISSIONS_COUNT');
  assert.equal(z.permissions.params.COUNT, '0');
  assert.equal(z.name, 'xr:b', 'an unnamed identity shows its domain, never blank');
  assert.equal(z.lifecycle.msg, 'IDS_XR_IDENTITIES_LIFECYCLE_ARCHIVED');
  assert.equal(core.rowView({ lifecycle: 'kOther' }).lifecycle.msg, 'IDS_XR_IDENTITIES_LIFECYCLE_UNKNOWN');
});

test('purge needs the typed name; reset-all needs dev AND the phrase', () => {
  const row = { domain: 'xr:a', display_name: 'Work' };
  assert.equal(core.purgeAllowed(row, ''), false);
  assert.equal(core.purgeAllowed(row, 'work'), false);
  assert.equal(core.purgeAllowed(row, 'Work'), true);
  assert.equal(core.purgeAllowed({ domain: 'xr:a' }, 'xr:a'), true);
  assert.equal(core.purgeAllowed({}, ''), false, 'an empty token never confirms');
  for (const ch of ['release', 'nightly-test', '', 'Dev', 'dev ']) {
    assert.equal(core.resetAllAllowed(ch, core.RESET_ALL_PHRASE), false, `channel '${ch}'`);
  }
  assert.equal(core.resetAllAllowed('dev', ''), false);
  assert.equal(core.resetAllAllowed('dev', 'reset all'), false);
  assert.equal(core.resetAllAllowed('dev', core.RESET_ALL_PHRASE), true);
});

test('an unverified purge is always shown; a verified one is not', () => {
  const m = core.pageModel({ state: 'purge-unverified', rows: [], purges: [
    { domain: 'xr:a', verified: false, residual_kinds: ['stray-cache'] },
    { domain: 'xr:b', verified: true, residual_kinds: [] },
  ] });
  const lines = core.purgeLines(m);
  assert.equal(lines.length, 1);
  assert.equal(lines[0].params.DOMAIN, 'xr:a');
  assert.equal(lines[0].params.KINDS, 'stray-cache');
});

test('real host replies render: release refuses, dev renders every reported state', () => {
  assert.ok(replies, 'the lane supplied captured host replies');
  for (const [name, reply] of Object.entries(replies)) {
    const m = core.pageModel(reply);
    const want = name.split(':')[0];
    assert.equal(m.state, want, `${name}: ${JSON.stringify(reply).slice(0, 120)}`);
    assert.notEqual(core.stateText(m).msg, UNKNOWN, `${name} renders a known line`);
  }
  const states = new Set(Object.keys(replies).map((k) => k.split(':')[0]));
  assert.deepEqual([...states].sort(), [...hostStates].sort(), 'every host state was exercised live');
});
