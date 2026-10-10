// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The PERMISSIONS settings section view (P15-CLOSE C5, ADR-0051 addendum).
// Thin per-section wrapper over <xr-settings-section> (see
// ui/settings/network.ts for the coverage-ratchet rationale). The section is
// the destination `xr://settings/permissions`; it owns no schema rows in v0
// (`privacy.notifications` stays where it is — moving it would be a schema
// migration). Its search words are schema data (the section's `aliases`),
// ranked by the C++ core, never by this view.
import { html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import '../settings-shell/settings-section.js';
import type {
  SettingsSectionData,
  XrStringMap,
} from '../settings-shell/settings-shell.js';
import type { SettingsRowData } from '../settings-shell/settings-section.js';

@customElement('xr-settings-permissions')
export class XrSettingsPermissions extends LitElement {
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
    'xr-settings-permissions': XrSettingsPermissions;
  }
}
