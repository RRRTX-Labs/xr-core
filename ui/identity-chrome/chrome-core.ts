// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Identity chrome: the PURE CORE (P14-T4, P14-CLOSE C-1).
//
// Plan §4 P14 T4: "visual system in *all* layouts: 2 px tab color bar +
// identity glyph/text, pill (name/color/route summary), ephemeral full-window
// border (amber dashed), SR-announced on tab focus". Plan §2: "Color never
// alone — always + glyph + text."
//
// The state -> visual mapping is DATA (states.json). This file turns one
// identity, one layout and one theme into a STRUCTURAL description of the DOM/AX
// shape, with no DOM, no I/O and no clock. The structure includes roles,
// accessible names as message ids, classes, parts, colors and contrast verdicts.
// The lit elements in identity-chrome.ts render what this returns. The
// structural snapshots under snapshots/ are this function's output, one file
// per layout x theme. xr-browser's tools/identity_chrome_check.py generates
// them, and tests/identity-chrome.test.mjs requires this core to reproduce
// every file exactly. Pixels are NOT-RUN
// (docs/qa/browser-harness.md#identity-chrome-visual).
//
// Contrast follows WCAG 2.x relative luminance. A 2 px bar is a non-text
// graphic (1.4.11, 3:1). Where an identity color falls below that on a theme's
// tab strip, the core adds a 1 px edge in the theme's text token. The low
// ratio is recorded, not hidden, and the edge restores a perceivable boundary.
// Ratios are rounded to 2 decimals (half-up) BEFORE any comparison, so the
// TypeScript core and the Python generator cannot disagree at a boundary.

export type ThemeTokens = Record<string, string | number>;

export interface ChromeLayout {
  bar_side: 'block-start' | 'inline-start' | 'none';
  mark: 'bar' | 'dot';
  glyph: boolean;
  label: boolean;
}

export interface WindowBorderSpec {
  style: 'dashed' | 'solid';
  px: number;
  /** "identity" (the identity's own color) or "token:<name>". */
  color: string;
}

export interface ChromeState {
  class: string;
  accessible_name: string;
  sr: string;
  route: string;
  window_border: WindowBorderSpec | null;
}

export interface ChromeTable {
  schema: string;
  bar_px: number;
  edge_px: number;
  dot_px: number;
  contrast_min: { non_text: number; text: number };
  tokens: { strip_surface: string; pill_text: string; pill_bg: string; edge: string };
  layouts: Record<string, ChromeLayout>;
  states: Record<string, ChromeState>;
}

export interface ChromeIdentity {
  template: string;
  name: string;
  color: string;
  glyph: string;
  state: string;
}

export interface Msg {
  msg: string;
  params?: Record<string, string | Msg>;
}

export interface Edge { token: string; px: number; color: string; ratio: number }

/** A refusal is typed and carries no identity value (the derivation law). */
export class ChromeRefusal extends Error {}

function channel(c: number): number {
  return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
}

function luminance(hex: string): number {
  const h = hex.replace('#', '').slice(0, 6);
  if (!/^[0-9a-fA-F]{6}$/.test(h)) {
    throw new ChromeRefusal('color-not-hex');
  }
  const [r, g, b] = [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16) / 255);
  return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

/** WCAG contrast ratio, rounded half-up to 2 decimals. */
export function contrast(a: string, b: string): number {
  const [hi, lo] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return Math.floor(((hi + 0.05) / (lo + 0.05)) * 100 + 0.5) / 100;
}

function token(theme: ThemeTokens, name: string): string {
  const v = theme[name];
  if (typeof v !== 'string') {
    throw new ChromeRefusal(`theme-token-missing:${name}`);
  }
  return v;
}

function edgeFor(table: ChromeTable, theme: ThemeTokens, ratio: number): Edge | null {
  if (ratio >= table.contrast_min.non_text) {
    return null;
  }
  const color = token(theme, table.tokens.edge);
  return {
    token: table.tokens.edge,
    px: table.edge_px,
    color,
    ratio: contrast(color, token(theme, table.tokens.strip_surface)),
  };
}

/** The structural description of one identity's chrome in one layout x theme. */
export function structure(table: ChromeTable, theme: ThemeTokens,
                          identity: ChromeIdentity, layoutId: string): Record<string, unknown> {
  const state = table.states[identity.state];
  if (!state) {
    throw new ChromeRefusal('unknown-state');
  }
  const layout = table.layouts[layoutId];
  if (!layout) {
    throw new ChromeRefusal('unknown-layout');
  }
  const surface = token(theme, table.tokens.strip_surface);
  const markRatio = contrast(identity.color, surface);
  const mark = layout.mark === 'dot'
    ? { part: 'dot', px: table.dot_px, color: identity.color, edge: edgeFor(table, theme, markRatio) }
    : { part: 'bar', side: layout.bar_side, px: table.bar_px, color: identity.color,
        edge: edgeFor(table, theme, markRatio) };
  const glyphText = layout.mark === 'dot' ? identity.name.slice(0, 1).toUpperCase() : identity.glyph;
  const tabChildren: Record<string, unknown>[] = [mark];
  if (layout.glyph) {
    tabChildren.push({ part: 'glyph', text: glyphText, aria_hidden: true });
  }
  if (layout.label) {
    tabChildren.push({ part: 'label', text: identity.name });
  }
  const route: Msg = { msg: state.route };
  let border: Record<string, unknown> | null = null;
  let borderRatio: number | null = null;
  if (state.window_border) {
    const spec = state.window_border;
    const color = spec.color === 'identity' ? identity.color
      : token(theme, spec.color.replace(/^token:/, ''));
    borderRatio = contrast(color, surface);
    border = { part: 'window-border', style: spec.style, px: spec.px, color,
               edge: edgeFor(table, theme, borderRatio), aria_hidden: true };
  }
  return {
    sample: identity.template,
    state: identity.state,
    class: ['xr-idc', state.class],
    tab: {
      role: 'tab',
      description: { msg: state.sr, params: { NAME: identity.name } },
      children: tabChildren,
    },
    pill: {
      role: 'button',
      name: { msg: state.accessible_name, params: { NAME: identity.name, ROUTE: route } },
      children: [
        { part: 'swatch', color: identity.color, aria_hidden: true },
        { part: 'glyph', text: identity.glyph, aria_hidden: true },
        { part: 'name', text: identity.name },
        { part: 'route', msg: state.route },
      ],
    },
    window_border: border,
    contrast: {
      mark: markRatio,
      pill_text: contrast(token(theme, table.tokens.pill_text), token(theme, table.tokens.pill_bg)),
      window_border: borderRatio,
    },
  };
}

/** Every sample in one layout x theme: exactly one snapshot file's body. */
export function snapshot(table: ChromeTable, theme: ThemeTokens, themeId: string,
                         layoutId: string, samples: ChromeIdentity[]): Record<string, unknown> {
  return {
    schema: 'identity-chrome-snapshot-v1',
    layout: layoutId,
    theme: themeId,
    nodes: samples.map((s) => structure(table, theme, s, layoutId)),
  };
}

/** The SR announcement on tab focus, resolved through the string map. */
export function srAnnouncement(table: ChromeTable, identity: ChromeIdentity,
                               strings: Record<string, string>): string {
  const state = table.states[identity.state];
  if (!state) {
    throw new ChromeRefusal('unknown-state');
  }
  const base = strings[state.sr] ?? state.sr;
  return base.replace(/\{NAME\}/g, identity.name);
}
