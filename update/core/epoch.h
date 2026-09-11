// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: epoch state — the kill-switch half of the update channel threat
// model (docs/threat-model.md row 14). An epoch is a named, seq-numbered
// signing generation. The client refuses manifests from an unknown/revoked
// epoch and NEVER auto-downgrades past a revocation: a revoked epoch forces
// the manual-update path until a human acts. That transition is data + a
// test, not a paragraph.
#pragma once

#include <string>

#include "update/core/json.h"

namespace xr::update {

struct EpochState {
  std::string epoch_id;   // currently trusted epoch ("" = none yet)
  std::string key_id;     // pinned signing key for that epoch
  long long seq = -1;     // monotonic epoch sequence; -1 = fresh install
  bool revoked = false;   // a revocation notice for THIS or a later seq landed
  bool manual_path = false;  // revocation forces the manual-update path
};

// A signed revocation notice (living contract xr-epoch-revocation-v1):
//   {"schema":"xr-epoch-revocation","schema_version":1,
//    "epoch_id":str,"key_id":str,"seq":int>=0,"reason":str}
// The canonical bytes of this object are the message the root key signs;
// the signature travels in the envelope's `signature` block (verified by
// the caller through the same injected primitive — no crypto here).
struct RevocationNotice {
  std::string epoch_id;
  std::string key_id;
  long long seq = -1;
  std::string reason;
  std::string canonical_body;  // the signed bytes
};

enum class NoticeResult {
  kOk,
  kMalformed,
  kUnknownField,
};

NoticeResult ParseRevocationNotice(const std::string& raw, RevocationNotice* out);

// Decide the epoch consequence of `notice` for `state`. Verification of the
// notice signature is the CALLER's job (it uses the pinned root key through
// the injected verifier); this function is the pure policy:
//   * notice.seq <  state.seq                      -> stale notice, ignored
//   * notice.seq >= state.seq, matching epoch/key  -> epoch revoked, the
//     manual path is forced and cannot be cleared by any manifest.
NoticeResult ApplyRevocation(const RevocationNotice& notice, EpochState* state,
                             bool* notice_applied);

// True when the envelope's epoch block is acceptable for `state`:
// known epoch id + seq >= the client's seq + matching pinned key.
// (Signature validity is checked separately by the verify policy.)
bool EpochAcceptable(const EpochState& state, const std::string& epoch_id,
                     const std::string& key_id, long long seq);

JsonValue EpochToJson(const EpochState& state);
void EpochFromJson(const JsonValue& v, EpochState* out);

}  // namespace xr::update
