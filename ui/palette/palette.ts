// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The command palette (view #1 of four-views-one-registry). A THIN presentational
// Lit component: ALL ranking / availability / dispatch logic lives in
// commands_host (C++). This view receives the host's ranked `query` results as
// a property (the host-protocol data contract) and renders them as an ARIA APG
// combobox with listbox popup. No business logic, no network, no `any`.
//
// Keyboard model (a11y pass v0, Plan UX req): Tab moves in/out, ArrowUp/Down
// moves the highlight (announced via aria-activedescendant), Enter executes the
// highlighted command, Esc closes. The highlight is the *only* thing that
// changes on arrow keys — the input keeps focus (the APG pattern).
import { html, LitElement, nothing, type PropertyValues } from 'lit';
import { customElement, property, query, state } from 'lit/decorators.js';

/** A ranked command as returned by the host `query` method. */
export interface PaletteResult {
  id: string;
  title: string;
  kind: number;
  score: number;
  available: boolean;
  reason: string;
}

/** The typed dispatch request the palette forwards on Enter (source=palette). */
export interface InvokeRequest {
  id: string;
  source: 'palette';
  confirmed: boolean;
}

@customElement('xr-palette')
export class XrPalette extends LitElement {
  /** Ranked results from the host (empty array when the flag is off). */
  @property({ type: Array }) results: PaletteResult[] = [];

  /** True when the host reports the registry flag is off (stock chrome). */
  @property({ type: Boolean }) flagOff: boolean = false;

  @state() private _query = '';
  @state() private _active = -1;

  @query('input') private _input?: HTMLInputElement;
  @query('[role="listbox"]') private _listbox?: HTMLUListElement;

  /** Called by the host glue when the user executes a command. */
  onInvoke: ((req: InvokeRequest) => void) | null = null;

  private get _visible(): PaletteResult[] {
    if (this._query.trim() === '') return this.results;
    return this.results.slice(0, 8);
  }

  private _focusActive(): void {
    // Move the DOM focus to the highlighted option so keyboard + SR align.
    const el = this._listbox?.children[this._active] as HTMLElement | undefined;
    if (el) el.focus();
  }

  private _onInput(e: Event): void {
    this._query = (e.target as HTMLInputElement).value;
    this._active = this._visible.length > 0 ? 0 : -1;
    this.requestUpdate();
  }

  private _move(delta: number): void {
    const n = this._visible.length;
    if (n === 0) return;
    this._active = (this._active + delta + n) % n;
    this._focusActive();
  }

  private _select(): void {
    const r = this._visible[this._active];
    if (r && r.available && this.onInvoke) {
      this.onInvoke({ id: r.id, source: 'palette', confirmed: false });
    }
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
      case 'Tab': break; // natural tab out; the APG pattern keeps SR state
      default: break;
    }
  }

  protected override firstUpdated(_changed: PropertyValues): void {
    this._input?.setAttribute('autocomplete', 'off');
  }

  protected override render() {
    if (this.flagOff) {
      // Stock chrome (flag off): the view renders NOTHING (Plan rollback row).
      return html`<span class="xr-empty" aria-live="polite">
        Command registry is off (stock chrome).
      </span>`;
    }
    const items = this._visible;
    const listId = 'xr-palette-listbox';
    const activeId = this._active >= 0 ? `xr-palette-opt-${this._active}` : '';
    return html`
      <label class="xr-palette-label" for="xr-palette-input">Command</label>
      <input
        id="xr-palette-input"
        class="xr-palette-input"
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
      <ul id=${listId} role="listbox" class="xr-palette-list" aria-label="Commands">
        ${items.map((r, i) => html`
          <li
            id=${`xr-palette-opt-${i}`}
            role="option"
            tabindex="-1"
            aria-selected=${i === this._active ? 'true' : 'false'}
            data-danger=${r.available ? 'safe' : 'caution'}
            class="xr-row"
            ?disabled=${!r.available}
            @keydown=${this._onKeydown}>
            <span class="xr-palette-title">${r.title}</span>
            ${r.available ? nothing : html`<span class="xr-palette-reason">${r.reason}</span>`}
          </li>
        `)}
      </ul>
      ${items.length === 0 ? html`<div class="xr-empty" role="status" aria-live="polite">
        No commands match “${this._query}”.
      </div>` : nothing}
    `;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'xr-palette': XrPalette;
  }
}
