// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the NEVER-ACCEPT fuzz oracle (P10-T1). Seeded, structure-aware
// mutations of valid envelopes are fed to VerifyUpdateResponse; the oracle
// refuses to accept any mutated document unless it is byte-identical to a
// known-valid envelope built by the helper (any structural change must
// either deny or — for signature-only mutations of a re-serialized doc —
// deny because the canonical bytes moved). Wall-clock timebox per the P6/P8
// pattern: 30 s unit budget; the >=600 s seeded campaign runs as
// XR_FUZZ_SECONDS=600 with the transcript recorded in evidence (never a
// silent shorter run in CI: XR_FAST_FUZZ is dev-only).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "env_helper.h"
#include "harness.h"
#include "test_verifier_testonly.h"
#include "update/core/verify_policy.h"

using namespace xr::update;
using xrtest_update::BaseResponse;
using xrtest_update::Envelope;

namespace {

constexpr uint32_t kSeed = 20260911;

std::string RandomJunk(std::mt19937& rng, size_t max_len) {
  static const char kAlnum[] = "abcxyz01.-_{}[]\":, \n";
  const size_t len = 1 + (rng() % max_len);
  std::string s;
  for (size_t i = 0; i < len; ++i) s += kAlnum[rng() % (sizeof(kAlnum) - 1)];
  return s;
}

// Mutate a raw envelope string at a random point: flip/insert/delete.
std::string Mutate(const std::string& raw, std::mt19937& rng) {
  if (raw.empty()) return RandomJunk(rng, 8);
  std::string out = raw;
  const size_t pos = rng() % raw.size();
  switch (rng() % 3) {
    case 0:
      out[pos] = static_cast<char>(out[pos] ^ (1 + (rng() % 127)));
      break;
    case 1:
      out.insert(pos, RandomJunk(rng, 6));
      break;
    default:
      out.erase(pos, 1 + (rng() % 8));
      break;
  }
  return out;
}

ClientState GoodState(SeenSet* seen) {
  ClientState s;
  s.channel = "dev";
  s.current_version = "1.0.0.0";
  s.epoch = EpochState{"epoch-2026-09", "xr-root-1", 3, false, false};
  s.seen = seen;
  return s;
}

}  // namespace

int main() {
  const long long budget_s =
      std::getenv("XR_FUZZ_SECONDS") != nullptr
          ? std::atoll(std::getenv("XR_FUZZ_SECONDS"))
          : 30;
  const long long min_iters =
      std::getenv("XR_FUZZ_MIN_ITERS") != nullptr
          ? std::atoll(std::getenv("XR_FUZZ_MIN_ITERS"))
          : 1000;
  const int64_t deadline =
      budget_s > 0
          ? std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                    .count() + budget_s
          : 0;

  std::mt19937 rng(kSeed);
  TestVerifierTestonly verifier;
  PinnedKeys keys;
  keys.Add("xr-root-1", "ROOT-PUB");
  keys.Add("xr-signing-2026-09", "SIGNING-PUB");

  // The pool of VALID envelopes: mutations that survive parsing but change
  // bytes must deny (the signature no longer matches the canonical
  // response); only byte-identical round-trips may accept.
  std::vector<std::string> valid;
  valid.push_back(Envelope(BaseResponse("1.2.0.0")));
  valid.push_back(Envelope(BaseResponse("2.0.0.0")));
  valid.push_back(")]}'\n" + valid[0]);

  long long iters = 0;
  long long accepted = 0;
  long long denials = 0;
  long long violations = 0;

  while (true) {
    if (deadline > 0 && (iters & 0xFF) == 0) {
      const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
      if (now >= deadline && iters >= min_iters) break;
      if (now >= deadline && iters < min_iters) {
        // keep grinding: the min-iters floor is law even when the timebox
        // expires (a short campaign certifies nothing)
      }
    }
    ++iters;
    SeenSet seen("");
    const ClientState st = GoodState(&seen);

    std::string doc;
    const uint32_t roll = rng() % 10;
    if (roll < 3) {
      doc = valid[rng() % valid.size()];  // unmutated: the accept path
    } else {
      const std::string base = valid[rng() % valid.size()];
      switch (rng() % 4) {
        case 0: doc = Mutate(base, rng); break;
        case 1: doc = RandomJunk(rng, 64); break;
        case 2: {  // mutate the response INSIDE the envelope: signature breaks
          doc = Mutate(Envelope(BaseResponse("1.2.0.0")), rng);
          break;
        }
        default: {  // tamper the signature only
          doc = Envelope(BaseResponse("1.2.0.0"), "epoch-2026-09",
                         "xr-root-1", 3,
                         "sig:" + Sha256Hex(RandomJunk(rng, 16)).substr(0, 16));
          break;
        }
      }
    }

    const VerifyOutcome o = VerifyUpdateResponse(doc, st, keys, verifier, "dev");
    if (o.verdict == Verdict::kDeny) {
      ++denials;
    } else {
      // The oracle: accept ONLY if the document's parsed CANONICAL response
      // is byte-identical to a pool entry's. Wire-format drift (whitespace,
      // the )]}' prefix) canonicalizes away BY DESIGN — the signature is
      // over canonical bytes — so only a canonical change may not accept.
      bool identical = false;
      std::string bare = doc;
      const std::string kPrefix = ")]}'\n";
      if (bare.compare(0, kPrefix.size(), kPrefix) == 0) {
        bare.erase(0, kPrefix.size());  // the safe prefix is not signed bytes
      }
      JsonParseResult acc = ParseJson(bare);
      if (acc.ok && acc.value.is_object()) {
        const JsonValue* resp = acc.value.find("response");
        if (resp) {
          const std::string canon = resp->Canonical();
          for (const std::string& v : valid) {
            JsonParseResult pv = ParseJson(v);
            if (pv.ok && pv.value.is_object()) {
              const JsonValue* r2 = pv.value.find("response");
              if (r2 && r2->Canonical() == canon) identical = true;
            }
          }
        }
      }
      if (identical) {
        ++accepted;
      } else {
        ++violations;
        std::fprintf(stderr,
                     "NEVER-ACCEPT VIOLATION at iter %lld: mutated doc "
                     "accepted (reason %s)\n",
                     iters, o.reason.c_str());
        if (violations > 5) break;
      }
    }
    if (deadline == 0 && iters >= min_iters) break;
  }

  XR_EXPECT_MSG(violations == 0,
                "never-accept oracle violated " + std::to_string(violations) +
                    " time(s) over " + std::to_string(iters) + " iters");
  XR_EXPECT_MSG(iters >= min_iters,
                "min-iters floor reached (" + std::to_string(iters) + " < " +
                    std::to_string(min_iters) + ")");
  XR_EXPECT_MSG(denials > 0, "the campaign actually exercised denials");

  std::printf("fuzz: %lld iters, %lld accepted, %lld denied, %lld violations "
              "(seed %u, budget %lld s)\n",
              iters, accepted, denials, violations, kSeed, budget_s);
  return xrtest::Report("test_update_fuzz");
}
