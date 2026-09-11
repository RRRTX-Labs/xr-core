// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The ABOUT view (P10-T8): version, Chromium base + pinned rev, build hash,
// signature status (including "not signed: dev channel" VERBATIM), rollback
// availability, and the update-check state machine
// idle/checking/available/downloading/ready/failed/refused — with the
// no-silent-failures law: a failed/refused state renders the TYPED reason,
// the manual-download pointer and a "check again" affordance. Never a
// spinner-forever, never a hidden error.
//
// Strings: every user-visible string is an IDS_XR_ABOUT_* id from
// l10n/xr_strings.grdp (the raw-string lint holds: no literals below).
// Tokens: colors/sizes come from the P8 token pipeline (design tokens only;
// no raw colors here). Host glue (P16/farm) pushes `state` (the
// update_host about-state payload) and `meta` (version/build rows); this
// view renders exactly what was sent — no logic, no network, no fetch.
import { html, LitElement, nothing } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import type { XrStringMap } from '../i18n.js';
import { text } from '../i18n.js';

/** The update state machine — MUST stay in lockstep with the C++ host's
 * about-states list (update_host.cc). tools/about_state_check.py fails the
 * build if this union drifts from the host's enumerated states. */
export const ABOUT_STATES = [
  'idle',
  'checking',
  'available',
  'downloading',
  'ready',
  'failed',
  'refused',
] as const;
export type AboutState = (typeof ABOUT_STATES)[number];

export interface AboutUpdateState {
  state: AboutState;
  reason?: string;
  manual_path?: boolean;
  manual_download?: { path?: string };
  check_again?: boolean;
  version?: string;
}

export interface AboutMeta {
  version?: string;
  chromium_rev?: string;
  build_hash?: string;
  signature_status?: string; // e.g. "ok" | "unknown-signing-key" |
                             // "not signed: dev channel" (VERBATIM id below)
  rollback_available?: boolean;
  channel?: string;
}

@customElement('xr-about')
export class XrAbout extends LitElement {
  @property({ type: Object }) meta: AboutMeta = {};
  @property({ type: Object }) state: AboutUpdateState = { state: 'idle' };
  @property({ type: Object }) strings: XrStringMap = {};

  protected override render() {
    const s = (id: string, params?: Record<string, string>) =>
      text(this.strings, id, params);
    const sigId =
      this.meta.signature_status === 'not signed: dev channel'
        ? 'IDS_XR_ABOUT_NOT_SIGNED_DEV_CHANNEL'
        : this.meta.signature_status ?? '';
    const failed = this.state.state === 'failed';
    const refused = this.state.state === 'refused';
    const showHelp = failed || refused; // no silent failures
    return html`
      <section class="xr-about" aria-labelledby="xr-about-heading">
        <h1 id="xr-about-heading">${s('IDS_XR_ABOUT_HEADING')}</h1>
        <dl class="xr-about-rows">
          <div class="xr-about-row">
            <dt>${s('IDS_XR_ABOUT_VERSION')}</dt>
            <dd>${this.meta.version ?? '—'}</dd>
          </div>
          <div class="xr-about-row">
            <dt>
              ${s('IDS_XR_ABOUT_CHROMIUM_BASE', {
                CHROMIUM_REV: this.meta.chromium_rev ?? '—',
              })}
            </dt>
            <dd>${this.meta.chromium_rev ?? '—'}</dd>
          </div>
          <div class="xr-about-row">
            <dt>${s('IDS_XR_ABOUT_BUILD_HASH')}</dt>
            <dd>${this.meta.build_hash ?? '—'}</dd>
          </div>
          <div class="xr-about-row">
            <dt>${s('IDS_XR_ABOUT_SIGNATURE_STATUS')}</dt>
            <dd>${sigId ? s(sigId) : '—'}</dd>
          </div>
          <div class="xr-about-row">
            <dt>${s('IDS_XR_ABOUT_ROLLBACK')}</dt>
            <dd>
              ${this.meta.rollback_available
                ? '✓'
                : '—'}
            </dd>
          </div>
        </dl>

        <h2>${s('IDS_XR_ABOUT_UPDATE_HEADING')}</h2>
        <p class="xr-about-state" role="status" aria-live="polite">
          ${this.stateText(s)}
        </p>
        ${this.state.state === 'idle'
          ? html`<button
              class="xr-about-check"
              @click=${this.onCheckNow}
              ?disabled=${false}
            >
              ${s('IDS_XR_ABOUT_CHECK_NOW')}
            </button>`
          : nothing}
        ${showHelp
          ? html`
              <p class="xr-about-manual" role="note">
                ${s('IDS_XR_ABOUT_MANUAL_DOWNLOAD')}
              </p>
              ${this.state.check_again === false
                ? nothing
                : html`<button
                    class="xr-about-check-again"
                    @click=${this.onCheckNow}
                  >
                    ${s('IDS_XR_ABOUT_CHECK_AGAIN')}
                  </button>`}
            `
          : nothing}
      </section>
    `;
  }

  private stateText(s: (id: string, p?: Record<string, string>) => string) {
    switch (this.state.state) {
      case 'idle':
        return s('IDS_XR_ABOUT_STATE_IDLE');
      case 'checking':
        return s('IDS_XR_ABOUT_STATE_CHECKING');
      case 'available':
        return s('IDS_XR_ABOUT_STATE_AVAILABLE');
      case 'downloading':
        return s('IDS_XR_ABOUT_STATE_DOWNLOADING');
      case 'ready':
        return s('IDS_XR_ABOUT_STATE_READY');
      case 'failed':
        return s('IDS_XR_ABOUT_STATE_FAILED',
                 { REASON: this.state.reason ?? '' });
      case 'refused':
        return s('IDS_XR_ABOUT_STATE_REFUSED',
                 { REASON: this.state.reason ?? '' });
      default:
        // an unknown state from the host renders honestly, never guessed
        return this.state.state;
    }
  }

  private onCheckNow() {
    this.dispatchEvent(new CustomEvent('xr-about-check-now', {
      bubbles: true,
      composed: true,
    }));
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'xr-about': XrAbout;
  }
}
