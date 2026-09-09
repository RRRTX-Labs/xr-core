// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The settings shell (P8-T1 "search-first settings"): search field + section
// list + deep-link jumping. THIN presentational Lit: all ranking, registry,
// availability and anchor resolution live in settings_host (C++); the shell
// renders host-protocol data (sections, search results) it is given and
// forwards jumps as events. No business logic, no network, no `any`.
//
// Typed states (never fake data): flagOff renders the flag-off note;
// no host data => the host-unavailable note (a spinner is never shown).
import { html, LitElement, nothing } from 'lit';
import { customElement, property, query, state } from 'lit/decorators.js';
import './settings-search.js';
import './settings-section.js';
import { text } from '../i18n.js';

export { XrSettingsSearch } from './settings-search.js';
export { XrSettingsSection } from './settings-section.js';

/** A settings search result exactly as settings_host `search` returns it. */
export interface SettingsSearchResult {
  key: string;
  kind: 'section' | 'setting';
  score: number;
}

/** A section exactly as settings_host `sections` returns it. */
export interface SettingsSectionData {
  id: string;
  title_id: string;
  anchor: string;
  available: boolean;
  availability: string;
  unavailable_reason?: string;
  settings: string[];
}

/** Title-id -> text map supplied by the host glue (P16; ids until then). */
export type XrStringMap = Record<string, string>;

/** Jump request emitted when the user activates a result / section. */
export interface SettingsJump {
  anchor: string;
  key: string;
}

@customElement('xr-settings-shell')
export class XrSettingsShell extends LitElement {
  /** Flag xr_settings_v0 off => nothing renders (rollback row). */
  @property({ type: Boolean }) flagOff: boolean = false;

  /** Sections from the host (empty = host not yet present or empty registry). */
  @property({ type: Array }) sections: SettingsSectionData[] = [];

  /** Host search results for the current query (ranked by the C++ core). */
  @property({ type: Array }) searchResults: SettingsSearchResult[] = [];

  /** Title/desc message-id -> text map (host glue; raw ids until then). */
  @property({ type: Object }) strings: XrStringMap = {};

  @state() private _query = '';
  @state() private _jumpTarget: SettingsJump | null = null;

  @query('xr-settings-search') private _search?: HTMLElement;

  /** Emitted when the user picks a result / section (host glue navigates). */
  onJump: ((jump: SettingsJump) => void) | null = null;

  private get _sectionList(): SettingsSectionData[] {
    if (this._query.trim() !== '') return [];
    return this.sections;
  }

  private _onSearchInput(e: Event): void {
    this._query = (e.target as HTMLInputElement).value;
    this._jumpTarget = null;
  }

  private _onJump(e: Event): void {
    const jump = (e as CustomEvent<SettingsJump>).detail;
    if (!jump) return;
    this._jumpTarget = jump;
    // Focus returns to the search field after a jump (a11y law).
    this._search?.focus();
    if (this.onJump) this.onJump(jump);
  }

  protected override render() {
    if (this.flagOff) {
      return html`<div class="xr-empty" role="status" aria-live="polite">
        ${text(this.strings, 'settings.flag-off')}
      </div>`;
    }
    const jumped = this._jumpTarget;
    return html`
      <section class="xr-settings">
        <h1 class="xr-settings-title">${this._text('settings.heading')}</h1>
        <xr-settings-search
          .results=${this.searchResults}
          .strings=${this.strings}
          @query-input=${this._onSearchInput}
          @jump=${this._onJump}></xr-settings-search>
        ${jumped
          ? html`<div class="xr-empty" role="status" aria-live="polite">
              ${text(this.strings, 'settings.jumped-to',
                {KEY: jumped.key, ANCHOR: jumped.anchor})}</div>`
          : nothing}
        ${this._sectionList.length === 0 && this.searchResults.length === 0
          ? html`<div class="xr-empty" role="status" aria-live="polite">
              ${this.sections.length === 0
                ? this._text('settings.host-unavailable')
                : this._text('settings.no-sections')}
            </div>`
          : nothing}
        ${this._sectionList.map(
          (s) =>
            html`<xr-settings-section
              .section=${s}
              .strings=${this.strings}
              @jump=${this._onJump}></xr-settings-section>`,
        )}
      </section>
    `;
  }

  private _text(id: string): string {
    return text(this.strings, id);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'xr-settings-shell': XrSettingsShell;
  }
}
