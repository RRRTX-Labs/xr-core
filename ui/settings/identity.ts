// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The IDENTITY settings section view (P8-T1). Thin per-section wrapper over
// <xr-settings-section> (see ui/settings/network.ts for the coverage-ratchet
// rationale). The identity section is availability-gated by the pinned
// policy snapshot (`identity.active`) — the host decides; this view renders
// the `available:false` + reason state exactly as sent.
import { html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import '../settings-shell/settings-section.js';
import type {
  SettingsSectionData,
  XrStringMap,
} from '../settings-shell/settings-shell.js';
import type { SettingsRowData } from '../settings-shell/settings-section.js';

@customElement('xr-settings-identity')
export class XrSettingsIdentity extends LitElement {
  @property({ type: Object }) section: SettingsSectionData | null = null;
  @property({ type: Array }) rows: SettingsRowData[] = [];
  @property({ type: Object }) strings: XrStringMap = {};

  protected override render() {
    return html`
      <xr-settings-section
        .section=${this.section}
        .rows=${this.rows}
        .strings=${this.strings}></xr-settings-section>`;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'xr-settings-identity': XrSettingsIdentity;
  }
}
