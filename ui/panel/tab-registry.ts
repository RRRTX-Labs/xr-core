// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// xr-panel tab registry — the declarative tab API (P13-C-P0.1 / T-C5).
//
// Why this file exists. The §10 law is "any feature without a command does not
// ship (CI check: every `Settings` section and panel tab maps to a command)".
// A panel tab is therefore a DECLARED thing, not a `.ts` file that happens to
// live under ui/panel/: `focus-trap.ts` and `panel-frame.ts` implement the
// `panel/xr` surface and are not tabs at all. `tools/coverage_check.py` reads
// this inventory (`tabs.json`) as its unit; this module is the runtime half
// that makes the inventory binding rather than advisory.
//
// The API is declarative and additive-only:
//   * a subsystem adds a tab by calling `register({id, title_msgid, order,
//     requires_identity_scope})`;
//   * an id that is NOT in the inventory is REFUSED with the typed error
//     `unknown-tab-id` — never dropped, never silently ignored (a dropped tab
//     is indistinguishable from a tab a user never opened, which is exactly
//     the invisibility this repo keeps paying for);
//   * two tabs claiming the same `order` is a validation failure
//     (`order-collision` at registration, refused by `parseInventory` inside
//     the inventory itself) — resolved by the author editing the inventory,
//     never by a tie-breaker here;
//   * registering the same id twice is `duplicate-id`.
//
// The inventory owns `order`. A subsystem may pass the order it read from the
// inventory; passing a different one is `order-mismatch` (a subsystem that
// renumbers itself silently reshuffles every user's tab strip).
//
// There is no `unregister`. A tab a subsystem stops providing reports itself
// unavailable at render time; removing a tab from the inventory is a contract
// change to `panel-tab-registration-v1` (docs/contracts/), not a runtime call.
//
// Strings: the registry carries message IDS only (title_msgid). It renders no
// text and owns no copy. Every `detail` below is a MACHINE TOKEN, not prose:
// l10n_extract's R4 rule forbids space-bearing literals anywhere in ui/** (they
// are indistinguishable from user-visible copy at rest), so a diagnostic is
// `duplicate-order:<owner>:<id>` that a test and a grep can both match, and
// never a sentence that reads like UI text.
// No DOM, no I/O: this module is pure data so node:test can prove it without a
// browser (the rendered halves are NOT-RUN with methods in
// docs/qa/browser-harness.md).

/** One declared panel tab. Shape == docs/contracts/panel-tab-registration-v1.schema.json. */
export interface TabRegistration {
  id: string;
  title_msgid: string;
  order: number;
  requires_identity_scope: boolean;
}

/** Typed refusals. A refusal always names what was refused and why. */
export type TabErrorCode =
  | 'unknown-tab-id'
  | 'order-collision'
  | 'order-mismatch'
  | 'order-out-of-range'
  | 'duplicate-id'
  | 'duplicate-order'
  | 'bad-shape';

export interface TabError {
  code: TabErrorCode;
  id: string;
  detail: string;
}

export interface RegistrationResult {
  ok: boolean;
  errors: TabError[];
}

/** The inventory document (`ui/panel/tabs.json`), as the host glue loads it. */
export interface TabInventory {
  schema: string;
  schema_version: number;
  tabs: TabRegistration[];
  reserved_orders?: { min?: number; max?: number };
}

const SCHEMA = 'xr-panel-tabs';
const REQUIRED_KEYS = ['id', 'title_msgid', 'order', 'requires_identity_scope'] as const;
const ID_RE = /^[a-z][a-z0-9-]{1,30}$/;

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

/** Validate one candidate tab. Returns errors; [] means the shape is legal. */
export function validateTab(raw: unknown, where = 'tab'): TabError[] {
  if (!isRecord(raw)) {
    return [{ code: 'bad-shape', id: where, detail: 'not-an-object' }];
  }
  const errors: TabError[] = [];
  const id = typeof raw['id'] === 'string' ? (raw['id'] as string) : '';
  const label = id || where;
  for (const key of REQUIRED_KEYS) {
    if (!(key in raw)) {
      errors.push({ code: 'bad-shape', id: label, detail: 'missing-required-field:' + key });
    }
  }
  if (!ID_RE.test(id)) {
    errors.push({ code: 'bad-shape', id: label, detail: 'not-a-slug:' + id });
  }
  if (typeof raw['title_msgid'] !== 'string' || (raw['title_msgid'] as string).length === 0) {
    errors.push({ code: 'bad-shape', id: label, detail: 'title-msgid-not-a-string' });
  }
  if (typeof raw['order'] !== 'number' || !Number.isInteger(raw['order'] as number)) {
    errors.push({ code: 'bad-shape', id: label, detail: 'order-not-an-integer' });
  }
  if (typeof raw['requires_identity_scope'] !== 'boolean') {
    errors.push({ code: 'bad-shape', id: label, detail: 'requires-identity-scope-not-a-boolean' });
  }
  return errors;
}

/**
 * Parse an inventory document. A malformed inventory is refused wholesale
 * (null), never patched: half an inventory renders half a panel.
 */
