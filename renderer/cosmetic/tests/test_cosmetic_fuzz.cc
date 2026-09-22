// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the NEVER-ACCEPT fuzz oracle for the cosmetic core (P12-T2). The
// same P6/P8/P10 pattern as update/tests/test_update_fuzz.cc: seeded,
// structure-aware mutations of known-good artifacts fed to the parser/
// compiler/decider modules; the oracle is about INVARIANTS, not coverage:
//
//   1. never-accept (parse/compile path) — a mutated blob or key-set may
//      never parse AND compile unless it is byte-identical to a known-good
//      canonical form. A mutation that survives both layers accepted means
//      the strictness laws (schema const, unknown-field deny, digest-verified,
//      closed vocabularies) are not actually enforced.
//   2. closed-vocabulary — every reason string the modules emit on refusal
//      MUST equal a name from the closed name() tables. A refusal whose text
//      the tables do not know is a new de facto API surface.
//   3. key-refusal — a non-empty embedder_site MUST refuse scope-key
//      derivation (the "key on the frame site only" law).
//   4. degrade total — every DegradeCondition maps to an outcome with a
//      non-empty reason passthrough grammar parity (table stays exhaustive).
//
// Wall-clock timebox per the P6/P8 pattern: 30 s unit budget; the >=600 s
// seeded campaign runs as XR_FUZZ_SECONDS=600 with the transcript recorded
// in evidence. XR_FUZZ_MIN_ITERS keeps a short timebox from certifying
// nothing.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "harness.h"
#include "renderer/cosmetic/abpf/abpf.h"
#include "renderer/cosmetic/core/blob.h"
#include "renderer/cosmetic/core/degrade.h"
#include "renderer/cosmetic/core/keyset.h"
#include "renderer/cosmetic/core/scope_key.h"
#include "renderer/cosmetic/core/selector.h"
#include "renderer/cosmetic/core/style.h"

namespace xrc = xr::cosmetic;

