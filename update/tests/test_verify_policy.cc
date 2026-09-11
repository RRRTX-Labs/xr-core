// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the verification-policy decision matrix (P10-T1). The
// TLS-INDEPENDENCE LAW is demonstrated here: the policy has no transport
// input, so contradictory transports over the same bytes MUST produce the
// identical verdict (the transport echo exists only at the host layer and
// is ignored). Also: channel/class refusals, epoch kill switch, key
// pinning, and the reason ordering the golden vectors pin.
#include "update/core/verify_policy.h"

#include "harness.h"
#include "test_verifier_testonly.h"
#include "env_helper.h"

using namespace xr::update;
using xrtest_update::BaseResponse;
using xrtest_update::Envelope;

namespace {

ClientState State(const std::string& version, SeenSet* seen,
                  const EpochState& epoch) {
  ClientState s;
  s.channel = "dev";
  s.current_version = version;
  s.epoch = epoch;
  s.seen = seen;
  s.install_id = "local-test-install-id";
  return s;
}

EpochState GoodEpoch() {
  EpochState e;
  e.epoch_id = "epoch-2026-09";
  e.key_id = "xr-root-1";
  e.seq = 3;
  return e;
}

PinnedKeys Keys() {
  PinnedKeys k;
  k.Add("xr-root-1", "ROOT-PUB");
  k.Add("xr-signing-2026-09", "SIGNING-PUB");
  return k;
}

}  // namespace

int main() {
  TestVerifierTestonly verifier;
  const PinnedKeys keys = Keys();
  SeenSet seen("");  // disposable

  // accept
  {
    ClientState s = State("1.0.0.0", &seen, GoodEpoch());
    const VerifyOutcome o = VerifyUpdateResponse(
        Envelope(BaseResponse("1.2.0.0")), s, keys, verifier, "dev");
    XR_EXPECT_MSG(o.verdict == Verdict::kAccept && o.reason == "ok",
                  "newer signed version accepted");
    XR_EXPECT_MSG(!o.manual_path, "no manual path on a clean accept");
    XR_EXPECT_MSG(o.manifest_id.size() == 64, "manifest id recorded");
  }

  // TLS-INDEPENDENCE MATRIX: the verdict function takes no transport data;
  // demonstrate it by feeding byte-identical envelopes through states whose
  // only difference is a notional transport description the policy cannot
  // even see. Bad TLS + good signature must accept; good TLS + bad
  // signature must deny; the deny/accept sets are identical.
  {
    const std::string good = Envelope(BaseResponse("1.2.0.0"));
    const std::string bad_sig = Envelope(BaseResponse("1.2.0.0"),
                                         "epoch-2026-09", "xr-root-1", 3,
                                         "sig:0000000000000000");
    struct Row { const char* name; bool tls_ok; const char* envelope; Verdict v; };
    const Row rows[] = {
        {"bad-TLS + good-sig", false, good.c_str(), Verdict::kAccept},
        {"good-TLS + good-sig", true, good.c_str(), Verdict::kAccept},
        {"good-TLS + bad-sig", true, bad_sig.c_str(), Verdict::kDeny},
        {"bad-TLS + bad-sig", false, bad_sig.c_str(), Verdict::kDeny},
    };
    for (const Row& r : rows) {
      SeenSet s1("");
      ClientState st = State("1.0.0.0", &s1, GoodEpoch());
      // transport_ok is recorded ONLY here in the test; VerifyUpdateResponse
      // has no parameter for it — that absence IS the law under test.
      (void)r.tls_ok;
      const VerifyOutcome o = VerifyUpdateResponse(r.envelope, st, keys,
                                                   verifier, "dev");
      XR_EXPECT_MSG(o.verdict == r.v,
                    std::string("TLS-independence: ") + r.name +
                        (r.v == Verdict::kAccept ? " accepts" : " denies"));
    }
  }

  // server compromise is not sufficient: valid shape, wrong key, bad sig
  {
    SeenSet s1("");
    ClientState s = State("1.0.0.0", &s1, GoodEpoch());
    const VerifyOutcome o = VerifyUpdateResponse(
        Envelope(BaseResponse("1.2.0.0"), "epoch-2026-09", "xr-root-1", 3,
                 "sig:deadbeefdeadbeef"),
        s, keys, verifier, "dev");
    XR_EXPECT_MSG(o.verdict == Verdict::kDeny && o.reason == "signature-invalid",
                  "forged manifest denied");
  }
  {
    SeenSet s1("");
    ClientState s = State("1.0.0.0", &s1, GoodEpoch());
    const VerifyOutcome o = VerifyUpdateResponse(
        Envelope(BaseResponse("1.2.0.0"), "epoch-2026-09", "xr-rogue", 3),
        s, keys, verifier, "dev");
    XR_EXPECT_MSG(o.verdict == Verdict::kDeny &&
                      o.reason == "unknown-signing-key",
                  "unpinned signing key denied");
  }

  // revoked epoch: deny + forced manual path; manual path is sticky
  {
    SeenSet s1("");
    EpochState e = GoodEpoch();
    e.revoked = true;
    e.manual_path = true;
    ClientState s = State("1.0.0.0", &s1, e);
    const VerifyOutcome o = VerifyUpdateResponse(
        Envelope(BaseResponse("1.2.0.0")), s, keys, verifier, "dev");
    XR_EXPECT_MSG(o.verdict == Verdict::kDeny && o.reason == "epoch-revoked" &&
                      o.manual_path,
                  "revoked epoch denies and forces the manual path");
  }

  // TEST-ONLY verifier refuses release channels (typed, tested)
  {
    SeenSet s1("");
    ClientState s = State("1.0.0.0", &s1, GoodEpoch());
    for (const char* ch : {"beta", "stable"}) {
      const VerifyOutcome o = VerifyUpdateResponse(
          Envelope(BaseResponse("1.2.0.0")), s, keys, verifier, ch);
      XR_EXPECT_MSG(o.verdict == Verdict::kDeny &&
                        o.reason == "test-verifier-refused",
                    std::string(ch) + " refuses the test verifier");
    }
    // a refusing verifier refuses everything
    RefusingVerifierTestonly refusing;
    const VerifyOutcome o = VerifyUpdateResponse(
        Envelope(BaseResponse("1.2.0.0")), s, keys, refusing, "dev");
    XR_EXPECT_MSG(o.verdict == Verdict::kDeny &&
                      o.reason == "test-verifier-refused",
                  "a verifier that opts out opts out everywhere");
  }

  // replay: same manifest id refused on the second sight
  {
    SeenSet s1("");
    ClientState s = State("1.0.0.0", &s1, GoodEpoch());
    const std::string env = Envelope(BaseResponse("1.2.0.0"));
    XR_EXPECT_MSG(VerifyUpdateResponse(env, s, keys, verifier, "dev")
                          .verdict == Verdict::kAccept,
                  "first sight accepts");
    // VerifyUpdateResponse recorded the id in s1; re-run against a state
    // that already contains it (fresh version state so monotonicity is not
    // the reason).
    SeenSet s2("");
    (void)s2.Insert(Sha256Hex(xr::update::ParseJson(env).value
                                  .as_object()
                                  .at("response")
                                  .Canonical()),
                    nullptr);
    ClientState s2_state = State("1.0.0.0", &s2, GoodEpoch());
    const VerifyOutcome o = VerifyUpdateResponse(env, s2_state, keys,
                                                 verifier, "dev");
    XR_EXPECT_MSG(o.verdict == Verdict::kDeny && o.reason == "replay-refused",
                  "replayed manifest refused");
  }

  return xrtest::Report("test_verify_policy");
}
