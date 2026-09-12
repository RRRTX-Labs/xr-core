// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// WebUI shell entry (esbuild entrypoint). Registers the P7 four-views-one-
// registry custom elements (palette / shortcut editor / help index) and the
// P8 settings shell + per-section views (network / privacy / identity). The
// host glue (P16) pushes host-protocol data into the views' properties and
// reads the palette's onInvoke / the settings shell's onJump. No network,
// no logic here.
import './palette/palette.js';
import './shortcut-editor/shortcut-editor.js';
import './help-index/help-index.js';
import './settings-shell/settings-shell.js';
import './settings/network.js';
import './settings/privacy.js';
import './settings/identity.js';
import './about/about.js';
import './shield/shield.js';

export const XR_UI_VIEWS = [
  'xr-palette',
  'xr-shortcut-editor',
  'xr-help-index',
  'xr-settings-shell',
  'xr-settings-network',
  'xr-settings-privacy',
  'xr-settings-identity',
  'xr-about',
  'xr-shield',
];
