// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the update VERIFICATION POLICY (P10-T1) — every decision the
// client makes about a served manifest, as a total, deny-on-unknown
// function over an INJECTED primitive. What this module is:
//   * which key is pinned, which epoch is live, monotonicity, replay,
//     downgrade refusal — all here, all tested;
//   * TLS-INDEPENDENT BY CONSTRUCTION: there is no transport input. A
//     transport success never substitutes for a signature and vice versa;
//     the host wrapper accepts a (documented, ignored) transport echo in
//     its request args purely so tests can prove the verdict is
//     byte-identical under contradictory transports.
// What this module is NOT: a crypto implementation. The primitive enters
// only through update/core/verifier.h (DR-11; no invented crypto).
#pragma once

#include <string>

#include "update/core/backoff.h"
#include "update/core/epoch.h"
#include "update/core/manifest.h"
#include "update/core/seen.h"
#include "update/core/verifier.h"

namespace xr::update {

// The client's mutable update state (persisted by the host with the P6
// store law; disposable sessions keep it in memory and write zero bytes).
struct ClientState {
  std::string channel;      // dev | nightly-test | beta | stable
  std::string current_version;  // canonical dotted quad ("0.0.0.0" = none)
  EpochState epoch;
  BackoffState backoff;
  SeenSet* seen = nullptr;  // owned by the caller (host wires the store dir)
  std::string install_id;   // LOCAL random id — cohort input, never uploaded
};

// Typed verdicts with stable reason codes (the golden-vector contract,
// docs/contracts/vectors/update-v1.json).
enum class Verdict { kAccept, kDeny };

struct VerifyOutcome {
  Verdict verdict = Verdict::kDeny;
  std::string reason;       // stable wire token (ToString(Reason))
  bool manual_path = false; // kill-switch forced the manual path
  std::string manifest_id;  // accepted: the id recorded into the seen-set
};

namespace reason {
// Stable tokens; never renamed without a golden-vector bump (the vectors
// cite these strings).
constexpr const char* kOk = "ok";
constexpr const char* kMalformed = "malformed-manifest";
constexpr const char* kUnknownField = "unknown-field";
constexpr const char* kWrongProtocol = "wrong-protocol";
constexpr const char* kBadVersion = "non-canonical-version";
constexpr const char* kOversize = "oversize";
constexpr const char* kInsecureUrl = "insecure-url";
constexpr const char* kBadDigest = "bad-digest";
constexpr const char* kUnknownApp = "unknown-appid";
constexpr const char* kNoOffer = "no-update-offered";
constexpr const char* kDowngrade = "downgrade-refused";
constexpr const char* kEqualVersion = "equal-version-reoffer";
constexpr const char* kReplay = "replay-refused";
constexpr const char* kRevokedEpoch = "epoch-revoked";
constexpr const char* kUnknownEpochKey = "unknown-epoch-key";
constexpr const char* kUnknownSigningKey = "unknown-signing-key";
constexpr const char* kMissingSignature = "signature-missing";
constexpr const char* kBadSignature = "signature-invalid";
constexpr const char* kTestVerifierRefused = "test-verifier-refused";
constexpr const char* kSeenPersistFailed = "seen-persist-failed";
constexpr const char* kFlagOff = "feature-flag-off";
}  // namespace reason

// Pinned-key decision: the map from key_id -> key material is DATA supplied
// by the host (release/keys/). The policy only decides whether the id is
// one we pin; it never accepts a key from the wire.
class PinnedKeys {
 public:
  void Add(const std::string& key_id, const std::string& public_key);
  bool Has(const std::string& key_id) const;
  const std::string& Material(const std::string& key_id) const;

 private:
  std::map<std::string, std::string> keys_;
};

// The decision function. Total: every input maps to a typed verdict.
// `expected_epoch_seq` rides in the client state. Order of checks is part
// of the contract (the golden vectors pin it):
//   parse -> oversize -> protocol -> appid -> offer -> version canonical ->
//   epoch acceptability -> signature (key pin, then primitive) ->
//   monotonicity -> replay -> record.
VerifyOutcome VerifyUpdateResponse(const std::string& raw_envelope,
                                   const ClientState& state,
                                   const PinnedKeys& keys,
                                   const SignatureVerifier& verifier,
                                   const std::string& channel);

}  // namespace xr::update
