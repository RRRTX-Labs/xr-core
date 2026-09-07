// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — file-based managed (enterprise) policy source (Plan P6-T7,
// minimal-now/formal-later). Laws implemented here:
//   * SIGNED-OR-IGNORED (L6): an unsigned, bad-signature, or unverifiable
//     managed doc is NEVER enforced — it is ignored with a ledger row.
//     Verification runs through the P2 minisign scaffold (detached
//     signature, pinned public key). If the minisign tool is absent the
//     check SKIPs VISIBLY (skip-policy law) and the doc stays ignored —
//     fail closed, never a silent accept.
//   * PRECEDENCE AS A LAYER (L3): the output is an EnterprisePolicy INPUT
//     to the same pure resolver — enterprise never becomes a second brain.
//   * Future schema versions: ignored (never guess), ledger row.
// The `xr://policy` PAGE is P8; this module ships the data contract only.
#pragma once

#include <string>
#include <vector>

#include "policy/core/resolve.h"

namespace xr::policy {

enum class ManagedStatus {
  kAbsent = 0,           // no managed doc at all (normal user profile)
  kVerified,             // signature verified, content applied
  kIgnoredUnsigned,      // doc present, signature file missing
  kIgnoredBadSignature,  // minisign rejected the signature
  kIgnoredInvalid,       // not parseable / bad envelope / bad fields
  kIgnoredFutureVersion, // schema_version newer than this binary knows
  kSkippedNoTool,        // minisign absent: verification impossible => ignored
};

const char* ToString(ManagedStatus s);

struct ManagedResult {
  ManagedStatus status = ManagedStatus::kAbsent;
  bool enforced = false;          // true ONLY for kVerified
  EnterprisePolicy policy;        // valid iff enforced
  std::vector<std::string> ledger_rows;
  std::string detail;
};

// Loads `doc_path` (JSON envelope) verifying the detached signature at
// `doc_path + ".minisign"` against `pubkey_path` when the minisign tool is
// available. Pure I/O + subprocess; no enforcement decisions happen here.
ManagedResult LoadManagedPolicy(const std::string& doc_path, const std::string& pubpath);

// Strict envelope: {"schema":"xr-managed-policy","schema_version":1,
//                   "data":{...EnterprisePolicy fields...}}
// (data fields validated by ParseEnterprise in resolve.cc).
bool ValidateManagedEnvelope(const JsonValue& v, EnterprisePolicy* out, std::string* error);

}  // namespace xr::policy
