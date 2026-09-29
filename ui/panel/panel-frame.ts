// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// xr-panel-frame — the accountability panel's frame (P13-T1).
//
// One panel, right-docked, 360 px, keyboard-first. This view owns the FRAME
// only: the tab strip, the tab bodies and every data surface arrive later
// (T2/T3/T4/T5/T6) through slots, so this file must never grow feature logic.
//
// What the frame is responsible for, all of it testable:
//   * geometry — 360 px, docked to the inline END of the window (logical
//     properties, so RTL docks to the left with no second code path;
//     tools/rtl_lint.py enforces the logical-only rule);
//   * modality — role="dialog" + aria-modal="true" + aria-labelledby, so a
//     screen reader is told the rest of the page is inert while it is open;
//   * containment — every Tab/Shift+Tab is decided by focus-trap.ts (the
//     planted-leak negative lives there, not in a comment here);
//   * focus handoff — opening records the opener and focuses the frame;
//     closing restores the opener (the element the user's keyboard was on);
//     Escape closes (keyboard-first: no pointer required for any of it);
//   * honesty — it renders exactly the strings it is given and nothing else.
//     The host glue (P16/farm) pushes `strings` (from l10n/xr_strings.grdp)
//     and `open`; this view does no I/O, no fetch, no clock.
//
// Strings: every user-visible string is an IDS_XR_PANEL_* id (the raw-string
// lint holds: no literals below). Tokens: the frame's own sizing token is
// --xr-panel-inline-size from the P8 token pipeline.
import { css, html, LitElement, nothing } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import type { XrStringMap } from '../i18n.js';
import { text } from '../i18n.js';
import type { FocusTarget, FocusWorld } from './focus-trap.js';
import { trapKeydown } from './focus-trap.js';

/** The panel's states, for docs/webui-e2e.md and the host glue's tests. */
export const PANEL_STATES = ['closed', 'open'] as const;
export type PanelState = (typeof PANEL_STATES)[number];

/** Selector for "focusable" — kept next to the adapter that uses it so the two
 * cannot drift: the trap's law (not disabled, not hidden, tabindex >= 0) is
 * expressed here in DOM terms. */
const FOCUSABLE = [
  'a[href]',
  'button:not([disabled])',
  'input:not([disabled])',
  'select:not([disabled])',
  'textarea:not([disabled])',
  '[tabindex]',
].join(',');

let nextFrameId = 0;

@customElement('xr-panel-frame')
export class XrPanelFrame extends LitElement {
  /** Light DOM: the frame is part of the page's own tab order, so
   * document.activeElement is the honest answer for both the trap and the
   * focus-restore path (a shadow root would report the host instead). */
  override createRenderRoot(): this {
    return this;
  }

  /** Open/closed. The host owns the state; this view only renders it. */
  @property({ type: Boolean, reflect: true }) open = false;
  /** IDS_XR_PANEL_* message map from the host glue. */
  @property({ type: Object }) strings: XrStringMap = {};

  /** True once a tab body is slotted; drives the empty state (a live region:
   * an empty panel must SAY it is empty, never render as a silent void). */
  @state() private hasBody = false;

  private readonly frameId = `xr-panel-frame-${(nextFrameId += 1)}`;
  private opener: HTMLElement | null = null;
  private world: FocusWorld | null = null;

  /** The DOM adapter the trap core talks to. Exposed for the panel's tests. */
  focusWorld(): FocusWorld {
    if (this.world === null) {
      this.world = {
        active: () => (document.activeElement as HTMLElement | null)?.id ?? null,
        focus: (id: string) => {
          const el = document.getElementById(id);
          if (el && typeof el.focus === 'function') el.focus();
        },
        inside: (root: string) => this._targets(this._root(root)),
        all: () => this._targets(document.body),
      };
    }
    return this.world;
  }

  private _root(id: string): HTMLElement | null {
    return document.getElementById(id);
  }

  private _targets(scope: HTMLElement | null): FocusTarget[] {
    if (scope === null) return [];
    return Array.from(scope.querySelectorAll<HTMLElement>(FOCUSABLE)).map(
      (el) => ({
        id: el.id,
        tabindex: el.tabIndex,
        disabled: el.hasAttribute('disabled'),
        hidden: el.hidden || el.getAttribute('aria-hidden') === 'true',
      }),
    );
  }

  protected override updated(changed: Map<string, unknown>): void {
    if (!changed.has('open')) return;
    if (this.open) this._opened();
    else this._closed();
  }

