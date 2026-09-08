// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The shortcut editor (view #3 of four-views-one-registry). THIN
// presentational Lit: the conflict matrix + persist (kill-durable store) live in
// commands_host / commands/core/shortcuts.cc (C++). This view renders the
// current bindings and, CRITICALLY, the conflict payload BEFORE a bind is
// committed — the conflicting command is named (UX req: "conflict shown before
// binding with the conflicting command named"). No business logic, no network,
// no `any`.
import { html, LitElement, nothing } from 'lit';
import { customElement, property } from 'lit/decorators.js';

/** A committed binding (host `bindings-list` row). */
export interface Binding {
  command_id: string;
  accelerator: string;
}

/** The conflict payload (host `bindings-set` reject). Shown BEFORE binding. */
export interface ConflictPayload {
  conflict: 'duplicate' | 'browser-reserved' | 'system-reserved';
  command_id: string;
  accelerator: string;
  conflicting_command?: string; // named for the UX (duplicate case)
  error: string;
}

@customElement('xr-shortcut-editor')
export class XrShortcutEditor extends LitElement {
  @property({ type: Array }) bindings: Binding[] = [];
  @property({ type: Object }) conflict: ConflictPayload | null = null;

  private _conflictText(): string {
    const c = this.conflict;
    if (!c) return '';
    switch (c.conflict) {
      case 'duplicate':
        return `“${c.accelerator}” is already bound to ` +
          `${c.conflicting_command ?? 'another command'}. Choose a different ` +
          `accelerator or unbind it first.`;
      case 'browser-reserved':
        return `“${c.accelerator}” is reserved by the browser (denied by ` +
          `default).`;
      case 'system-reserved':
        return `“${c.accelerator}” is reserved by the OS (denied by default).`;
    }
  }

  protected override render() {
    return html`
      <h2>Shortcuts</h2>
      <ul class="xr-bindings" aria-label="Current shortcuts">
        ${this.bindings.length === 0
          ? html`<li class="xr-empty" role="status" aria-live="polite">
              No shortcuts bound yet.</li>`
          : this.bindings.map((b) => html`
              <li class="xr-row">
                <code class="xr-acc">${b.accelerator}</code>
                <span class="xr-cmd">${b.command_id}</span>
              </li>`)}
      </ul>
      ${this.conflict
        ? html`<div class="xr-conflict" role="alert" aria-live="assertive"
                 data-conflict=${this.conflict.conflict}>
            <strong>Cannot bind:</strong> ${this._conflictText()}
          </div>`
        : nothing}
    `;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'xr-shortcut-editor': XrShortcutEditor;
  }
}
