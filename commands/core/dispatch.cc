// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — the DISPATCH security edge (see dispatch.h). C++ only, never TS.
#include "commands/core/dispatch.h"

#include <algorithm>

#include "commands/core/json.h"

namespace xr::commands {
namespace {

// Build a canonical (sorted-keys) ledger row. seq is a monotonic counter —
// no clock, so ledger bytes are deterministic for a given invocation sequence.
std::string LedgerRow(int seq, const std::string& event,
                      const std::string& reason, const std::string& id,
                      const std::string& source) {
  JsonValue::Object o = {
      {"event", JsonValue(event)},
      {"id", JsonValue(id)},
      {"reason", JsonValue(reason)},
      {"seq", JsonValue(static_cast<int64_t>(seq))},
      {"source", JsonValue(source)},
  };
  return JsonValue(o).Canonical();
}

}  // namespace

const std::vector<std::string>& Dispatcher::AllowedSources() {
  // The palette never executes page-originated commands: "page" is NOT in
  // this set, so a page-originated invocation is rejected + ledgered.
  static const std::vector<std::string> kSources = {
      "ui-chrome", "palette", "menu", "shortcut", "test"};
  return kSources;
}

void Dispatcher::Record(const std::string& event, const std::string& reason,
                        const std::string& id, const std::string& source) const {
  std::string row = LedgerRow(static_cast<int>(seq_++), event, reason, id, source);
  ledger_.push_back(row);
}

InvokeOutcome Dispatcher::Invoke(const std::string& id, const std::string& source,
                                 bool confirmed) const {
  InvokeOutcome out;
  out.id = id;
  out.source = source;

  // 1) Source-tag allowlist. Page-originated and any unknown source are
  //    rejected BEFORE the id is even looked up (defense in depth).
  if (std::find(AllowedSources().begin(), AllowedSources().end(), source) ==
      AllowedSources().end()) {
    out.status = "rejected";
    out.reason = source == "page"
                     ? "page-originated invocations are rejected "
                       "(cross-process origin check; the palette never "
                       "executes page-originated commands)"
                     : "source tag '" + source + "' is not whitelisted";
    Record("invoke-reject", out.reason, id, source);
    out.ledger_rows.push_back(ledger_.back());
    return out;
  }

  // 2) Id whitelist: unknown id never reaches a handler.
  const Command* cmd = reg_.Find(id);
  if (cmd == nullptr) {
    out.status = "rejected";
    out.reason = "unknown command id '" + id + "' (registry id whitelist)";
    Record("invoke-reject", out.reason, id, source);
    out.ledger_rows.push_back(ledger_.back());
    return out;
  }
  out.handler = cmd->handler;
  out.danger_class = cmd->danger_class;

  // 3) Danger-class confirmation gate (L7 friction): destructive requires an
  //    explicit confirm; modeled as predicate-output data, not a crash.
  if (cmd->danger_class == "destructive" && !confirmed) {
    out.status = "confirmation-required";
    out.reason = "destructive command '" + id +
                 "' requires explicit confirmation (L7: friction is a feature)";
    Record("invoke-confirm-required", "destructive", id, source);
    out.ledger_rows.push_back(ledger_.back());
    return out;
  }

  // Authorized: source ok, id known, danger confirmed (or non-destructive).
  out.authorized = true;
  out.status = "authorized";
  Record("invoke-allow", "ok", id, source);
  out.ledger_rows.push_back(ledger_.back());
  return out;
}

}  // namespace xr::commands
