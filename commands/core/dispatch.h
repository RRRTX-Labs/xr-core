// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — the DISPATCH security edge (Plan P7 Security req, §9).
// Dispatch happens in C++ (never in TS). Every invocation carries a source
// tag; only whitelisted sources may invoke. Registry ids are a whitelist: an
// unknown id never reaches a handler. Destructive danger classes require
// confirmation (L7 "friction is a feature") — modeled as predicate OUTPUT
// data (a confirmation-required result), not a hard crash.
//
//   page-originated  => reject + ledger row   (cross-process origin check;
//                                                the palette never executes
//                                                page-originated commands)
//   unknown id       => reject + ledger row
//   non-whitelisted source => reject + ledger row
//   destructive & !confirmed => confirmation-required (NOT executed)
//
// Rejects are typed results with ledger rows (L6). No exceptions cross this
// boundary.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "commands/core/registry.h"

namespace xr::commands {

struct InvokeOutcome {
  bool authorized = false;   // passed all gates; the handler may run
  std::string status;        // "rejected" | "confirmation-required" | "authorized"
  std::string reason;        // empty when authorized
  std::string id;
  std::string source;
  std::string handler;       // registered action id (authorized only)
  std::string danger_class;
  std::vector<std::string> ledger_rows;  // canonical reject/allow rows (L6)
};

class Dispatcher {
 public:
  // Allowed source tags. Everything else (notably "page") is rejected.
  static const std::vector<std::string>& AllowedSources();

  explicit Dispatcher(const Registry& reg) : reg_(reg) {}

  InvokeOutcome Invoke(const std::string& id, const std::string& source,
                       bool confirmed) const;

  const std::vector<std::string>& ledger() const { return ledger_; }

 private:
  const Registry& reg_;
  mutable std::vector<std::string> ledger_;
  mutable size_t seq_ = 0;
  void Record(const std::string& event, const std::string& reason,
              const std::string& id, const std::string& source) const;
};

}  // namespace xr::commands
