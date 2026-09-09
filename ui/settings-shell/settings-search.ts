// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// settings-search — the search-first entry point (P8-T1). An ARIA APG
// combobox with listbox popup over settings_host `search` RESULTS (ranked by
// the C++ core — never re-ranked here). Keyboard: ArrowUp/Down highlight
// (aria-activedescendant), Enter jumps (emits a composed `jump` event with
// the key + derived anchor), Escape clears, Tab exits. Empty states are live
// regions; search-result counts are announced via aria-live.
import { html, LitElement, nothing, type PropertyValues } from 'lit';
import { customElement, property, query, state } from 'lit/decorators.js';
import type {
  SettingsSearchResult,
  XrStringMap,
} from './settings-shell.js';

@customElement('xr-settings-search')
export class XrSettingsSearch extends LitElement {
  /** Ranked results straight from settings_host `search`. */
  @property({ type: Array }) results: SettingsSearchResult[] = [];

  @property({ type: Object }) strings: XrStringMap = {};

  @state() private _query = '';
  @state() private _active = -1;

  @query('input') private _input?: HTMLInputElement;
  @query('[role="listbox"]') private _listbox?: HTMLUListElement;

  private get _visible(): SettingsSearchResult[] {
    return this._query.trim() === '' ? [] : this.results.slice(0, 8);
  }

  private _focusActive(): void {
    const el = this._listbox?.children[this._active] as HTMLElement | undefined;
    if (el) el.focus();
  }

  private _onInput(e: Event): void {
    this._query = (e.target as HTMLInputElement).value;
    this._active = this._visible.length > 0 ? 0 : -1;
    this.dispatchEvent(
      new CustomEvent('query-input', { bubbles: true, composed: true }),
    );
  }

  private _move(delta: number): void {
    const n = this._visible.length;
    if (n === 0) return;
    this._active = (this._active + delta + n) % n;
    this._focusActive();
  }

  private _select(): void {
    const r = this._visible[this._active];
    if (!r) return;
    const [section] = r.key.split('.');
    const suffix = r.kind === 'section' ? section : r.key.split('.').slice(1).join('/');
    const anchor = `xr://settings/${r.kind === 'section' ? section : `${section}/${suffix}`}`;
    this.dispatchEvent(
      new CustomEvent('jump', {
        detail: { key: r.key, anchor },
        bubbles: true,
        composed: true,
      }),
    );
  }

  private _close(): void {
    this._active = -1;
    this._input?.focus();
  }

  private _onKeydown(e: KeyboardEvent): void {
    switch (e.key) {
      case 'ArrowDown': e.preventDefault(); this._move(1); break;
      case 'ArrowUp': e.preventDefault(); this._move(-1); break;
      case 'Enter': e.preventDefault(); this._select(); break;
      case 'Escape': e.preventDefault(); this._close(); break;
      default: break;
    }
  }

  protected override firstUpdated(_changed: PropertyValues): void {
    this._input?.setAttribute('autocomplete', 'off');
  }

  private _text(id: string): string {
    return this.strings[id] ?? id;
  }

  protected override render() {
    const items = this._visible;
    const listId = 'xr-settings-search-listbox';
    const activeId = this._active >= 0 ? `xr-settings-opt-${this._active}` : '';
    return html`
      <label class="xr-settings-label" for="xr-settings-search-input">
        ${this._text('settings.search-label')}</label>
      <input
        id="xr-settings-search-input"
        class="xr-settings-input"
        type="text"
        role="combobox"
        .value=${this._query}
        aria-expanded=${items.length > 0 ? 'true' : 'false'}
        aria-controls=${listId}
        aria-activedescendant=${activeId || nothing}
        aria-autocomplete="list"
        aria-haspopup="listbox"
        autocomplete="off"
        @input=${this._onInput}
        @keydown=${this._onKeydown} />
      <ul id=${listId} role="listbox" class="xr-settings-list"
          aria-label=${this._text('settings.search-results')}>
        ${items.map((r, i) => html`
          <li id=${`xr-settings-opt-${i}`} role="option" tabindex="-1"
              aria-selected=${i === this._active ? 'true' : 'false'}>
            <span class="xr-settings-result">${r.key}</span>
            <span class="xr-settings-kind">${r.kind}</span>
          </li>`)}
      </ul>
      ${this._query.trim() !== ''
        ? html`<div class="xr-empty" role="status" aria-live="polite">
            ${items.length === 0
              ? this._text('settings.no-results')
              : `${items.length} ${this._text('settings.results-count')}`}
          </div>`
        : nothing}
    `;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'xr-settings-search': XrSettingsSearch;
  }
}
