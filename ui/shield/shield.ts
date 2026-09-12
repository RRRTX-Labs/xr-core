// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The xr://shield debug page view (P11-T6): DEV BUILDS ONLY — enforced by
// the shield host's real `--build-channel` startup gate (its `debug-page`
// method refuses typed on any other channel; this view never decides
// visibility itself). It renders exactly what the host glue pushes:
//   * engine status + binding + the vendored engine pin (never claiming
//     the Rust engine runs — `rust_bound` states the truth),
//   * bundle rows: active / last-known-good / pinned slots,
//   * last-apply result + duration + timestamp (caller-supplied — no
//     clock in this product),
//   * refused-directive counts (never hidden),
//   * memory of our core structures (canonical bytes, ring, scopes),
//   * per-list sources + attribution VERBATIM,
//   * the enterprise force-disable row with the policy reason VERBATIM
//     (no-silent-suppression law), and
//   * the chip count — the ONLY passive security counter in the product.
// Attention Budget law (docs/state/attention-budget.md §Shield):
// the chip count is the ONLY passive attention surface this view may
// raise — tools/attention_check.py (xr-browser) machine-enforces the
// banned-vocabulary rule on this file. Amber states (engine dead/poisoned, kill switch) are rendered
// distinctly from "blocked nothing" (the state line names the posture).
//
// Strings: every user-visible string is an IDS_XR_SHIELD_* id from
// l10n/xr_strings.grdp (the raw-string lint holds: no literals below).
// Tokens: colors/sizes come from the P8 token pipeline (design tokens
// only; no raw colors here). Host glue (P16/farm) pushes `page` (the
// shield_host debug-page payload); this view renders exactly what was
// sent — no logic, no network, no fetch.
import { html, LitElement, nothing } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import type { XrStringMap } from '../i18n.js';
import { text } from '../i18n.js';

/** The page-state union — MUST stay in lockstep with the C++ host's
 * kPageStates list (shield_host.cc, served by `page-states`).
 * tools/shield_state_check.py fails the build if this union drifts from
 * the host's enumerated states (the about_state_check pattern). */
export const SHIELD_PAGE_STATES = [
  'normal',
  'engine-dead',
  'engine-poisoned',
  'kill-switch',
  'route-loss',
] as const;
export type ShieldPageState = (typeof SHIELD_PAGE_STATES)[number];

export interface ShieldSlot {
  present?: boolean;
  bundle_id?: string;
  digest?: string;
  version?: number;
}

export interface ShieldBundles {
  active?: ShieldSlot;
  lkg?: ShieldSlot;
  pins?: ShieldSlot[];
  last_apply_mono?: number;
}

export interface ShieldLastApply {
  ts_millis?: number;
  result?: string;
  duration_ms?: number;
}

export interface ShieldListRow {
  name?: string;
  attribution?: string;
  rules?: number;
}

export interface ShieldRefusalRow {
  directive?: string;
  reason?: string;
  count?: number;
}

export interface ShieldPage {
  page_state?: ShieldPageState;
  channel?: string;
  chip_count?: number;
  posture?: { mode?: string; chip?: string; reason?: string };
  engine?: {
    alive?: boolean;
    poisoned?: boolean;
    binding?: string;
    vendored_pin?: string;
    rust_bound?: boolean;
  };
  bundles?: ShieldBundles | null;
  last_apply?: ShieldLastApply | null;
  refused_directives?: ShieldRefusalRow[];
  lists?: ShieldListRow[];
  memory?: {
    bundle_canonical_bytes?: number;
    ring_events?: number;
    scope_count?: number;
  };
  enterprise?: { force_disabled?: boolean; reason?: string };
}

@customElement('xr-shield')
export class XrShield extends LitElement {
  @property({ type: Object }) page: ShieldPage = {};
  @property({ type: Object }) strings: XrStringMap = {};

