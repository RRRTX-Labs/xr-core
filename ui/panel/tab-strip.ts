// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// xr-panel-tab-strip — the panel's tablist (P13-C-P0.1b, the DOM half of the
// §10 unit change).
//
// Why this is a separate element and not three lines inside panel-frame.ts:
// the frame's own header says it "must never grow feature logic", and a tab
// strip that reads a registry is feature logic. The frame slots this in.
//
// What it guarantees, all of it testable on the farm (the a11y snapshot half
// runs in-sandbox — docs/qa/axtree-snapshot.json gains these nodes; the real
// AXTree comes from CDP on the rig, HG-31):
//   * the strip renders role="tablist" with a name, and one role="tab" per
//     REGISTERED tab — never per declared tab: a declared-but-unavailable tab
//     must not appear as a tab a user can click;
//   * each tab's accessible name comes from its `title_msgid` through the
//     host's string map, so a missing message renders as the msgid and the
//     l10n lane notices, rather than as an empty tab nobody sees;
//   * selection is aria-selected + roving tabindex (the APG tabs pattern):
//     exactly one tab is in the page tab order at a time, and the arrow keys
//     move within the strip — which is why the frame's focus trap and this
//     strip agree about what "inside the panel" means;
//   * it renders NO text of its own: every visible string is an IDS_XR_* id
//     pushed by the host glue (l10n_extract's raw-string lint must find nothing
//     here, and it will say so if this comment becomes a lie).
//
// No I/O, no clock, no fetch: the host owns the registry and the strings.
import { css, html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import type { XrStringMap } from '../i18n.js';
import { text } from '../i18n.js';
import type { TabRegistration } from './tab-registry.js';

export const TAB_STRIP_ROLES = ['tablist', 'tab'] as const;

@customElement('xr-panel-tab-strip')
export class XrPanelTabStrip extends LitElement {
  static override styles = css`
    :host {
      display: block;
      inline-size: 100%;
    }
    [role='tablist'] {
      display: flex;
      gap: var(--xr-panel-tab-gap, 2px);
      border-block-end: 1px solid var(--xr-border, currentColor);
    }
    [role='tab'] {
      /* Logical properties only (tools/rtl_lint.py enforces it): the strip
         mirrors with no second code path. */
      padding-block: 6px;
      padding-inline: 10px;
      background: none;
      border: 0;
      border-block-end: 2px solid transparent;
      color: inherit;
      cursor: pointer;
      font: inherit;
    }
    [role='tab'][aria-selected='true'] {
      border-block-end-color: var(--xr-accent, currentColor);
    }
    [role='tab']:focus-visible {
      outline: 2px solid var(--xr-accent, currentColor);
      outline-offset: -2px;
    }
  `;

  /** Registered tabs, in render order (the registry's answer, not the
   * inventory's). The host pushes these; nothing here sorts or filters. */
  @property({ type: Array }) tabs: TabRegistration[] = [];
  /** IDS_XR_* string map (panel.tab.<id> per tab, panel.tabs for the name). */
  @property({ type: Object }) strings: XrStringMap = {};
  /** The tab currently shown. Empty means "no tab body is up yet". */
  @property({ type: String }) selected = '';

  private onSelect(id: string): void {
    this.dispatchEvent(new CustomEvent('panel-tab-select', {
      detail: { id },
      bubbles: true,
      composed: true,
    }));
  }

  private onKeydown(event: KeyboardEvent, index: number): void {
    const keys: Record<string, number> = {
      ArrowRight: 1, ArrowLeft: -1, Home: 0, End: 0,
    };
    const step = keys[event.key];
    if (step === undefined || this.tabs.length === 0) {
      return;
    }
    const last = this.tabs.length - 1;
    const next = event.key === 'Home' ? 0
      : event.key === 'End' ? last
        : (index + step + this.tabs.length) % this.tabs.length;
    event.preventDefault();
    this.onSelect(this.tabs[next].id);
    const root = this.renderRoot as DocumentFragment;
    root.querySelectorAll<HTMLElement>('[role="tab"]')[next]?.focus();
  }

  protected override render() {
    return html`
      <div role="tablist" aria-label=${this.strings['panel.tabs'] ?? 'panel.tabs'}>
        ${this.tabs.map((tab, index) => html`
          <button
            id="xr-panel-tab-${tab.id}"
            role="tab"
            type="button"
            aria-selected=${tab.id === this.selected ? 'true' : 'false'}
            aria-controls="xr-panel-tabpanel-${tab.id}"
            tabindex=${tab.id === this.selected
              || (this.selected === '' && index === 0) ? '0' : '-1'}
            @click=${() => this.onSelect(tab.id)}
            @keydown=${(e: KeyboardEvent) => this.onKeydown(e, index)}
          >${text(this.strings, tab.title_msgid)}</button>
        `)}
      </div>
    `;
  }
}