  private _opened(): void {
    const active = document.activeElement as HTMLElement | null;
    // The opener is whatever the keyboard was on BEFORE the panel took focus.
    // Captured once per open, never overwritten by our own focus moves.
    if (this.opener === null && active !== null && active !== this) {
      this.opener = active;
    }
    const frame = this._root(this.frameId);
    if (frame !== null && typeof frame.focus === 'function') frame.focus();
    this.dispatchEvent(new CustomEvent('panel-open', { bubbles: true }));
  }

  private _closed(): void {
    const opener = this.opener;
    this.opener = null;
    if (opener !== null && typeof opener.focus === 'function') opener.focus();
    this.dispatchEvent(new CustomEvent('panel-close', { bubbles: true }));
  }

  /** Keyboard-first: the frame itself decides containment and dismissal. */
  private _onKeydown(e: KeyboardEvent): void {
    if (!this.open) return;
    const action = trapKeydown(
      this.focusWorld(),
      this.frameId,
      e.key,
      e.shiftKey,
    );
    if (action.kind === 'ignore') return;
    e.preventDefault();
    if (action.kind === 'focus') {
      this.focusWorld().focus(action.id);
      return;
    }
    // close: the host owns `open`; we ask, and restore focus if it agrees.
    const close = new CustomEvent('panel-close-request', {
      bubbles: true,
      cancelable: true,
    });
    if (this.dispatchEvent(close)) this.open = false;
  }

  private _onSlotChange(e: Event): void {
    const slot = e.target as HTMLSlotElement;
    const name = slot.getAttribute('name') ?? '';
    if (name === 'body') {
      this.hasBody = slot.assignedElements({ flatten: true }).length > 0;
    }
    if (name === 'tabs') {
      // A tab that arrives while open must be reachable immediately; re-focus
      // the frame so the next Tab enters the new tab strip, not the page.
      if (this.open) this._root(this.frameId)?.focus();
    }
  }

  override render() {
    if (!this.open) return nothing;
    return html`
      <aside
        id=${this.frameId}
        class="frame"
        role="dialog"
        aria-modal="true"
        aria-labelledby="${this.frameId}-title"
        tabindex="-1"
        @keydown=${this._onKeydown}
      >
        <header class="head">
          <h2 id="${this.frameId}-title" class="title">
            ${text(this.strings, 'IDS_XR_PANEL_HEADING')}
          </h2>
          <button
            type="button"
            class="close"
            id="${this.frameId}-close"
            aria-label=${text(this.strings, 'IDS_XR_PANEL_CLOSE')}
            @click=${() => {
              this.open = false;
            }}
          >
            ${text(this.strings, 'IDS_XR_PANEL_CLOSE')}
          </button>
        </header>
        <nav class="tabs" aria-label=${text(this.strings, 'IDS_XR_PANEL_TABS')}>
          <slot name="tabs" @slotchange=${this._onSlotChange}></slot>
        </nav>
        <div class="body">
          <slot name="body" @slotchange=${this._onSlotChange}></slot>
          ${this.hasBody
            ? nothing
            : html`<p class="empty" aria-live="polite">
                ${text(this.strings, 'IDS_XR_PANEL_EMPTY')}
              </p>`}
        </div>
        <footer class="foot">
          <slot name="footer"></slot>
        </footer>
      </aside>
    `;
  }

  static styles = css`
    :host { --xr-panel-inline-size: 360px; display: block; }
    .frame {
      inline-size: var(--xr-panel-inline-size);
      block-size: 100%;
      position: fixed;
      inset-block: 0;
      inset-inline-end: 0;
      display: flex;
      flex-direction: column;
      background: var(--xr-surface, Canvas);
      color: var(--xr-text, CanvasText);
      border-inline-start: 1px solid var(--xr-border, GrayText);
      box-sizing: border-box;
      padding-block: var(--xr-space-2, 8px);
    }
    .head { display: flex; align-items: center; justify-content: space-between; }
    .tabs { display: flex; gap: var(--xr-space-1, 4px); }
    .body { flex: 1 1 auto; overflow: auto; }
    .frame:focus-visible, .close:focus-visible {
      outline: 2px solid var(--xr-focus, Highlight);
      outline-offset: 2px;
    }
  `;

}
declare global {
  interface HTMLElementTagNameMap {
    'xr-panel-frame': XrPanelFrame;
  }
}
