// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The NETWORK settings section view (P8-T1). Thin per-section wrapper over
// <xr-settings-section>: host glue (P16) pushes the section data + rows.
// This file exists so the §10 coverage ratchet (coverage_check.py) sees a
// LANDED ui/settings/network surface covered by the roster's
// `settings.network` command (coverage-allowlist.yaml) — the view itself
// renders no ranking/availability logic (all of it is C++ core).
import { html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import '../settings-shell/settings-section.js';
import type {
  SettingsSectionData,
  XrStringMap,
} from '../settings-shell/settings-shell.js';
import type { SettingsRowData } from '../settings-shell/settings-section.js';

@customElement('xr-settings-network')
export class XrSettingsNetwork extends LitElement {
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
    'xr-settings-network': XrSettingsNetwork;
  }
}
