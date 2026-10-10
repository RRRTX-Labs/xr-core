// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// xr://identities — the PURE CORE of the identity manager dev page (P14-T7,
// P14-CLOSE C-3). No DOM, no I/O, no clock: identities.ts (the lit view)
// renders what these functions return, and tests/identities.test.mjs runs
// them under node:test against the real host vocabulary.
//
// DEV BUILDS ONLY, enforced by the identity host's real `--build-channel`
// startup gate (identity_host `manager-page` / `reset-all` refuse with
// `build-channel-not-dev:<channel>` on any other channel and render no page
// bytes) and by the roster: `identities.page` rides the build.channel-dev
// predicate. pageModel() maps that refusal to the `dev-refused` state; this
// file never decides visibility itself. resetAllAllowed() is a second,
// view-side lock (the host gate is the enforcement): it opens only for the
// dev channel AND the typed phrase.
//
// The page-state union must stay in lockstep with the core's
// kManagerPageStates (identity/core/manager_page.h, served by the host's
// `manager-page-states`). xr-browser's tools/shield_state_check.py fails the
// build when a host state has no union member or no stateText arm.
//
// Attention: this page is silent. It raises nothing on its own; every
// confirmation is inline text the user types into the page itself
// (docs/state/attention-budget.md, Identity section; attention_check.py).
//
// Strings are IDS_XR_IDENTITIES_* ids (l10n/xr_strings.grdp); a page row
// never shows a guessed value: a permission count the overlay did not report
// renders as "not reported", never 0.

export const IDENTITIES_PAGE_STATES = [
  'normal',
  'empty',
  'purge-unverified',
  'dev-refused',
] as const;
export type IdentitiesPageState = (typeof IDENTITIES_PAGE_STATES)[number];

