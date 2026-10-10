// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The xr://identities dev page view (P14-T7, P14-CLOSE C-3). DEV BUILDS
// ONLY — enforced by the identity host's real `--build-channel` gate (the
// host refuses `manager-page` / `reset-all` on any other channel; the
// roster's identities.page rides build.channel-dev). This view never decides
// visibility: it renders the model identities-core.ts builds from what the
// host glue pushes (`reply` = identity_host manager-page stdout, parsed).
//
// Per identity: name, color + glyph + text (color never alone), lifecycle,
// open tabs, storage bytes, site permissions ("not reported" when the overlay
// sent nothing — never 0). Edits (rename / recolor / archive) and purge are
// requests the host glue performs; purge and reset-all need inline typed
// confirmation. The page is silent: it raises nothing outside itself
// (docs/state/attention-budget.md, Identity section).
//
// Strings: IDS_XR_IDENTITIES_* ids only (the raw-string lint holds).
import { html, LitElement, nothing } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { styleMap } from 'lit/directives/style-map.js';
import type { XrStringMap } from '../i18n.js';
import { text } from '../i18n.js';
import type { HostRow, Msg, PageModel, Route } from './identities-core.js';
import {
  pageModel,
  parseRoute,
  purgeAllowed,
  purgeLines,
  RESET_ALL_PHRASE,
  resetAllAllowed,
  rowView,
  stateText,
} from './identities-core.js';

export interface IdentitiesEditDetail {
  domain: string;
  op: 'rename' | 'recolor' | 'archive' | 'purge' | 'reset-all';
  value: string;
}

@customElement('xr-identities')
export class XrIdentities extends LitElement {
  @property({ type: Object }) reply: unknown = null;
  @property({ type: String }) url = 'xr://identities';
  @property({ type: String }) channel = 'release';
  @property({ type: Object }) strings: XrStringMap = {};
  @state() private typed: Record<string, string> = {};

  private t(m: Msg): string {
    return text(this.strings, m.msg, m.params);
  }

  private request(detail: IdentitiesEditDetail) {
    this.dispatchEvent(
      new CustomEvent<IdentitiesEditDetail>('xr-identities-request', {
        detail,
        bubbles: true,
        composed: true,
      }),
    );
  }

  private onTyped(key: string, e: Event) {
    this.typed = { ...this.typed, [key]: (e.target as HTMLInputElement).value };
  }

  protected override render() {
    const route: Route = parseRoute(this.url);
    const model: PageModel = pageModel(this.reply);
    const s = (id: string, p?: Record<string, string>) => text(this.strings, id, p);
    if (route.kind === 'not-found') {
      return html`<section class="xr-identities" aria-labelledby="xr-identities-heading">
        <h1 id="xr-identities-heading">${s('IDS_XR_IDENTITIES_HEADING')}</h1>
        <p role="status">${s('IDS_XR_IDENTITIES_NOT_FOUND')}</p>
      </section>`;
    }
    const rows =
      route.kind === 'detail'
        ? model.rows.filter((r) => r.domain === route.domain)
        : model.rows;
    return html`
      <section class="xr-identities" aria-labelledby="xr-identities-heading">
        <h1 id="xr-identities-heading">${s('IDS_XR_IDENTITIES_HEADING')}</h1>
        <p class="xr-identities-dev-only" role="note">${s('IDS_XR_IDENTITIES_DEV_ONLY')}</p>
        <p class="xr-identities-state" role="status" aria-live="polite">
          ${this.t(stateText(model))}
        </p>
        ${purgeLines(model).map(
          (m) => html`<p class="xr-identities-purge-unverified" role="note">${this.t(m)}</p>`,
        )}
        ${model.state === 'dev-refused' || route.kind === 'reset-all'
          ? nothing
          : this.renderTable(rows)}
        ${route.kind === 'reset-all' && model.state !== 'dev-refused'
          ? this.renderResetAll()
          : nothing}
      </section>
    `;
  }

