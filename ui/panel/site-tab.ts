// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// xr-site-tab — the Site tab's PURE CORE (P13-T2).
//
// The tab answers three questions about the page you are on, and nothing else:
//
//   1. WHAT is this connection?  — TLS/cert basics, as data. The core renders
//      what it is given and INVENTS NOTHING: a certificate detail that is not in
//      the input does not appear in the output, so "no cert row" is visibly
//      different from "cert row with a made-up value".
//   2. WHAT have I changed for this site? — the trust dial and the per-site
//      exception rows, derived from P11's ONE scope set. The core holds NO
//      STATE: it is a pure function of the scope array it is handed, and every
//      control returns a REQUEST (`exception-add` / `exception-remove` /
//      `site-toggle`) rather than a mutation. That is what "reuse the single
//      scope object, never a second store" means when it is a property of the
//      code instead of a promise in a comment.
//   3. WHY was something blocked? — the drill: a blocked count opens the rows
//      behind it, each row naming its rule/list/source through the reason-code
//      table (docs/shield/reason-codes.json). Every code in the table renders,
//      and an unknown code is a TYPED FALLBACK that says so — never a blank row,
//      never a guess.
//
// The shields-down bit is exactly ONE thing (the canonical scope_id
// `site-toggle:<site>`) that the shield core and the cosmetic layer both read
// (P11-T5). This file does not get to have its own opinion about it: the dial
// reads the scope set, and toggles by asking.
//
// No DOM, no I/O, no clock, no fetch — the shell owns all of that, which is why
// this half is testable in a sandbox and the rendered half is NOT-RUN
// (method: docs/qa/browser-harness.md).

/** The 8 canonical scope keys (xr-core fakes/shield.py SCOPE_KEYS). */
export interface Scope {
  expiry_mono: number;
  identity: string;
  list_id: string;
  reason: string;
  rule_id: string;
  scope_id: string;
  site: string;
  workspace: string;
}

export interface CertBasics {
  subject: string;
  issuer: string;
  valid_from: string;
  valid_to: string;
  protocol: string;
  /** True when the chain verified. Never inferred from the dates. */
  verified: boolean;
}

export interface IsolationRow {
  measure: string;
  value: string;
  source: string;
}

export interface TrustDial {
  state: 'standard' | 'shields-down';
  scope_id: string;
  /** Every control that changes it must go through this one method. */
  by: 'site-toggle';
  /** Who else reads the bit — the P11 law, restated where the UI can cite it. */
  read_by: string[];
}

/** The canonical per-site shields-down scope id (P11-T5). */
export function siteToggleScopeId(site: string): string {
  return `site-toggle:${site}`;
}

/** The scope set, filtered to one site. Order is by scope_id: deterministic. */
export function siteScopes(scopes: Scope[], site: string): Scope[] {
  return scopes
    .filter((s) => s.site === site)
    .slice()
    .sort((a, b) => (a.scope_id < b.scope_id ? -1 : a.scope_id > b.scope_id ? 1 : 0));
}

/** Is shields-down set for this site? Reads the scope set — nothing else. */
export function shieldsDown(scopes: Scope[], site: string): boolean {
  const id = siteToggleScopeId(site);
  return scopes.some((s) => s.scope_id === id);
}

/** The dial, as data. `read_by` names the two consumers, so the UI can say it. */
export function trustDial(scopes: Scope[], site: string): TrustDial {
  return {
    state: shieldsDown(scopes, site) ? 'shields-down' : 'standard',
    scope_id: siteToggleScopeId(site),
    by: 'site-toggle',
    read_by: ['xr-core/shield', 'xr-core/cosmetic'],
  };
}

/**
 * The request a control produces. A REQUEST, not a mutation: the shell sends it
 * to the shield and re-renders from the scope set that comes back, which is why
 * two views of the same site cannot disagree.
 */
export function toggleRequest(site: string, on: boolean): { method: string; args: Record<string, unknown> } {
  return {
    method: 'site-toggle',
    args: { site, scope_id: siteToggleScopeId(site), on },
  };
}

/** An add/remove request for a scope the site tab itself can express. */
export function exceptionRequest(
  site: string,
  scopeText: string,
  op: 'exception-add' | 'exception-remove',
  nowMono: number,
  identity: string,
): { method: string; args: Record<string, unknown> } {
  return {
    method: op,
    // The scope is the P11 wire form, verbatim: 8 keys, no extras, and
    // expiry_mono comes from the caller (no clock in this file).
    args: { scope: {
      expiry_mono: nowMono,
      identity,
      list_id: '',
      reason: 'site-tab',
      rule_id: '',
      scope_id: scopeText,
      site,
      workspace: '',
    } },
  };
}

/** The per-site exception rows, one per scope, with the origin named. */
export function exceptionRows(scopes: Scope[], site: string): Array<Record<string, string>> {
  return siteScopes(scopes, site).map((s) => ({
    scope_id: s.scope_id,
    rule_id: s.rule_id,
    list_id: s.list_id,
    reason: s.reason,
    expires_at_mono: String(s.expiry_mono),
    remove: 'exception-remove',
  }));
}

/** Certificate basics as rows — verbatim from the input, or one honest row. */
export function certRows(cert: CertBasics | null): Array<Record<string, string>> {
  if (cert === null) {
    return [{ subject: '', issuer: '', note: 'no-certificate-observed' }];
  }
  return [{
    subject: cert.subject,
    issuer: cert.issuer,
    valid_from: cert.valid_from,
    valid_to: cert.valid_to,
    protocol: cert.protocol,
    verified: cert.verified ? 'chain-verified' : 'chain-unverified',
  }];
}

