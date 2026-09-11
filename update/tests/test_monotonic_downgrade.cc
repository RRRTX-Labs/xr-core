// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: monotonicity is enforced in the CLIENT, not negotiated (security
// req 3): every version <= the recorded version is refused (with distinct
// reasons for equal re-offer vs downgrade), regardless of signature
// validity. A revocation does NOT open an auto-downgrade path — it forces
// the manual path (tested here).
#include "update/core/verify_policy.h"

#include "env_helper.h"
#include "harness.h"
#include "test_verifier_testonly.h"

using namespace xr::update;
using xrtest_update::BaseResponse;
using xrtest_update::Envelope;

namespace {
ClientState State(const std::string& version, SeenSet* seen) {
  ClientState s;
  s.channel = "dev";
  s.current_version = version;
  s.epoch = EpochState{"epoch-2026-09", "xr-root-1", 3, false, false};
  s.seen = seen;
  return s;
}
EpochState RevokedEpoch() {
  return EpochState{"epoch-2026-09", "xr-root-1", 5, true, true};
}
}  // namespace

int main() {
  TestVerifierTestonly verifier;
  PinnedKeys keys;
  keys.Add("xr-root-1", "ROOT-PUB");
  keys.Add("xr-signing-2026-09", "SIGNING-PUB");

  struct Row {
    const char* current;
    const char* offered;
    const char* reason;
  };
  const Row rows[] = {
      {"1.0.0.0", "1.0.0.0", "equal-version-reoffer"},
      {"1.0.0.0", "0.9.9.9", "downgrade-refused"},
      {"2.0.0.0", "1.9.9.9", "downgrade-refused"},
      {"1.0.1.0", "1.0.0.9", "downgrade-refused"},
  };
  for (const Row& r : rows) {
    SeenSet seen("");
    const VerifyOutcome o = VerifyUpdateResponse(
        Envelope(BaseResponse(r.offered)), State(r.current, &seen), keys,
        verifier, "dev");
    XR_EXPECT_MSG(o.verdict == Verdict::kDeny && o.reason == r.reason,
                  std::string("current ") + r.current + " vs offered " +
                      r.offered + " -> " + r.reason);
  }

  // even a perfectly signed offer cannot cross a revocation downward:
  // after revocation the epoch itself is refused (manual path), so the
  // only road is the manual download.
  {
    SeenSet seen("");
    ClientState s = State("2.0.0.0", &seen);
    s.epoch = RevokedEpoch();
    const VerifyOutcome o = VerifyUpdateResponse(
        Envelope(BaseResponse("1.0.0.0")), s, keys, verifier, "dev");
    XR_EXPECT_MSG(o.verdict == Verdict::kDeny && o.reason == "epoch-revoked" &&
                      o.manual_path,
                  "revocation leaves no auto-downgrade road");
  }

  return xrtest::Report("test_monotonic_downgrade");
}