export function parseInventory(raw: unknown): { inventory: TabInventory | null; errors: TabError[] } {
  const errors: TabError[] = [];
  if (!isRecord(raw)) {
    return { inventory: null, errors: [{ code: 'bad-shape', id: 'inventory', detail: 'not-an-object' }] };
  }
  if (raw['schema'] !== SCHEMA) {
    errors.push({ code: 'bad-shape', id: 'inventory', detail: 'bad-schema:' + SCHEMA });
  }
  const rawTabs = raw['tabs'];
  if (!Array.isArray(rawTabs)) {
    return { inventory: null, errors: [...errors, { code: 'bad-shape', id: 'inventory', detail: 'tabs-not-an-array' }] };
  }
  rawTabs.forEach((t, i) => errors.push(...validateTab(t, `tabs[${i}]`)));
  const reserved = isRecord(raw['reserved_orders']) ? raw['reserved_orders'] : {};
  const min = typeof reserved['min'] === 'number' ? reserved['min'] : 1;
  const max = typeof reserved['max'] === 'number' ? reserved['max'] : 9999;
  const seenIds = new Set<string>();
  const seenOrders = new Map<number, string>();
  for (const t of rawTabs) {
    if (!isRecord(t)) {
      continue;
    }
    const id = String(t['id'] ?? '');
    if (seenIds.has(id) && id !== '') {
      errors.push({ code: 'duplicate-id', id, detail: 'duplicate-id:' + id });
    }
    seenIds.add(id);
    const order = t['order'];
    if (typeof order === 'number') {
      const owner = seenOrders.get(order);
      if (owner !== undefined && owner !== id) {
        errors.push({
          code: 'duplicate-order',
          id,
          detail: 'duplicate-order:' + owner + ':' + id,
        });
      }
      seenOrders.set(order, id);
      if (order < min || order > max) {
        errors.push({
          code: 'order-out-of-range',
          id,
          detail: 'order-out-of-range:' + order,
        });
      }
    }
  }
  if (errors.length > 0) {
    return { inventory: null, errors };
  }
  const inventory: TabInventory = {
    schema: raw['schema'] as string,
    schema_version: Number(raw['schema_version'] ?? 1),
    tabs: (rawTabs as unknown[]).map((t) => ({ ...(t as TabRegistration) })),
    reserved_orders: { min, max },
  };
  return { inventory, errors };
}

/**
 * The registry a subsystem talks to. Seeded from the inventory; `tabs()`
 * returns the REGISTERED tabs, never the inventory, so "declared but not
 * provided" stays distinguishable from "provided".
 */
export class PanelTabRegistry {
  private readonly declared = new Map<string, TabRegistration>();
  private readonly registered = new Map<string, TabRegistration>();
  private readonly minOrder: number;
  private readonly maxOrder: number;

  constructor(inventory: TabInventory) {
    for (const t of inventory.tabs) {
      this.declared.set(t.id, t);
    }
    this.minOrder = inventory.reserved_orders?.min ?? 1;
    this.maxOrder = inventory.reserved_orders?.max ?? 9999;
  }

  /** Ids the inventory declares, in render order. */
  declaredIds(): string[] {
    return [...this.declared.values()].sort((a, b) => a.order - b.order).map((t) => t.id);
  }

  /** Registered tabs in render order. Empty until a subsystem provides one. */
  tabs(): TabRegistration[] {
    return [...this.registered.values()].sort((a, b) => a.order - b.order);
  }

  /**
   * Register one tab. Additive-only; every refusal is typed and named, and a
   * refused registration mutates nothing.
   */
  register(entry: unknown): RegistrationResult {
    const errors = validateTab(entry);
    if (errors.length > 0) {
      return { ok: false, errors };
    }
    const tab = entry as TabRegistration;
    const declared = this.declared.get(tab.id);
    if (declared === undefined) {
      return {
        ok: false,
        errors: [{
          code: 'unknown-tab-id',
          id: tab.id,
          detail: 'unknown-tab-id:' + tab.id,
        }],
      };
    }
    if (this.registered.has(tab.id)) {
      return {
        ok: false,
        errors: [{ code: 'duplicate-id', id: tab.id, detail: 'duplicate-id:' + tab.id }],
      };
    }
    const clash = [...this.registered.values()].find((t) => t.order === tab.order);
    if (clash !== undefined) {
      return {
        ok: false,
        errors: [{
          code: 'order-collision',
          id: tab.id,
          detail: 'order-collision:' + clash.id,
        }],
      };
    }
    if (tab.order !== declared.order) {
      return {
        ok: false,
        errors: [{
          code: 'order-mismatch',
          id: tab.id,
          detail: 'order-mismatch:declared=' + declared.order + ',asked=' + tab.order,
        }],
      };
    }
    if (tab.order < this.minOrder || tab.order > this.maxOrder) {
      return {
        ok: false,
        errors: [{
          code: 'order-out-of-range',
          id: tab.id,
          detail: 'order-out-of-range:' + tab.order,
        }],
      };
    }
    this.registered.set(tab.id, tab);
    return { ok: true, errors: [] };
  }
}

/** The frame's own surface: the panel is not a tab. */
export const FRAME_SURFACE = 'panel/xr';
