// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The panel's focus-containment core (P13-T1).
//
// The panel is MODAL: while it is open, keyboard focus may not leave it. The
// law this file exists to make structural:
//
//   * Tab from the LAST focusable inside the frame goes to the FIRST (never
//     out of the frame);
//   * Shift+Tab from the FIRST goes to the LAST;
//   * a Tab that would land OUTSIDE the frame is *intercepted* and moved to the
//     first (forward) or last (backward) focusable inside it — including the
//     case where focus is already outside when the handler runs (a re-open
//     race, or a host that moved focus after we opened);
//   * Escape closes; the caller restores focus to the opener it captured.
//
// Why a core + adapter instead of DOM calls inline: the sandbox has no browser
// and the toolchain is dependency-frozen (lit/axe-core/esbuild/typescript; no
// jsdom, no happy-dom), so the invariant has to be provable WITHOUT a DOM. The
// DOM-facing half (`panel-frame.ts`) implements this adapter; the tests drive
// the core through a hand-rolled focus world, and the containment invariant has
// a planted-leak negative that must redden the suite (build/webui/panel-tests.sh
// --plant-leak). A trap that is only described in a docstring is exactly the
// "docstring, not a test" failure the phase brief names.
//
// No DOM types here on purpose: this module is importable in node, and the
// adapter (not this core) owns document/window.

/** One focusable element, as the adapter reports it. */
export interface FocusTarget {
  /** Stable id — the world's handle for the element. */
  id: string;
  /** tabindex as authored; < 0 means programmatically focusable only. */
  tabindex?: number;
  /** Disabled controls are not focusable. */
  disabled?: boolean;
  /** Hidden (display:none / hidden attr / aria-hidden ancestry): not focusable. */
  hidden?: boolean;
}

/** The adapter surface the trap needs. Implemented by panel-frame.ts. */
export interface FocusWorld {
  /** id of the element that currently has focus, or null. */
  active(): string | null;
  /** Move focus to `id` (adapter no-ops on an unknown or unfocusable id). */
  focus(id: string): void;
  /** The frame's focusable descendants, in DOM order. */
  inside(root: string): FocusTarget[];
  /** Every focusable element in the document, in DOM order (the frame's
   * descendants included exactly once). Used to decide what "next" means. */
  all(): FocusTarget[];
}

export type TrapAction =
  | { kind: 'focus'; id: string }
  | { kind: 'close' }
  | { kind: 'ignore' };

/** Focusable per the DOM: not hidden, not disabled, tabindex >= 0. */
function focusable(t: FocusTarget): boolean {
  return !t.hidden && !t.disabled && (t.tabindex ?? 0) >= 0;
}

export function focusables(world: FocusWorld, root: string): FocusTarget[] {
  return world.inside(root).filter(focusable);
}

/**
 * The containment law. Given where focus is and which way the user is moving,
 * return the element focus must land on — or null when the browser's own
 * tab order already stays inside the frame.
 *
 * `activeId` may be outside the frame (or unknown): that is the re-open race,
 * and it is contained rather than trusted.
 */
export function containedTarget(
  world: FocusWorld,
  root: string,
  activeId: string | null,
  backward: boolean,
): string | null {
  const inside = focusables(world, root);
  if (inside.length === 0) return null; // nothing to contain to; caller decides
  const insideIds = new Set(inside.map((t) => t.id));

  if (activeId === null || !insideIds.has(activeId)) {
    // Focus is outside (or nowhere): pull it in, in the direction of travel.
    return backward ? inside[inside.length - 1].id : inside[0].id;
  }

  // Where would the browser go next, ignoring us?
  const all = world.all().filter(focusable).map((t) => t.id);
  const at = all.indexOf(activeId);
  if (at < 0) return backward ? inside[inside.length - 1].id : inside[0].id;
  const step = backward ? -1 : 1;
  const next = all[(at + step + all.length) % all.length];
  // The browser wraps at the document ends; we only care about escapes.
  if (!insideIds.has(next)) {
    return backward ? inside[inside.length - 1].id : inside[0].id;
  }
  return null; // the browser's own move stays inside — do not fight it
}

/** The keydown decision. The adapter applies the action; the core only decides. */
export function trapKeydown(
  world: FocusWorld,
  root: string,
  key: string,
  backward: boolean,
): TrapAction {
  if (key === 'Escape') return { kind: 'close' };
  if (key !== 'Tab') return { kind: 'ignore' };
  const target = containedTarget(world, root, world.active(), backward);
  return target === null ? { kind: 'ignore' } : { kind: 'focus', id: target };
}

/**
 * The containment invariant, as a reusable assertion — shared by the tests and
 * by the "planted leak" negative so that both judge the same property. Walks
 * `steps` tabs from wherever focus is and reports the ids it visited; any id
 * outside the frame is an escape.
 */
export function walkTabs(
  world: FocusWorld,
  root: string,
  steps: number,
  backward = false,
): string[] {
  const visited: string[] = [];
  for (let i = 0; i < steps; i += 1) {
    const action = trapKeydown(world, root, 'Tab', backward);
    if (action.kind === 'focus') world.focus(action.id);
    else if (action.kind === 'ignore') {
      // The browser moves focus itself: emulate the real tab order.
      const all = world.all().filter(focusable).map((t) => t.id);
      const at = all.indexOf(world.active() ?? '');
      const step = backward ? -1 : 1;
      const next = at < 0 ? all[0] : all[(at + step + all.length) % all.length];
      if (next !== undefined) world.focus(next);
    }
    visited.push(world.active() ?? '<none>');
  }
  return visited;
}

/** Escapes = ids visited that are not inside the frame. */
export function escapes(world: FocusWorld, root: string, visited: string[]): string[] {
  const insideIds = new Set(focusables(world, root).map((t) => t.id));
  return visited.filter((id) => !insideIds.has(id));
}