export const DEV_REFUSAL_PREFIX = 'build-channel-not-dev:';
export const RESET_ALL_PHRASE = 'reset-all';
const ROUTE_ROOT = 'xr://identities';
const DOMAIN_RE =
  /^xr:[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

export interface Msg {
  msg: string;
  params?: Record<string, string>;
}

export type Route =
  | { kind: 'list' }
  | { kind: 'detail'; domain: string }
  | { kind: 'reset-all' }
  | { kind: 'not-found' };

/** xr://identities, xr://identities/<domain>, xr://identities/reset-all.
 * Anything else is not-found (never a guessed nearest page). */
export function parseRoute(url: string): Route {
  if (!url.startsWith(ROUTE_ROOT)) return { kind: 'not-found' };
  const rest = url.slice(ROUTE_ROOT.length);
  if (rest === '' || rest === '/') return { kind: 'list' };
  if (!rest.startsWith('/')) return { kind: 'not-found' };
  const seg = rest.slice(1).replace(/\/$/, '');
  if (seg === RESET_ALL_PHRASE) return { kind: 'reset-all' };
  if (DOMAIN_RE.test(seg)) return { kind: 'detail', domain: seg };
  return { kind: 'not-found' };
}

export interface HostRow {
  domain?: string;
  display_name?: string;
  color?: string;
  glyph?: string;
  template_id?: string;
  in_memory?: boolean;
  lifecycle?: string;
  tab_count?: number;
  storage_bytes?: number;
  permission_count?: number | null;
}

export interface HostPurge {
  domain?: string;
  verified?: boolean;
  residual_kinds?: string[];
}

export interface PageModel {
  state: string; // a member of IDENTITIES_PAGE_STATES, or an honest unknown
  detail: string; // the refused channel / the host error, verbatim
  rows: HostRow[];
  purges: HostPurge[];
}

function isObject(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

/** The host reply (`manager-page` stdout, parsed) -> the page model. */
export function pageModel(reply: unknown): PageModel {
  const empty = { rows: [] as HostRow[], purges: [] as HostPurge[] };
  if (!isObject(reply)) return { state: 'unknown', detail: 'no-reply', ...empty };
  if (typeof reply.error === 'string') {
    const reason = typeof reply.reason === 'string' ? reply.reason : '';
    if (reply.error === 'kRejected' && reason.startsWith(DEV_REFUSAL_PREFIX)) {
      return { state: 'dev-refused', detail: reason.slice(DEV_REFUSAL_PREFIX.length), ...empty };
    }
    return { state: 'unknown', detail: reason || reply.error, ...empty };
  }
  return {
    state: typeof reply.state === 'string' ? reply.state : 'unknown',
    detail: '',
    rows: Array.isArray(reply.rows) ? (reply.rows as HostRow[]) : [],
    purges: Array.isArray(reply.purges) ? (reply.purges as HostPurge[]) : [],
  };
}

/** The state line. Every page state has an arm; anything else renders as an
 * honest unknown carrying the raw value (never guessed). */
export function stateText(model: PageModel): Msg {
  switch (model.state) {
    case 'normal':
      return { msg: 'IDS_XR_IDENTITIES_STATE_NORMAL' };
    case 'empty':
      return { msg: 'IDS_XR_IDENTITIES_STATE_EMPTY' };
    case 'purge-unverified':
      return { msg: 'IDS_XR_IDENTITIES_STATE_PURGE_UNVERIFIED' };
    case 'dev-refused':
      return { msg: 'IDS_XR_IDENTITIES_STATE_DEV_REFUSED', params: { CHANNEL: model.detail } };
    default:
      return {
        msg: 'IDS_XR_IDENTITIES_STATE_UNKNOWN',
        params: { STATE: model.detail ? `${model.state}: ${model.detail}` : model.state },
      };
  }
}

export function lifecycleText(lifecycle: string | undefined): Msg {
  switch (lifecycle) {
    case 'kActive':
      return { msg: 'IDS_XR_IDENTITIES_LIFECYCLE_ACTIVE' };
    case 'kHibernated':
      return { msg: 'IDS_XR_IDENTITIES_LIFECYCLE_ARCHIVED' };
    default:
      return { msg: 'IDS_XR_IDENTITIES_LIFECYCLE_UNKNOWN', params: { STATE: String(lifecycle) } };
  }
}

export interface RowView {
  domain: string;
  name: string;
  color: string;
  glyph: string;
  lifecycle: Msg;
  tabs: string;
  storage: string;
  permissions: Msg;
  inMemory: boolean;
  label: Msg;
}

/** One table row. Numbers ride as decimal strings (the l10n layer formats);
 * a missing number is "not reported", never 0. */
export function rowView(row: HostRow): RowView {
  const num = (v: unknown) => (typeof v === 'number' ? String(v) : '');
  const name = row.display_name || row.domain || '';
  const permissions: Msg =
    typeof row.permission_count === 'number'
      ? { msg: 'IDS_XR_IDENTITIES_PERMISSIONS_COUNT', params: { COUNT: String(row.permission_count) } }
      : { msg: 'IDS_XR_IDENTITIES_NOT_REPORTED' };
  return {
    domain: row.domain ?? '',
    name,
    color: row.color ?? '',
    glyph: row.glyph ?? '',
    lifecycle: lifecycleText(row.lifecycle),
    tabs: num(row.tab_count),
    storage: num(row.storage_bytes),
    permissions,
    inMemory: row.in_memory === true,
    label: {
      msg: 'IDS_XR_IDENTITIES_ROW_LABEL',
      params: { NAME: name, TABS: num(row.tab_count), BYTES: num(row.storage_bytes) },
    },
  };
}

/** The text a user types to confirm a purge: the identity's name (its domain
 * when unnamed). A purge is never one click. */
export function purgeConfirmToken(row: HostRow): string {
  return row.display_name || row.domain || '';
}

export function purgeAllowed(row: HostRow, typed: string): boolean {
  const token = purgeConfirmToken(row);
  return token !== '' && typed === token;
}

/** View-side lock on reset-all: dev channel AND the typed phrase. The host
 * gate is the enforcement; this lock only keeps the control inert. */
export function resetAllAllowed(channel: string, typed: string): boolean {
  return channel === 'dev' && typed === RESET_ALL_PHRASE;
}

/** Purge outcomes the page must show (an unverified purge is never hidden). */
export function purgeLines(model: PageModel): Msg[] {
  return model.purges
    .filter((p) => p.verified !== true)
    .map((p) => ({
      msg: 'IDS_XR_IDENTITIES_PURGE_UNVERIFIED_ROW',
      params: { DOMAIN: p.domain ?? '', KINDS: (p.residual_kinds ?? []).join(', ') },
    }));
}