  protected override render() {
    const s = (id: string, params?: Record<string, string>) =>
      text(this.strings, id, params);
    const p = this.page;
    return html`
      <section class="xr-shield" aria-labelledby="xr-shield-heading">
        <h1 id="xr-shield-heading">${s('IDS_XR_SHIELD_HEADING')}</h1>
        <p class="xr-shield-dev-only" role="note">
          ${s('IDS_XR_SHIELD_DEV_ONLY')}
        </p>
        <p class="xr-shield-state" role="status" aria-live="polite">
          ${this.stateText(s)}
        </p>
        ${p.enterprise?.force_disabled
          ? html`<p class="xr-shield-forced" role="note">
              ${s('IDS_XR_SHIELD_FORCED_DISABLED', {
                REASON: p.enterprise.reason ?? '',
              })}
            </p>`
          : nothing}
        <dl class="xr-shield-rows">
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_CHIP_COUNT_LABEL')}</dt>
            <dd>
              ${s('IDS_XR_SHIELD_CHIP_COUNT', {
                COUNT: String(p.chip_count ?? 0),
              })}
            </dd>
          </div>
        </dl>

        <h2>${s('IDS_XR_SHIELD_ENGINE_HEADING')}</h2>
        <dl class="xr-shield-rows">
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_ENGINE_ALIVE')}</dt>
            <dd>${p.engine?.alive ? '✓' : '—'}</dd>
          </div>
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_ENGINE_POISONED')}</dt>
            <dd>${p.engine?.poisoned ? '✓' : '—'}</dd>
          </div>
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_ENGINE_BINDING')}</dt>
            <dd>${p.engine?.binding ?? '—'}</dd>
          </div>
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_ENGINE_VENDORED')}</dt>
            <dd>${p.engine?.vendored_pin ?? '—'}</dd>
          </div>
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_ENGINE_RUST_BOUND')}</dt>
            <dd>${p.engine?.rust_bound ? '✓' : '—'}</dd>
          </div>
        </dl>

        <h2>${s('IDS_XR_SHIELD_BUNDLES_HEADING')}</h2>
        <dl class="xr-shield-rows">
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_BUNDLE_ACTIVE')}</dt>
            <dd>${this.slotText(s, p.bundles?.active)}</dd>
          </div>
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_BUNDLE_LKG')}</dt>
            <dd>${this.slotText(s, p.bundles?.lkg)}</dd>
          </div>
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_BUNDLE_PINS')}</dt>
            <dd>
              ${(p.bundles?.pins ?? []).map(
                (pin) => html`${this.slotText(s, pin)}<br />`,
              )}
              ${(p.bundles?.pins ?? []).length === 0 ? '—' : nothing}
            </dd>
          </div>
        </dl>

        <h2>${s('IDS_XR_SHIELD_APPLY_HEADING')}</h2>
        <p class="xr-shield-apply">
          ${p.last_apply
            ? s('IDS_XR_SHIELD_APPLY_ROW', {
                RESULT: p.last_apply.result ?? '',
                DURATION: String(p.last_apply.duration_ms ?? 0),
                TS: String(p.last_apply.ts_millis ?? 0),
              })
            : s('IDS_XR_SHIELD_APPLY_NONE')}
        </p>

        <h2>${s('IDS_XR_SHIELD_REFUSED_HEADING')}</h2>
        ${(p.refused_directives ?? []).length === 0
          ? html`<p class="xr-shield-refused-empty">
              ${s('IDS_XR_SHIELD_REFUSED_EMPTY')}
            </p>`
          : html`<ul class="xr-shield-refused">
              ${(p.refused_directives ?? []).map(
                (r) => html`<li>
                  ${s('IDS_XR_SHIELD_REFUSED_ROW', {
                    DIRECTIVE: r.directive ?? '',
                    COUNT: String(r.count ?? 0),
                    REASON: r.reason ?? '',
                  })}
                </li>`,
              )}
            </ul>`}

        <h2>${s('IDS_XR_SHIELD_LISTS_HEADING')}</h2>
        ${(p.lists ?? []).length === 0
          ? html`<p class="xr-shield-lists-empty">
              ${s('IDS_XR_SHIELD_LISTS_EMPTY')}
            </p>`
          : html`<table class="xr-shield-lists">
              <thead>
                <tr>
                  <th scope="col">${s('IDS_XR_SHIELD_LIST_NAME')}</th>
                  <th scope="col">${s('IDS_XR_SHIELD_LIST_RULES')}</th>
                  <th scope="col">${s('IDS_XR_SHIELD_LIST_ATTRIBUTION')}</th>
                </tr>
              </thead>
              <tbody>
                ${(p.lists ?? []).map(
                  (l) => html`<tr>
                    <td>${l.name ?? '—'}</td>
                    <td>${String(l.rules ?? 0)}</td>
                    <td>${l.attribution ?? '—'}</td>
                  </tr>`,
                )}
              </tbody>
            </table>`}

        <h2>${s('IDS_XR_SHIELD_MEMORY_HEADING')}</h2>
        <dl class="xr-shield-rows">
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_MEMORY_BYTES')}</dt>
            <dd>${String(p.memory?.bundle_canonical_bytes ?? 0)}</dd>
          </div>
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_MEMORY_RING')}</dt>
            <dd>${String(p.memory?.ring_events ?? 0)}</dd>
          </div>
          <div class="xr-shield-row">
            <dt>${s('IDS_XR_SHIELD_MEMORY_SCOPES')}</dt>
            <dd>${String(p.memory?.scope_count ?? 0)}</dd>
          </div>
        </dl>
      </section>
    `;
  }

  private stateText(
    s: (id: string, p?: Record<string, string>) => string,
  ): string {
    switch (this.page.page_state) {
      case 'normal':
        return s('IDS_XR_SHIELD_STATE_NORMAL');
      case 'engine-dead':
        return s('IDS_XR_SHIELD_STATE_ENGINE_DEAD');
      case 'engine-poisoned':
        return s('IDS_XR_SHIELD_STATE_ENGINE_POISONED');
      case 'kill-switch':
        return s('IDS_XR_SHIELD_STATE_KILL_SWITCH');
      case 'route-loss':
        return s('IDS_XR_SHIELD_STATE_ROUTE_LOSS');
      default:
        // an unknown state from the host renders honestly, never guessed
        // (the about.ts law)
        return this.page.page_state ?? '';
    }
  }

  private slotText(
    s: (id: string, p?: Record<string, string>) => string,
    slot?: ShieldSlot,
  ): string {
    if (!slot || !slot.present) return '—';
    return s('IDS_XR_SHIELD_SLOT_VALUE', {
      NAME: slot.bundle_id ?? '',
      VERSION: String(slot.version ?? 0),
      DIGEST: slot.digest ?? '',
    });
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'xr-shield': XrShield;
  }
}
