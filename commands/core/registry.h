// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1 — the Command Registry (Plan §1.10, DR-24 "Command Registry
// before features"). One registry; palette / Tools menu / overflow menu /
// shortcut editor / printable cheatsheet / help index are all VIEWS over it.
//
// A Command = the FROZEN descriptor (validated strictly, command-descriptor-v1)
// plus the P7 post-freeze metadata (availability-predicate-contract v1):
// keywords (matcher recall), group (menu grouping), scope (binding scope), and
// the availability-predicate data-id. The tier-1 ceiling is enforced HERE, in
// the registry itself (not in any view): a 10th tier1 control is rejected at
// registration with the cited rule — "Tier-1 always-visible <=9 controls"
// (Plan §1.10 Chrome tiers / Attention Budget, §10).
//
// No exceptions; register() returns a typed result.
#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "commands/core/descriptor.h"

namespace xr::commands {

// Binding scope (post-freeze registry metadata; not a frozen-descriptor field).
enum class Scope { kGlobal, kWindow, kIdentity, kSite };
Scope ScopeFromString(const std::string& s);       // unknown => kGlobal
const char* ToString(Scope s);

struct Command {
  // --- frozen descriptor (command-descriptor-v1) ---
  std::string id;
  std::string title;
  std::string attention_tier;  // tier0 | tier1 | tier2
  std::string danger_class;    // safe | caution | destructive
  std::string surface;
  std::string handler;
  // --- post-freeze registry metadata (availability-predicate-contract v1) ---
  std::vector<std::string> keywords;
  std::string group;                  // menu grouping (e.g. "Identity")
  Scope scope = Scope::kGlobal;
  std::string predicate_id = "always";  // registered availability data-id
  // registration order (stable matcher tie-break; assigned by the registry)
  size_t order = 0;
};

struct RegisterResult {
  bool ok = false;
  std::string error;   // cites the violated rule
  size_t order = 0;    // assigned order (ok only)
};

class Registry {
 public:
  // Tier-1 ceiling (Plan §1.10 "Tier 1 always-visible <=9 controls").
  static constexpr size_t kMaxTier1 = 9;

  // Validate + insert. Rejects (typed, rule cited): duplicate id, missing
  // frozen field, out-of-enum danger_class/attention_tier (re-run of the
  // frozen-schema validation on the struct), and a 10th tier1 control.
  RegisterResult Register(const Command& cmd);

  bool KnownId(const std::string& id) const;
  const Command* Find(const std::string& id) const;

  // Registration order (deterministic).
  std::vector<const Command*> List() const;
  std::vector<const Command*> Group(const std::string& group) const;
  std::vector<std::string> Groups() const;  // first-seen order
  size_t Tier1Count() const;
  size_t Size() const { return by_id_.size(); }

  // Serialize to canonical JSON (commands-registry-v1) for the host store.
  JsonValue ToJson() const;
  // Parse a commands-registry-v1 doc; returns false (error) on schema mismatch.
  bool FromJson(const JsonValue& doc, std::string* error);

 private:
  std::map<std::string, Command> by_id_;
  std::vector<std::string> order_;
  size_t tier1_ = 0;
};

}  // namespace xr::commands
