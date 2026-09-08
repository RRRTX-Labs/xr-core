// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// WebUI shell entry (esbuild entrypoint). Registers the four-views-one-registry
// custom elements (palette / shortcut editor / help index; the menu model is
// data, not an element). The host glue (P16) pushes host-protocol data into the
// views' properties and reads the palette's onInvoke. No network, no logic here.
import './palette/palette.js';
import './shortcut-editor/shortcut-editor.js';
import './help-index/help-index.js';

export const XR_UI_VIEWS = ['xr-palette', 'xr-shortcut-editor', 'xr-help-index'];
