// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1-adjacent — command descriptor validation against the FROZEN
// `command-descriptor-v1` contract (xr-browser/docs/contracts/
// command-descriptor-v1.schema.json). The frozen schema is the source of
// truth; this module IMPLEMENTS TO IT, never redefines it:
//
//   required:  id, title, attention_tier, danger_class, surface, handler
//   enums:     attention_tier in {tier0,tier1,tier2}
//              danger_class  in {safe,caution,destructive}
//   strict:    additionalProperties: false — an unknown field is a rejection
//
// The registry's post-freeze metadata (keywords/group/scope/availability
// predicate data-id) is NOT part of this frozen descriptor object; it is a
// P7-born surface registered via docs/contracts/registry-post-freeze.md and
// validated separately (availability-predicate-contract v1). No exceptions;
// a typed result carries the rejection reason (deny-default, L3).
#pragma once

#include <string>
#include <vector>

#include "commands/core/json.h"

namespace xr::commands {

// The six FROZEN descriptor fields, in canonical order.
struct DescriptorFields {
  std::string id;
  std::string title;
  std::string attention_tier;  // tier0 | tier1 | tier2
  std::string danger_class;    // safe | caution | destructive
  std::string surface;         // e.g. command-palette, tools-menu
  std::string handler;         // a registered action id (declarative)
};

struct DescriptorResult {
  bool ok = false;
  DescriptorFields fields;
  std::string error;  // human-readable, cites the frozen-schema rule
};

// The frozen enum sets (single source, also cross-checked in tests against the
// .schema.json so a drift between this code and the contract is a failure).
extern const std::vector<std::string> kAttentionTiers;
extern const std::vector<std::string> kDangerClasses;
extern const std::vector<std::string> kRequiredFields;

// Strict validation of a descriptor JSON object against the frozen schema.
// Rejects: not-an-object, any missing required field, empty string field,
// out-of-enum attention_tier/danger_class, and ANY unknown field.
DescriptorResult ValidateDescriptor(const JsonValue& obj);

}  // namespace xr::commands