/** The isolation card is DATA (P4's measured table), row for row. */
export function isolationCardRows(card: IsolationRow[]): Array<Record<string, string>> {
  return card.map((r) => ({ measure: r.measure, value: r.value, source: r.source }));
}

/** Permissions summary: what this site asked for, and where the grant lives. */
export function permissionsSummary(items: Array<{ permission: string; state: string }>): {
  items: Array<{ permission: string; state: string }>;
  grants_live_in: string;
} {
  return { items: items.slice().sort((a, b) => (a.permission < b.permission ? -1 : 1)),
           grants_live_in: 'P15-permissions' };
}

export interface WhyRow {
  why_code: string;
  text_key: string;
  rule_id: string;
  list_id: string;
  count: number;
  rendered: 'table' | 'typed-fallback';
}

/**
 * The drill: blocked counts → the rows behind them.
 *
 * Every code the table carries renders its `text_key`. A code the table does NOT
 * carry is still shown — as a typed fallback that says the code is unknown —
 * because a dropped row is indistinguishable from "nothing was blocked", and
 * that is the one thing a count must never be able to claim.
 */
export function blockedDrill(
  counts: Array<{ why_code: string; rule_id?: string; list_id?: string; count: number }>,
  whyTable: Array<{ why_code: string; text_key: string }>,
): WhyRow[] {
  const known = new Map(whyTable.map((e) => [e.why_code, e.text_key]));
  return counts.map((c) => {
    const text_key = known.get(c.why_code);
    return {
      why_code: c.why_code,
      text_key: text_key ?? `shield.why.unknown.${c.why_code}`,
      rule_id: c.rule_id ?? '',
      list_id: c.list_id ?? '',
      count: c.count,
      rendered: text_key === undefined ? 'typed-fallback' : 'table',
    };
  });
}

/**
 * The always-on generic hide set (P12-T6's 33-rule `xr_shield_cosmetic_v1` set),
 * as rows that say what they are: NOT exception-able, and WHY.
 *
 * This exists because "not exception-able" is a property of the SET, not a
 * disabled checkbox. A UI that renders a greyed-out switch has said nothing: the
 * user cannot tell "this is off" from "this cannot be turned off". So the row
 * carries `exceptionable: false` AND `reason_key`, and the tab has no other way
 * to express it — there is no per-rule toggle to forget to disable, because the
 * core never emits a remove action for a member of this set.
 */
export function genericHideSetRows(set: {
  count: number;
  rule_ids: string[];
  reason_key: string;
}): Array<Record<string, string | boolean>> {
  return set.rule_ids.map((rule_id) => ({
    rule_id,
    set_size: String(set.count),
    exceptionable: false,
    reason_key: set.reason_key,
    // Explicitly absent: `remove` is the field exceptionRows() emits. Its
    // ABSENCE is what makes this row non-exception-able in the rendered shape.
    remove: '',
  }));
}

/**
 * The scriptlet surface's rows (P13-T2 / P12-T6's dev page, extended).
 *
 * The always-on generic set is not exception-able; the scriptlet registry is
 * present but INERT. Both facts are rendered as rows with machine tokens, so the
 * tab says what is true ("registry present, execution INERT (flag off)") instead
 * of implying a capability the build does not have. `installation_state` is
 * passed in — this core does not read a flag file, a pref or a clock, and a row
 * whose state is unknown renders as `unknown` rather than as the comfortable
 * answer (least privilege + fail safe: an unknown flag state is never "off").
 */
export function scriptletRows(registry: { rules: number } | null,
                              flag_state: 'off' | 'on' | 'unknown'): Array<Record<string, string>> {
  return [
    { row: 'scriptlet_registry', state: registry === null ? 'absent' : 'present',
      detail: registry === null ? 'no-registry-file' : `rules:${registry.rules}` },
    { row: 'scriptlet_execution',
      state: flag_state === 'on' ? 'ACTIVE' : flag_state === 'off' ? 'INERT' : 'unknown',
      detail: flag_state === 'on' ? 'flag:on' : flag_state === 'off' ? 'flag:off'
                                                                    : 'state-not-observed' },
  ];
}

/**
 * What the isolation card says when P4's measurement is NOT-RUN for a row.
 *
 * The card is data-driven (P4's measured table), and a row with no measurement
 * must SAY SO. "No cell" reads as "fine", which is the one thing an isolation
 * claim may never do: it is the difference between "we measured it and it holds"
 * and "nobody measured this".
 *
 * The result is MACHINE TOKENS, never a sentence: `state` is one of
 * `not-run` / `holds` / `violated`, and the method rides as a PATH the renderer
 * turns into localized copy. l10n_extract's R4 rule forbids space-bearing
 * literals under ui/** (they are indistinguishable from user-visible copy at
 * rest), and the honest fix is to keep the core token-shaped rather than to
 * allowlist a sentence — a rule relaxed for a good reason is relaxed for the
 * next one too. The suite asserts no returned value contains a space.
 */
export function isolationRowLabel(row: { prop: string; measured?: boolean; holds?: boolean }):
  { prop: string; state: 'not-run' | 'holds' | 'violated'; method: string } {
  if (row.measured !== true) {
    return { prop: row.prop, state: 'not-run', method: 'docs/qa/browser-harness.md' };
  }
  return { prop: row.prop, state: row.holds === true ? 'holds' : 'violated',
           method: 'ui/isolation-matrix' };
}
