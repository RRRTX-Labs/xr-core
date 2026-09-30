// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// xr-update-tab — the update-available tab's PURE CORE (P13-T5).
//
// T1 TIER ONLY. This tab tells you that a newer version EXISTS; it does not
// download, does not install, does not restart, and does not offer to. The whole
// surface is a sentence about two version strings and a channel, plus a link to
// the update page where the download actually happens.
//
// The law this file exists to make mechanical: NO WORDING AND NO STATE THAT
// IMPLIES AN UPDATE HAPPENED AUTOMATICALLY. That is a property of the data, not
// of the copy, and it is enforced twice here:
//
//   * `actionFor()` can only ever return `open-update-page`, `retry` or `none`.
//     There is no `install`, no `restart`, no `apply` — a verb that does not
//     exist cannot be rendered by a renderer that trusts this function, and the
//     test enumerates the returned verbs over every state;
//   * `FORBIDDEN_STATES` names the states this tab is not allowed to have
//     (`downloading`, `installing`, `restart-pending`, `auto-updated`) and
//     `assertKnownState()` throws on one, so a renderer cannot invent a state
//     that reads as progress.
//
// The channel state is P10's (`running_version`, `available_version`,
// `checked_at`, `channel`) — reused verbatim, never re-derived: two components
// with their own idea of "which version am I on" is how a UI starts lying.
// `checked_at` is supplied by the caller (the host pushes it), so nothing here
// reads a clock.
//
// No DOM, no I/O, no fetching, no updater. The rendered half is NOT-RUN
// (method: docs/qa/browser-harness.md).

/** P10's channel state, verbatim. Nothing in this file derives a second copy. */
export interface ChannelState {
  channel: 'dev' | 'beta' | 'stable';
  running_version: string;
  available_version: string | null;
  /** Caller-supplied ISO timestamp of the last successful check. */
  checked_at: string;
}

export const UPDATE_STATES = [
  'up-to-date',
  'update-available',
  'checking',
  'check-failed',
  'channel-unknown',
] as const;
export type UpdateState = (typeof UPDATE_STATES)[number];

/** States that would imply the product updates itself. This tab may not have them. */
export const FORBIDDEN_STATES = [
  'downloading',
  'installing',
  'restart-pending',
  'auto-updated',
  'applying',
] as const;

export function assertKnownState(state: string): UpdateState {
  if ((FORBIDDEN_STATES as readonly string[]).includes(state)) {
    throw new Error(`forbidden-state:${state}`);
  }
  if (!(UPDATE_STATES as readonly string[]).includes(state)) {
    throw new Error(`unknown-state:${state}`);
  }
  return state as UpdateState;
}

export function updateAvailable(state: ChannelState | null): boolean {
  if (state === null || state.available_version === null) return false;
  return state.available_version !== state.running_version;
}

/** The tab's row set, derived only from the channel state. */
export function updateRows(state: ChannelState | null): Array<{ key: string; value: string }> {
  if (state === null) {
    return [{ key: 'state', value: 'channel-unknown' }];
  }
  const rows = [
    { key: 'state', value: updateAvailable(state) ? 'update-available' : 'up-to-date' },
    { key: 'channel', value: state.channel },
    { key: 'running_version', value: state.running_version },
    { key: 'checked_at', value: state.checked_at },
  ];
  if (updateAvailable(state)) {
    rows.push({ key: 'available_version', value: String(state.available_version) });
  }
  return rows;
}

/** What the user can DO. The verb set is closed, and none of it is an install. */
export function actionFor(state: ChannelState | null): {
  verb: 'open-update-page' | 'retry' | 'none';
  label_key: string;
} {
  if (state === null) {
    return { verb: 'none', label_key: 'panel.update.state.unknown' };
  }
  if (updateAvailable(state)) {
    // The ONE action: go where the download lives. T1 does not update anything.
    return { verb: 'open-update-page', label_key: 'panel.update.action.openPage' };
  }
  return { verb: 'none', label_key: 'panel.update.state.current' };
}

/** The whole surface as one machine-readable summary, for the string audit. */
export function updateSummary(state: ChannelState | null): {
  state: UpdateState;
  verbs: string[];
  implies_automatic_update: false;
} {
  return { state: state === null ? 'channel-unknown' : 'update-available',
           verbs: [actionFor(state).verb],
           implies_automatic_update: false };
}
