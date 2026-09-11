// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/engine — the INJECTED blocking-engine interface
// (P11-T2). The decision core never embeds an engine: match.cc talks to a
// BlockingEngine, and which one is bound is a deployment decision —
//   * FakeEngine (shield/core/fake_engine.{h,cc}): the deterministic,
//     std-only table engine the local suites and the Python fake mirror
//     byte-for-byte (golden vectors pin BOTH backends);
//   * the Rust FFI shim (shield/engine/): adblock-rust behind this same
//     interface, compiled on the hosted/farm lanes (never claimed built
//     where cargo is absent — runner-capabilities ledger law).
// Engine DEATH is representable and total: alive() flips false, Match
// stops being called, and posture.cc turns that into fail-OPEN browsing
// with an amber chip (never a crash, never a silent allow-with-green).
#pragma once

#include <string>

namespace xr::shield {

struct RequestContext;  // context.h

// Engine-level actions (list-driven). mojom BlockAction's kUpgraded is a
// network-layer action (HTTP->HTTPS) that no list rule produces in v1 —
// it is deliberately absent here and documented as such.
enum class Action {
  kBlock = 0,
  kAllow = 1,      // exception rule (@@) matched: observed-but-allowed
  kRedirect = 2,   // $redirect=<resource>
  kReplace = 3,    // resource replacement (redirect resource with body)
};

const char* ActionName(Action a);  // closed vocabulary: block|allow|redirect|replace

struct EngineHit {
  Action action = Action::kBlock;
  std::string rule_id;            // the matched rule's bundle-local id
  std::string list_id;            // which list inside the bundle
  std::string redirect_resource;  // set for kRedirect/kReplace, else ""
};

class BlockingEngine {
 public:
  virtual ~BlockingEngine() = default;

  // False once the engine has died (panic-equivalent, poisoned state, OOM
  // during apply — the caller simulates via FakeEngine's kill switch or the
  // FFI shim's process/liveness check). When false, Match MUST NOT be
  // called; posture owns the consequence (fail-open, amber).
  virtual bool alive() const = 0;

  // True when a rule matched (`hit` filled); false = no opinion (the
  // decision core then ALLOWs with why_code no-match — allow-by-default is
  // the engine contract; blocking is always rule-driven).
  // Total: never throws, never blocks, bounded work per call.
  virtual bool Match(const RequestContext& ctx, EngineHit* hit) = 0;
};

}  // namespace xr::shield
