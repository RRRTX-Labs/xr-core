// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// settings-section — renders ONE settings section from host-protocol data
// (settings_host `sections` + per-key `get` shapes). THIN: no availability
// computation (the host already resolved it against the pinned policy
// snapshot), no write path in v0 (policy-owned rows render their source and
// are never toggleable here). Deep link anchors are emitted as jump events.
import { html, LitElement, nothing } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import type { SettingsSectionData, XrStringMap } from './settings-shell.js';

/** A single setting row as settings_host `get` returns it. */
export interface SettingsRowData {
  key: string;
  value: unknown;
  source: 'default' | 'resolver' | 'enterprise' | string;
  preempted: boolean;
  writable: boolean;
  section: string;
  attention_tier: string;
  scope: string;
  title_id?: string;
  desc_id?: string;
}

@customElement('xr-settings-section')
export class XrSettingsSection extends LitElement {
  @property({ type: Object }) section: SettingsSectionData | null = null;

  @property({ type: Array }) rows: SettingsRowData[] = [];

  @property({ type: Object }) strings: XrStringMap = {};

  private _emitJump(anchor: string, key: string): void {
    this.dispatchEvent(
      new CustomEvent('jump', {
        detail: { anchor, key },
        bubbles: true,
        composed: true,
      }),
    );
  }

  protected override render() {
    const s = this.section;
    if (!s) return nothing;
    const title = this.strings[s.title_id] ?? s.title_id;
    return html`
      <section class="xr-settings-section" data-section=${s.id}>
        <h2 class="xr-settings-section-title">
          <a
            href=${s.anchor}
            class="xr-anchor"
            @click=${(e: Event) => {
              e.preventDefault();
              this._emitJump(s.anchor, s.id);
            }}>${title}</a>
          <code class="xr-section-id">${s.id}</code>
        </h2>
        ${!s.available && s.unavailable_reason
          ? html`<p class="xr-unavailable" role="note">
              ${this.strings['settings.section-unavailable'] ??
              'section unavailable'} — ${s.unavailable_reason}</p>`
          : nothing}
        <ul class="xr-setting-list">
          ${this.rows.map((r) => this._row(r))}
        </ul>
      </section>
    `;
  }

  private _row(r: SettingsRowData) {
    const anchor = `xr://settings/${r.section}/${r.key.split('.').pop() ?? ''}`;
    const title = (r.title_id && this.strings[r.title_id]) || r.key;
    return html`<li class="xr-setting-row" data-key=${r.key}>
      <a
        href=${anchor}
        class="xr-anchor"
        @click=${(e: Event) => {
          e.preventDefault();
          this._emitJump(anchor, r.key);
        }}>${title}</a>
      <code class="xr-setting-value">${JSON.stringify(r.value)}</code>
      <span class="xr-setting-meta">${r.source}</span>
      ${r.preempted
        ? html`<span class="xr-setting-badge" role="note">preempted</span>`
        : nothing}
      ${!r.writable
        ? html`<span class="xr-setting-badge" role="note">read-only in v0</span>`
        : nothing}
    </li>`;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'xr-settings-section': XrSettingsSection;
  }
}