namespace {

constexpr uint32_t kSeed = 20260919;

std::string RandomJunk(std::mt19937& rng, size_t max_len) {
  static const char kAlnum[] = "abcxyz01.-_{}[]\":, \n<>~^*=/";
  const size_t len = 1 + (rng() % max_len);
  std::string s;
  for (size_t i = 0; i < len; ++i) s += kAlnum[rng() % (sizeof(kAlnum) - 1)];
  return s;
}

// Mutate a raw string at a random point: flip/insert/delete.
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

// One known-good blob: built through the module's own serializer so the
// digest is correct by construction.
xrc::CosmeticBlob MakeBlob() {
  xrc::CosmeticBlob b;
  b.blob_id = "fuzz-blob-1";
  b.generated_epoch = 1758000000;
  b.scope.site = "example.test";
  b.scope.identity_class = "anonymous";
  xrc::BlobRule r1;
  r1.id = "r1";
  r1.selector = "div > .ad";
  r1.action = "hide";
  xrc::BlobRule r2;
  r2.id = "r2";
  r2.selector = ".sponsor";
  r2.action = "remove";
  b.rules = {r1, r2};
  xrc::BlobRefusal f;
  f.rule_index = 9;
  f.reason = "unknown-pseudo-class";
  b.refusals = {f};
  b.sha256 = xrc::BlobDigest(b);
  return b;
}

xrc::InputRule MakeRule(size_t i) {
  xrc::InputRule r;
  r.id = "c" + std::to_string(i);
  r.selector = (i % 2 == 0) ? "div > .ad" : ".sponsor";
  r.action = (i % 5 == 3) ? "remove" : "hide";
  if ((i % 7) == 0) r.style = {{"display", "none"}};
  if ((i % 11) == 0) r.exception_sites = {"s1.example", "s2.example"};
  return r;
}

// Known-good key-set rule set.
std::vector<xrc::InputRule> GoodRules(size_t n) {
  std::vector<xrc::InputRule> out;
  for (size_t i = 0; i < n; ++i) out.push_back(MakeRule(i));
  return out;
}

bool KnownBlob(const std::string& canonical,
               const std::vector<std::string>& pool) {
  for (const std::string& p : pool) if (p == canonical) return true;
  return false;
}

bool KnownRefusalReason(const std::string& reason) {
  // The closed refusal vocabulary = every name() the modules can emit. If the
  // corpus ever produces a reason not in this set, the oracle fails: a new
  // reason is a new de facto contract item nobody signed. Unions the blob/
  // selector table with the key-set, style, scope and blob error tables.
  if (reason.empty() || xrc::KnownRefusalReasons().count(reason)) return true;
  for (int e = 0; e <= static_cast<int>(xrc::KeySetError::kDuplicateId); ++e) {
    if (reason == xrc::KeySetErrorName(static_cast<xrc::KeySetError>(e)))
      return true;
  }
  for (int e = 0; e <= static_cast<int>(xrc::StyleError::kNonAsciiControl);
       ++e) {
    if (reason == xrc::StyleErrorName(static_cast<xrc::StyleError>(e)))
      return true;
  }
  for (int e = 0; e <= static_cast<int>(xrc::ScopeError::kEmptyDocumentClass);
       ++e) {
    if (reason == xrc::ScopeErrorName(static_cast<xrc::ScopeError>(e)))
      return true;
  }
  for (int e = 0; e <= static_cast<int>(xrc::BlobError::kEmptyBlobId); ++e) {
    if (reason == xrc::BlobErrorName(static_cast<xrc::BlobError>(e)))
      return true;
  }
  return false;
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
  const std::vector<std::string> blob_pool = {
      xrc::BlobToCanonicalJson(MakeBlob())};

  long long iters = 0, accepts = 0, refusals = 0, violations = 0;

  while (true) {
    if (deadline > 0 && (iters & 0x3FF) == 0) {
      const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
      if (now >= deadline && iters >= min_iters) break;
    }
    ++iters;

    const uint32_t roll = rng() % 12;
    if (roll < 3) {
      // THE ROUND-TRIP path: an unmutated blob must parse and compile.
      xrc::BlobResult out;
      xrc::BlobError e = xrc::ParseBlob(blob_pool[0], nullptr, &out);
      if (e == xrc::BlobError::kOk && out.valid && out.key_set.valid) {
        ++accepts;
        XR_EXPECT_MSG(out.key_set.bytes > 0, "round-trip key set must be real");
      } else {
        ++violations;
        std::fprintf(stderr,
                     "NEVER-ACCEPT VIOLATION at iter %lld: known-good blob "
                     "refused (%s)\n",
                     iters, xrc::BlobErrorName(e));
      }
    } else if (roll < 8) {
      // The mutation path: a mutated blob may REFUSE, or may accept ONLY if
      // its canonical bytes are byte-identical to a pool entry (never a
      // partially applied mutation).
      const std::string doc = Mutate(blob_pool[0], rng);
      xrc::BlobResult out;
      xrc::BlobError e = xrc::ParseBlob(doc, nullptr, &out);
      if (e == xrc::BlobError::kOk && out.valid) {
        if (KnownBlob(xrc::BlobToCanonicalJson(out.blob), blob_pool)) {
          ++accepts;
        } else {
          ++violations;
          std::fprintf(stderr,
                       "NEVER-ACCEPT VIOLATION at iter %lld: mutated blob "
                       "accepted with changed canonical bytes (%s)\n",
                       iters, xrc::BlobErrorName(e));
        }
      } else {
        ++refusals;
        if (!KnownRefusalReason(out.reason)) {
          ++violations;
          std::fprintf(stderr,
                       "CLOSED-VOCAB VIOLATION at iter %lld: refusal reason "
                       "%s is not in the closed tables\n",
                       iters, out.reason.c_str());
        }
      }
    } else {
      // The key-refusal + compile path: derived scopes must refuse the
      // embedder, and compiled rule sets must bound their byte accounting.
      xrc::ScopeInput in;
      in.frame_site = "frame.example";
      in.frame_identity = "xr:00000000-0000-0000-0000-000000000000";
      in.trust = xrc::IdentityTrust::kAuthenticated;
      in.document_url_class = "article";
      const std::string embed = (rng() % 2) ? "top.example" : "";
      xrc::ScopeKey key;
      xrc::ScopeError se = xrc::DeriveScopeKey(in, embed, &key);
      const bool embed_refused =
          (se == xrc::ScopeError::kMalformedSite && !key.valid);
      if (!embed.empty() ? !embed_refused : (se != xrc::ScopeError::kOk)) {
        ++violations;
        std::fprintf(stderr,
                     "KEY-REFUSAL VIOLATION at iter %lld: embedder_site=%s "
                     "err=%s\n",
                     iters, embed.c_str(), xrc::ScopeErrorName(se));
      }

      xrc::KeySetResult ks;
      xrc::KeySetError ke = xrc::CompileKeySet(GoodRules(8), &ks);
      if (ke == xrc::KeySetError::kOk) {
        if (ks.bytes == 0 || ks.compiled_selectors == 0) {
          ++violations;
          std::fprintf(stderr,
                       "BYTE-ACCOUNTING VIOLATION at iter %lld: accepted key "
                       "set reports zero bytes/selectors\n",
                       iters);
        }
      } else {
        ++refusals;
        if (!KnownRefusalReason(ks.reason)) {
          ++violations;
          std::fprintf(stderr,
                       "CLOSED-VOCAB VIOLATION at iter %lld: key-set refusal "
                       "%s is not in the closed tables\n",
                       iters, ks.reason.c_str());
        }
      }
    }
    if (violations > 5) break;
    if (deadline == 0 && iters >= min_iters) break;
  }

  // The degrade truth table stays exhaustive and non-empty: every condition
  // answered, and the table's floor (kMinDegradeRows) is structural, not a
  // number we assert faith in. Exercised every run so a lost row fails here.
  for (int c = 0; c <= static_cast<int>(xrc::DegradeCondition::kGenericSetOnly);
       ++c) {
    const xrc::DegradeRow& row =
        xrc::LookupDegrade(static_cast<xrc::DegradeCondition>(c));
    XR_EXPECT_MSG(row.page_effect != nullptr && row.page_effect[0] != '\0',
                  "degrade row without a page_effect");
  }
  XR_EXPECT_MSG(xrc::DegradeTable().size() >= xrc::kMinDegradeRows,
                "degrade table fell below its structural floor");

  XR_EXPECT_MSG(violations == 0,
                "never-accept oracle violated " + std::to_string(violations) +
                    " time(s) over " + std::to_string(iters) + " iters");
  XR_EXPECT_MSG(iters >= min_iters,
                "min-iters floor reached (" + std::to_string(iters) + " < " +
                    std::to_string(min_iters) + ")");
  XR_EXPECT_MSG(refusals > 0, "the campaign actually exercised refusals");

  std::printf("fuzz: %lld iters, %lld accepts, %lld refusals, %lld violations "
              "(seed %u, budget %lld s)\n",
              iters, accepts, refusals, violations, kSeed, budget_s);
  return xrtest::Report("test_cosmetic_fuzz");
}