  private renderTable(rows: HostRow[]) {
    const s = (id: string, p?: Record<string, string>) => text(this.strings, id, p);
    if (rows.length === 0) return nothing;
    return html`
      <table class="xr-identities-table">
        <thead>
          <tr>
            <th scope="col">${s('IDS_XR_IDENTITIES_COL_NAME')}</th>
            <th scope="col">${s('IDS_XR_IDENTITIES_COL_LIFECYCLE')}</th>
            <th scope="col">${s('IDS_XR_IDENTITIES_COL_TABS')}</th>
            <th scope="col">${s('IDS_XR_IDENTITIES_COL_STORAGE')}</th>
            <th scope="col">${s('IDS_XR_IDENTITIES_COL_PERMISSIONS')}</th>
            <th scope="col">${s('IDS_XR_IDENTITIES_COL_ACTIONS')}</th>
          </tr>
        </thead>
        <tbody>
          ${rows.map((row) => this.renderRow(row))}
        </tbody>
      </table>
    `;
  }

  private renderRow(row: HostRow) {
    const s = (id: string, p?: Record<string, string>) => text(this.strings, id, p);
    const v = rowView(row);
    const key = `purge:${v.domain}`;
    const typed = this.typed[key] ?? '';
    return html`
      <tr class="xr-identities-row" aria-label=${this.t(v.label)}>
        <th scope="row">
          <span class="xr-identities-mark" aria-hidden="true" style=${styleMap({ '--xr-id-color': v.color })}
            >${v.glyph}</span
          >
          ${v.name}
          ${v.inMemory ? html`<span class="xr-identities-memory">${s('IDS_XR_IDENTITIES_IN_MEMORY')}</span>` : nothing}
        </th>
        <td>${this.t(v.lifecycle)}</td>
        <td>${v.tabs}</td>
        <td>${v.storage}</td>
        <td>${this.t(v.permissions)}</td>
        <td>
          <button type="button" @click=${() => this.request({ domain: v.domain, op: 'rename', value: this.typed[`name:${v.domain}`] ?? '' })}>
            ${s('IDS_XR_IDENTITIES_RENAME')}
          </button>
          <input type="text" aria-label=${s('IDS_XR_IDENTITIES_RENAME')} @input=${(e: Event) => this.onTyped(`name:${v.domain}`, e)} />
          <input type="color" aria-label=${s('IDS_XR_IDENTITIES_RECOLOR')} .value=${v.color} @change=${(e: Event) => this.request({ domain: v.domain, op: 'recolor', value: (e.target as HTMLInputElement).value })} />
          <button type="button" @click=${() => this.request({ domain: v.domain, op: 'archive', value: '' })}>
            ${s('IDS_XR_IDENTITIES_ARCHIVE')}
          </button>
          <label>
            ${s('IDS_XR_IDENTITIES_PURGE_CONFIRM', { NAME: v.name })}
            <input type="text" @input=${(e: Event) => this.onTyped(key, e)} />
          </label>
          <button type="button" ?disabled=${!purgeAllowed(row, typed)} @click=${() => this.request({ domain: v.domain, op: 'purge', value: typed })}>
            ${s('IDS_XR_IDENTITIES_PURGE')}
          </button>
        </td>
      </tr>
    `;
  }

  private renderResetAll() {
    const s = (id: string, p?: Record<string, string>) => text(this.strings, id, p);
    const typed = this.typed['reset-all'] ?? '';
    return html`
      <form class="xr-identities-reset-all" @submit=${(e: Event) => e.preventDefault()}>
        <label>
          ${s('IDS_XR_IDENTITIES_RESET_ALL_CONFIRM', { PHRASE: RESET_ALL_PHRASE })}
          <input type="text" @input=${(e: Event) => this.onTyped('reset-all', e)} />
        </label>
        <button type="button" ?disabled=${!resetAllAllowed(this.channel, typed)} @click=${() => this.request({ domain: '', op: 'reset-all', value: typed })}>
          ${s('IDS_XR_IDENTITIES_RESET_ALL')}
        </button>
      </form>
    `;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'xr-identities': XrIdentities;
  }
}
