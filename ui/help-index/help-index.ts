// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The help index + printable cheatsheet (view #4 of four-views-one-registry).
// THIN presentational Lit: it renders the FULL roster as a printable artifact
// generated from the registry. "No coming-soon rails" (§10) is enforced here —
// a placeholder command (e.g. the Tor session, disabled via its availability
// predicate) renders as `disabled` WITH its reason, never a "coming soon"
// surface. No business logic, no network, no `any`.
import { html, LitElement, nothing } from 'lit';
import { customElement, property } from 'lit/decorators.js';

/** A roster command as rendered in the help index (menu-model item shape). */
export interface HelpItem {
  id: string;
  title: string;
  group: string;
  tier: 'tier0' | 'tier1' | 'tier2';
  danger_class: 'safe' | 'caution' | 'destructive';
  available: boolean;
  reason: string;
}

@customElement('xr-help-index')
export class XrHelpIndex extends LitElement {
  @property({ type: Array }) commands: HelpItem[] = [];

  private get _groups(): Map<string, HelpItem[]> {
    const m = new Map<string, HelpItem[]>();
    for (const c of this.commands) {
      const g = m.get(c.group) ?? [];
      g.push(c);
      m.set(c.group, g);
    }
    return m;
  }

  protected override render() {
    const groups = this._groups;
    return html`
      <h2>Help — Command Index</h2>
      <p class="xr-help-intro">Every command, every group. Disabled commands are
        shown with the reason — nothing here is a "coming soon" placeholder.</p>
      ${[...groups.entries()].map(([group, items]) => html`
        <section class="xr-help-group">
          <h3>${group}</h3>
          <table class="xr-cheatsheet">
            <thead>
              <tr><th>Command</th><th>Tier</th><th>Class</th><th>Status</th></tr>
            </thead>
            <tbody>
              ${items.map((c) => html`
                <tr class="xr-row" data-danger=${c.danger_class}
                    ?disabled=${!c.available}>
                  <td class="xr-help-title">${c.title}
                    <code class="xr-help-id">${c.id}</code></td>
                  <td>${c.tier}</td>
                  <td>${c.danger_class}</td>
                  <td>${c.available
                    ? 'enabled'
                    : html`<span class="xr-help-disabled" role="note">
                        disabled — ${c.reason}</span>`}</td>
                </tr>`)}
            </tbody>
          </table>
        </section>`)}
      ${this.commands.length === 0
        ? html`<div class="xr-empty" role="status" aria-live="polite">
            No commands (registry flag off).</div>`
        : nothing}
    `;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'xr-help-index': XrHelpIndex;
  }
}
