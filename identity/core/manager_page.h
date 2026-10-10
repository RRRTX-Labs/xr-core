// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the xr://identities manager page MODEL (P14-T7, closed at
// P14-CLOSE C-3). The page is a DEV page: the same enforcement as xr://shield
// applies (the roster command rides build.channel-dev; the host refuses any
// other channel with a typed reason and renders no page bytes). This module
// is the pure half the page renders from:
//   * the page-state union kManagerPageStates — ui/identities must render
//     every value (tools/shield_state_check.py, extended at C-3);
//   * DevChannel — the dev-only law as code, shared by the page and by the
//     reset-all escape (fails CLOSED: only the exact string "dev" passes);
//   * ApplyPageEdit — rename / recolor / archive. Edits touch display
//     metadata only, never the opaque domain (the storage key); a rename that
//     the domain would appear to embed is refused (the opacity law);
//   * BuildManagerPage — per-identity stats: tab count (from the binding's
//     latest row per tab), storage bytes (the purge-verify walk: surface +
//     out-of-place residual), permission count (supplied by the caller from
//     the permission overlay; absent = null, never a guessed 0), and the
//     purge outcomes (verified or not, with the residual kinds named).
// Refusal strings never echo input (a log line built from one would carry
// whatever the caller typed).
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "common/core/json.h"
#include "core/binding.h"
#include "core/identity.h"

namespace xr::identity {

// The closed page-state set, in render-priority order.
inline constexpr const char* kManagerPageStates[] = {
    "normal", "empty", "purge-unverified", "dev-refused"};
inline constexpr size_t kDisplayNameMax = 64;

// True only for the "dev" channel. Otherwise `refusal` (when non-null) is
// "build-channel-not-dev" — the channel value itself is not echoed.
bool DevChannel(std::string_view channel, std::string* refusal);

struct PurgeOutcome {
  std::string domain;
  bool verified = false;
  std::vector<std::string> residual_kinds;
};

// The page's edits. Archive hibernates: the v1 core has no separate
// archived state (the manager mojom's archive is review-complete, HG-26).
enum class PageEdit { kRename, kRecolor, kArchive };

CallResult ApplyPageEdit(IdentityStore* store, Manager* manager,
                         std::string_view domain, PageEdit edit,
                         std::string_view value);

// Tabs per identity: the binding's latest row per tab (what a session
// snapshot computes from).
std::map<std::string, int64_t> TabCounts(const BindingModel& binding);

// The page model. `order` is the display order (provisioning order); a
// domain no longer in the store contributes no row. State: any unverified
// purge => "purge-unverified"; else no rows => "empty"; else "normal".
// ("dev-refused" is the host's refusal, rendered by the view.)
xr::common::JsonValue BuildManagerPage(
    const IdentityStore& store, const BindingModel& binding,
    const std::vector<std::string>& order,
    const std::map<std::string, int64_t>& permission_counts,
    const std::vector<PurgeOutcome>& purges);

}  // namespace xr::identity
