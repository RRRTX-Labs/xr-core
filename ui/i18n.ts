// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// View-side i18n helper (P8-T5). Every user-visible string in the XR views
// is a message id from xr-core/l10n/xr_strings.grdp (the single en-US
// source; grdp_check validates the file). The host glue (P16) supplies the
// text map; until then a view renders the raw id — visible, honest, never a
// fabricated translation. Placeholders: messages embed {UPPER_SNAKE} tokens
// (the grdp <ph name> program text) and text() substitutes them from the
// params argument; an unknown token stays literal so missing params can
// never crash a render.
export type XrStringMap = Record<string, string>;

export function text(
  strings: XrStringMap,
  id: string,
  params?: Record<string, string>,
): string {
  const base = strings[id] ?? id;
  if (!params) return base;
  return base.replace(
    /\{([A-Z][A-Z0-9_]*)\}/g,
    (m, k: string) => (k in params ? params[k] : m),
  );
}
