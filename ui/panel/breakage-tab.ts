// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// xr-breakage-tab — the breakage report tab's PURE CORE (P13-T4).
//
// The report leaves the machine, so the tab's job is to show the user WHAT
// WOULD LEAVE and nothing else. This core owns the two halves a renderer must
// not invent for itself:
//
//   1. THE ROW SET IS CLOSED. `reportRows()` derives its rows from a
//      ReportContext — origin (scheme + registrable domain), a UA-LESS version
//      tag, the matched rule/list ids, the bundle version, the action — and from
//      nothing else. There is no parameter that can carry page content, a
//      cookie, a full URL or a user agent: the shape has no room for them, which
//      is stronger than a rule that says they are not allowed. The refusal
//      classes that a real filing path must still enforce are named in
//      FORBIDDEN_CONTEXT so the tab can say WHY a piece of context is refused
//      rather than silently dropping it (the export tool on the xr-browser side
//      is where the refusal is mechanical; this is where it is legible).
//
//   2. THE CONFIRMATION IS NON-ATTENTIONAL. The attention budget says an
//      action a user initiated gets no modal, no badge, no toast and no nag —
//      the plan's P12 rule forbids notification vocabulary for cosmetic, and a
//      breakage confirmation is the same shape: the user is already looking at
//      the tab. `confirmShape()` returns an inline, one-click shape, and
//      `assertConfirmable()` THROWS on notification vocabulary, so a renderer
//      cannot add a modal by passing one. The modal negative belongs to the
//      suite; the throw is what makes it possible.
//
// The queue is labelled `fixture` in its path and in the tool's stdout (the
// xr-browser half). There is no live mode in this repository: filing for real
// is HUMAN-GATED (docs/panel/breakage-report.md), and no median is computed
// anywhere — `slaRow()` renders label + hours + class as DATA.
//
// No DOM, no I/O, no clock (the caller supplies `as_of`). The rendered half is
// NOT-RUN (method: docs/qa/browser-harness.md).

export interface ReportOrigin {
  scheme: 'http' | 'https';
  registrable_domain: string;
}

/** Exactly the context a report may carry. There is nowhere here for anything else. */
export interface ReportContext {
  origin: ReportOrigin;
  /** UA-LESS: 'xr-1.2.0.0', never a user-agent string. */
  browser_version_tag: string;
  rule_id: string;
  list_id: string;
  bundle_version: number;
  action: 'kBlocked' | 'kAllowed' | 'kRedirected' | 'kUpgraded';
}

export interface SlaData {
  label: string;
  hours: number;
  class: string;
}

/** Context a report may NOT carry, named so the tab can refuse it out loud. */
export const FORBIDDEN_CONTEXT = [
  'page-content',
  'cookie',
  'full-url',
  'query-string',
  'fragment',
  'user-agent',
  'selector',
  'credentials',
] as const;

export const ATTENTION_VOCABULARY = ['modal', 'dialog', 'badge', 'toast',
                                     'notification', 'nag', 'banner'] as const;

/** The tab's rows: context only, plus the queue label and the SLA as data. */
export function reportRows(ctx: ReportContext, sla: SlaData): Array<{ key: string; value: string }> {
  return [
    { key: 'origin.scheme', value: ctx.origin.scheme },
    { key: 'origin.registrable_domain', value: ctx.origin.registrable_domain },
    { key: 'browser_version_tag', value: ctx.browser_version_tag },
    { key: 'rule_id', value: ctx.rule_id },
    { key: 'list_id', value: ctx.list_id },
    { key: 'bundle_version', value: String(ctx.bundle_version) },
    { key: 'action', value: ctx.action },
    { key: 'queue', value: 'fixture' },
    { key: 'sla.label', value: sla.label },
    { key: 'sla.hours', value: String(sla.hours) },
    { key: 'sla.class', value: sla.class },
  ];
}

/**
 * The confirmation's shape: INLINE and one click. `attention` is a value the
 * core chooses, not a parameter — a caller cannot ask for a modal, because the
 * function does not take a preference.
 */
export function confirmShape(): {
  surface: 'inline';
  clicks: 1;
  attention: 'inline';
  forgets: false;
} {
  return { surface: 'inline', clicks: 1, attention: 'inline', forgets: false };
}

/** Throws on any notification vocabulary. The modal negative drives this. */
export function assertConfirmable(asked: { attention?: string }): { attention: 'inline' } {
  const want = asked.attention ?? 'inline';
  if ((ATTENTION_VOCABULARY as readonly string[]).includes(want)) {
    throw new Error(`attention-budget:${want}`);
  }
  if (want !== 'inline') {
    throw new Error(`unknown-attention:${want}`);
  }
  return { attention: 'inline' };
}

/** A refused piece of context is NAMED, never dropped in silence. */
export function refusalFor(field: string): { code: string } | null {
  return (FORBIDDEN_CONTEXT as readonly string[]).includes(field)
    ? { code: 'refused-pre-send:' + field }
    : null;
}
