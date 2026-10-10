// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Identity chrome: the lit elements (P14-T4, P14-CLOSE C-1).
//
//   <xr-identity-tab-mark>       the per-tab mark: a 2 px color bar, or a dot
//                                in the rail layout, plus a glyph or initial,
//                                plus a label where the layout has room. The
//                                SR string is the tab's accessible
//                                description, announced on tab focus.
//   <xr-identity-pill>           the toolbar pill: swatch, glyph, name and
//                                route summary, as a button named by the
//                                state's accessible-name message.
//   <xr-identity-window-border>  the full-window frame: amber dashed for a
//                                disposable identity, solid in the identity's
//                                color for the Tor template.
//
// Every element renders ONLY what chrome-core.ts's structure() returns. No
// state or layout logic lives here, so the structural snapshots (and the test
// that holds the core to them) describe exactly what these elements draw.
// Strings are message ids resolved through ../i18n.js text(). Colors arrive
// as a custom property set through styleMap (CSSOM, so no inline style text
// and no CSP exception). Layout uses logical properties only (rtl_lint).
// Pixels are NOT-RUN (docs/qa/browser-harness.md#identity-chrome-visual).
import { css, html, LitElement, nothing } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { styleMap } from 'lit/directives/style-map.js';
import type { XrStringMap } from '../i18n.js';
import { text } from '../i18n.js';
import type { ChromeIdentity, ChromeTable, Msg, ThemeTokens } from './chrome-core.js';
import { ChromeRefusal, structure } from './chrome-core.js';

type Part = Record<string, unknown>;

function resolve(strings: XrStringMap, m: Msg): string {
  const params: Record<string, string> = {};
  for (const [k, v] of Object.entries(m.params ?? {})) {
    params[k] = typeof v === 'string' ? v : resolve(strings, v);
  }
  return text(strings, m.msg, params);
}

function edgeStyle(part: Part): Record<string, string> {
  const edge = part.edge as { color: string; px: number } | null;
  const out: Record<string, string> = { '--xr-idc-color': String(part.color) };
  if (edge) {
    out['--xr-idc-edge'] = `${edge.px}px solid ${edge.color}`;
  }
  return out;
}

class ChromeBase extends LitElement {
  @property({ type: Object }) table: ChromeTable | null = null;
  @property({ type: Object }) theme: ThemeTokens = {};
  @property({ type: Object }) identity: ChromeIdentity | null = null;
  @property({ type: String }) layout = 'top';
  @property({ type: Object }) strings: XrStringMap = {};

  protected model(): Part | null {
    if (!this.table || !this.identity) {
      return null;
    }
    try {
      return structure(this.table, this.theme, this.identity, this.layout);
    } catch (e) {
      // A typed refusal (unknown state/layout, theme without the token)
      // renders nothing rather than a wrong mark; anything else is a bug.
      if (e instanceof ChromeRefusal) {
        return null;
      }
      throw e;
    }
  }
}

@customElement('xr-identity-tab-mark')
export class XrIdentityTabMark extends ChromeBase {
  static override styles = css`
    :host { display: inline-flex; align-items: center; gap: 4px; }
    .bar[data-side='block-start'] {
      position: absolute; inset-inline: 0; inset-block-start: 0;
      block-size: 2px; background: var(--xr-idc-color);
      border-block-end: var(--xr-idc-edge, none);
    }
    .bar[data-side='inline-start'] {
      position: absolute; inset-block: 0; inset-inline-start: 0;
      inline-size: 2px; background: var(--xr-idc-color);
      border-inline-end: var(--xr-idc-edge, none);
    }
    .dot {
      inline-size: 8px; block-size: 8px; border-radius: 50%;
      background: var(--xr-idc-color); outline: var(--xr-idc-edge, none);
    }
  `;

  protected override render() {
    const m = this.model();
    if (!m) {
      return nothing;
    }
    const tab = m.tab as { description: Msg; children: Part[] };
    return html`
      <span class=${(m.class as string[]).join(' ')}
            aria-description=${resolve(this.strings, tab.description)}>
        ${tab.children.map((p) => {
          if (p.part === 'bar') {
            return html`<span class="bar" aria-hidden="true" data-side=${String(p.side)}
              style=${styleMap(edgeStyle(p))}></span>`;
          }
          if (p.part === 'dot') {
            return html`<span class="dot" aria-hidden="true"
              style=${styleMap(edgeStyle(p))}></span>`;
          }
          if (p.part === 'glyph') {
            return html`<span class="glyph" aria-hidden="true">${String(p.text)}</span>`;
          }
          return html`<span class="label">${String(p.text)}</span>`;
        })}
      </span>`;
  }
}

@customElement('xr-identity-pill')
export class XrIdentityPill extends ChromeBase {
  static override styles = css`
    button {
      display: inline-flex; align-items: center; gap: 6px;
      padding-block: 2px; padding-inline: 8px;
      border: 1px solid var(--xr-border, currentColor); border-radius: 999px;
      background: var(--xr-surface-raised, transparent);
      color: var(--xr-text, currentColor); font: inherit;
    }
    .swatch {
      inline-size: 10px; block-size: 10px; border-radius: 50%;
      background: var(--xr-idc-color);
    }
    button:focus-visible { outline: 2px solid var(--xr-focus-ring, currentColor); }
  `;

  private onOpen(): void {
    this.dispatchEvent(new CustomEvent('identity-pill-open', { bubbles: true, composed: true }));
  }

  protected override render() {
    const m = this.model();
    if (!m) {
      return nothing;
    }
    const pill = m.pill as { name: Msg; children: Part[] };
    return html`
      <button type="button" class=${(m.class as string[]).join(' ')}
              aria-label=${resolve(this.strings, pill.name)}
              @click=${() => this.onOpen()}>
        ${pill.children.map((p) => {
          if (p.part === 'swatch') {
            return html`<span class="swatch" aria-hidden="true"
              style=${styleMap({ '--xr-idc-color': String(p.color) })}></span>`;
          }
          if (p.part === 'glyph') {
            return html`<span class="glyph" aria-hidden="true">${String(p.text)}</span>`;
          }
          if (p.part === 'route') {
            return html`<span class="route">${text(this.strings, String(p.msg))}</span>`;
          }
          return html`<span class="name">${String(p.text)}</span>`;
        })}
      </button>`;
  }
}

@customElement('xr-identity-window-border')
export class XrIdentityWindowBorder extends ChromeBase {
  static override styles = css`
    :host { position: fixed; inset: 0; pointer-events: none; }
    .frame {
      position: absolute; inset: 0;
      border-width: var(--xr-idc-px, 2px); border-color: var(--xr-idc-color);
      outline: var(--xr-idc-edge, none); outline-offset: -4px;
    }
    .frame[data-style='dashed'] { border-style: dashed; }
    .frame[data-style='solid'] { border-style: solid; }
  `;

  protected override render() {
    const m = this.model();
    const b = m ? (m.window_border as Part | null) : null;
    if (!b) {
      return nothing;
    }
    return html`<div class="frame" aria-hidden="true" data-style=${String(b.style)}
      style=${styleMap({ ...edgeStyle(b), '--xr-idc-px': `${String(b.px)}px` })}></div>`;
  }
}
