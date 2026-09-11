// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// The decision core. Deny is the default; every early return is a typed
// reason. Nothing here reads a clock, a socket, or the filesystem.
#include "update/core/verify_policy.h"

#include <map>
#include <utility>

namespace xr::update {

void PinnedKeys::Add(const std::string& key_id, const std::string& public_key) {
  keys_[key_id] = public_key;
}
bool PinnedKeys::Has(const std::string& key_id) const {
  return keys_.count(key_id) != 0;
}
const std::string& PinnedKeys::Material(const std::string& key_id) const {
  static const std::string kEmpty;
  auto it = keys_.find(key_id);
  return it == keys_.end() ? kEmpty : it->second;
}

namespace {

VerifyOutcome Deny(const char* r, bool manual = false) {
  VerifyOutcome o;
  o.verdict = Verdict::kDeny;
  o.reason = r;
  o.manual_path = manual;
  return o;
}

bool VersionLess(const Version& a, const Version& b) { return a < b; }

}  // namespace

VerifyOutcome VerifyUpdateResponse(const std::string& raw_envelope,
                                   const ClientState& state,
                                   const PinnedKeys& keys,
                                   const SignatureVerifier& verifier,
                                   const std::string& channel) {
  // The TEST-ONLY verifier refuses release channels; the refusal is typed
  // and tested (security req 7: TEST-ONLY cannot leak into production).
  if (!verifier.AllowedForChannel(channel)) {
    return Deny(reason::kTestVerifierRefused);
  }

  UpdateEnvelope env;
  const ParseResult pr = ParseEnvelope(raw_envelope, &env);
  switch (pr) {
    case ParseResult::kOk: break;
    case ParseResult::kMalformed: return Deny(reason::kMalformed);
    case ParseResult::kUnknownField: return Deny(reason::kUnknownField);
    case ParseResult::kWrongProtocol: return Deny(reason::kWrongProtocol);
    case ParseResult::kBadVersion: return Deny(reason::kBadVersion);
    case ParseResult::kOversize: return Deny(reason::kOversize);
    case ParseResult::kInsecureUrl: return Deny(reason::kInsecureUrl);
    case ParseResult::kBadDigest: return Deny(reason::kBadDigest);
  }

  if (env.app_id != "labs.rrrtx.xr") return Deny(reason::kUnknownApp);
  if (env.updatecheck_status != "ok") return Deny(reason::kNoOffer);

  // Epoch acceptability (kill switch): unknown/revoked/regressed epoch is
  // refused BEFORE any signature is consulted, and a revocation the client
  // has recorded forces the manual path permanently.
  if (!EpochAcceptable(state.epoch, env.epoch_id, env.epoch_key_id,
                       env.epoch_seq)) {
    return Deny(reason::kRevokedEpoch, state.epoch.manual_path);
  }
  // The manifest's signing key must be the epoch's PINNED key: a manifest
  // claiming our epoch under any other key is a key-substitution attempt
  // (reported as unknown-signing-key — same deny, more precise reason).
  if (!state.epoch.key_id.empty() && env.epoch_key_id != state.epoch.key_id) {
    return Deny(reason::kUnknownSigningKey);
  }
  if (env.sig_key_id != env.epoch_key_id || !keys.Has(env.sig_key_id)) {
    return Deny(reason::kUnknownSigningKey);
  }
  // Signature over the canonical response bytes — TLS-independent by
  // construction: no transport datum exists in this frame.
  if (env.sig_value.empty()) return Deny(reason::kMissingSignature);
  if (!verifier.Verify(keys.Material(env.sig_key_id), env.canonical_response,
                       env.sig_value)) {
    return Deny(reason::kBadSignature);
  }

  // Monotonicity is enforced HERE, not negotiated. An equal version is a
  // re-offer (deny, distinct reason); anything older is a downgrade —
  // refused unless a VALID revocation-forced-rollback instruction says
  // otherwise, which in v0 only a signed revocation notice can cause (and
  // that path forces the manual update flow rather than auto-installing).
  Version current;
  if (!Version::Parse(state.current_version.empty() ? "0.0.0.0"
                                                    : state.current_version,
                      &current)) {
    return Deny(reason::kBadVersion);
  }
  if (env.version == current) return Deny(reason::kEqualVersion);
  if (VersionLess(env.version, current)) return Deny(reason::kDowngrade);

  // Replay: a previously-seen manifest id is refused.
  if (state.seen && state.seen->Contains(env.manifest_id)) {
    return Deny(reason::kReplay);
  }

  // Record BEFORE accepting: an update we could not record is refused (the
  // next process would otherwise accept the replay we just saw).
  if (state.seen) {
    std::string err;
    if (!state.seen->Insert(env.manifest_id, &err)) {
      return Deny(reason::kSeenPersistFailed);
    }
  }

  VerifyOutcome ok;
  ok.verdict = Verdict::kAccept;
  ok.reason = reason::kOk;
  ok.manifest_id = env.manifest_id;
  return ok;
}

}  // namespace xr::update
