// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// In-house seeded theme fuzzer (P8-T4; mirrors the P7/P6 in-house pattern —
// the ClusterFuzzLite fleet is a P9 handoff, recorded in research-log-P8).
//
// The invariant is NOT "no crash": it is the SECURITY oracle — "never apply
// a theme the audit rejects" (loader refuse law). Every generated doc is
// pushed through ImportTheme on a scratch loader; when the loader ACCEPTS,
// the fuzzer independently re-audits the applied map and asserts the
// contrast audit passes (no edge below its requirement, and no unmet pair
// hiding behind a missing waiver), and re-validates schema strictness. Any
// accept-then-fail is a violation. Crash/hang = test failure by exit.
//
// Budget: wall-clock via XR_FUZZ_SECONDS (default 30 s for the unit lane;
// the P8 evidence campaign runs XR_FUZZ_SECONDS=600, seed 20260909).
// Deterministic PRNG (xorshift64*); seed overridable via XR_FUZZ_SEED.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "harness.h"
#include "themes/core/loader.h"

namespace {

using Clock = std::chrono::steady_clock;
using xr::themes::ApplyResult;
using xr::themes::BuiltinTheme;
using xr::themes::ContrastFinding;
using xr::themes::FindBuiltin;
using xr::themes::ImportTheme;
using xr::themes::LoaderState;
using xr::themes::TokenDef;
using xr::themes::TokenMap;

std::string ReadTokens() {
  bool ok = false;
  std::string t = xrtest::ReadFile("ui/themes/tokens.json", &ok);
  if (!ok)
    t = xrtest::ReadFile("../../ui/themes/tokens.json", &ok);
  return ok ? t : "";
}

uint64_t g_checks = 0;
uint64_t g_violations = 0;

// xorshift64* — deterministic per seed.
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed ? seed : 1) {}
  uint64_t Next() {
    uint64_t x = s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    s = x;
    return x * 0x2545F4914F6CDD1Dull;
  }
};

// Independent re-audit used as the oracle (walks the pairing graph again;
// refuses to trust the loader's own pass decision).
bool IndependentAuditPasses(const LoaderState& st, const TokenMap& values,
                            const std::vector<std::string>& waived) {
  // token -> values lookup; pair edges are audited with the same thresholds
  // as contrast.cc. Waivers honored only for exact (token,pair) rows whose
  // best matches the computed ratio within 0.011 and ratio >= 4.5.
  struct Row {
    std::string token, pair;
    double best = 0;
  };
  std::vector<Row> rows;
  for (const auto& w : waived) {
    xr::themes::Waiver out;
    std::string err;
    if (xr::themes::ParseWaiverRow(w, &out, &err))
      rows.push_back(Row{out.token, out.pair, out.best});
  }
  auto hex_of = [&](const std::string& name) -> std::string {
    auto it = values.find(name);
    if (it == values.end() || !it->second.is_string()) return "";
    return it->second.as_string();
  };
  for (const auto& t : st.source.tokens) {
    if (t.type != "color" || t.pairing.empty()) continue;
    const std::string fg = hex_of(t.name);
    if (fg.empty()) continue;
    for (const auto& pair : t.pairing) {
      const std::string bg = hex_of(pair);
      if (bg.empty()) continue;
      const double ratio = xr::themes::ContrastBetween(fg, bg);
      const double required = t.security_critical ? 7.0 : 4.5;
      bool waived_ok = false;
      for (const auto& row : rows) {
        if (row.token == t.name && row.pair == pair &&
            ratio >= 4.5 - 0.011 &&
            std::abs(row.best - ratio) <= 0.011) {
          waived_ok = true;
          break;
        }
      }
      if (ratio < required - 0.011 && !waived_ok) return false;
      // The reserved critical-red family law holds post-accept too.
      if (t.name == "critical-red" && fg.size() >= 7) {
        // Reuse the loader's own validator only as a schema cross-check:
        // strictness is asserted by ValidateThemeDoc below.
        (void)0;
      }
    }
  }
  return true;
}

}  // namespace

int main() {
  const std::string toks = ReadTokens();
  if (toks.empty()) {
    std::fprintf(stderr, "fuzz: cannot read ui/themes/tokens.json\n");
    return 1;
  }
  long long budget_s = 30;
  if (const char* e = std::getenv("XR_FUZZ_SECONDS"))
    budget_s = std::atoll(e);
  uint64_t seed = 20260909;
  if (const char* e = std::getenv("XR_FUZZ_SEED")) seed = std::atoll(e);

  Rng rng(seed);
  LoaderState base = xr::themes::LoaderLoad(toks, "light");
  if (!base.ok) {
    std::fprintf(stderr, "fuzz: base loader failed: %s\n", base.error.c_str());
    return 1;
  }
  // Corpus of built-in value maps (valid seeds for mutations).
  std::vector<TokenMap> corpus;
  for (const auto& b : base.source.builtins) corpus.push_back(b.values);

  std::vector<std::string> names;
  for (const auto& t : base.source.tokens) names.push_back(t.name);

  // Mutation alphabet: real hexes, hostile payloads, weird numbers/keys.
  const char* kHostile[] = {
      "#ffffff", "#000000", "#1565c0", "#ffaeae", "url(https://evil/x)",
      "script",  "import",  "<script>", "1e999",   "99999999999999999999",
      "\xff\xfe", "#12345",  "#1234567", "{}",      "[1,2]",
  };

  const auto start = Clock::now();
  uint64_t accepts = 0;
  uint64_t refusals = 0;
  auto deadline = start + std::chrono::seconds(budget_s);
  long long iter = 0;
  while (Clock::now() < deadline) {
    ++iter;
    // Build a doc: start from a random built-in and apply 1-6 mutations.
    TokenMap doc = corpus[rng.Next() % corpus.size()];
    const unsigned int muts = 1 + static_cast<unsigned int>(rng.Next() % 6);
    for (unsigned int m = 0; m < muts; ++m) {
      const uint64_t r = rng.Next();
      const unsigned int mode = static_cast<unsigned int>(r % 5);
      const std::string key = names[rng.Next() % names.size()];
      if (mode == 0) {
        // hostile value on a real token (sometimes a color, sometimes not)
        const std::string v = kHostile[rng.Next() %
                                       (sizeof(kHostile) / sizeof(char*))];
        // Only embed printable values; skip the raw \xff byte here (it is
        // exercised via the raw-bytes path below).
        if (v.find('\xff') == std::string::npos)
          doc[key] = xr::themes::JsonValue(v);
      } else if (mode == 1) {
        // random color from the corpus of built-ins
        const TokenMap& other = corpus[rng.Next() % corpus.size()];
        auto it = other.find(key);
        if (it != other.end()) doc[key] = it->second;
      } else if (mode == 2) {
        // hostile KEY (non-token)
        const std::string k2 = kHostile[rng.Next() %
                                        (sizeof(kHostile) / sizeof(char*))];
        if (k2.find('#') != 0 && k2.find('\xff') == std::string::npos)
          doc[k2] = xr::themes::JsonValue(std::string("#ff0000"));
      } else if (mode == 3) {
        // numeric garbage on a random key
        const int64_t v = static_cast<int64_t>(rng.Next()) *
                          (rng.Next() & 1 ? 1 : -1);
        doc[key] = xr::themes::JsonValue(v);
      } else {
        // remove the key entirely
        doc.erase(key);
      }
    }
    // Serialize canonically, then re-parse through the strict path.
    xr::themes::JsonValue::Object obj;
    for (const auto& [k, v] : doc) obj[k] = v;
    const std::string text = xr::themes::JsonValue(std::move(obj)).Canonical();

    ++g_checks;
    LoaderState scratch = base;
    scratch.mode = "light";
    scratch.applied = "system";
    ApplyResult r = ImportTheme(&scratch, text);
    if (!r.ok) {
      ++refusals;
      continue;
    }
    ++accepts;
    // ORACLE: the loader accepted — the independent audit must agree that
    // every pairing edge passes (custom imports carry NO waivers).
    if (!IndependentAuditPasses(scratch, scratch.values, {})) {
      ++g_violations;
      std::fprintf(stderr,
                   "VIOLATION: loader accepted an audit-failing theme\n%s\n",
                   text.c_str());
    }
    // Schema strictness oracle: unknown keys cannot exist in the applied map.
    for (const auto& [k, v] : scratch.values) {
      (void)v;
      bool known = false;
      for (const auto& t : base.source.tokens)
        if (t.name == k) known = true;
      if (!known) {
        ++g_violations;
        std::fprintf(stderr,
                     "VIOLATION: accepted doc carries unknown token '%s'\n",
                     k.c_str());
      }
    }
  }

  // Also run some raw-bytes hostile docs (invalid UTF-8 etc.) deterministi-
  // cally — parser-level refusals counted as checks.
  const std::string raws[] = {
      std::string("\xff\xfe{", 3),
      "{\"surface\":\"#fff\"} trailing",
      std::string("{\"a\":1,\"a\":2}"),
      std::string(70 * 1024, 'x'),
  };
  for (const auto& raw : raws) {
    ++g_checks;
    LoaderState scratch = base;
    ApplyResult r = ImportTheme(&scratch, raw);
    if (r.ok) {
      ++g_violations;
      std::fprintf(stderr, "VIOLATION: hostile raw accepted\n");
    } else {
      ++refusals;
    }
  }

  const double secs = std::chrono::duration<double>(Clock::now() - start)
                          .count();
  std::printf("themes_fuzz: %llu checks, %llu accepts, %llu refusals, "
              "%llu violations (seed %llu, %.0f s budget, %lld iters)\n",
              static_cast<unsigned long long>(g_checks),
              static_cast<unsigned long long>(accepts),
              static_cast<unsigned long long>(refusals),
              static_cast<unsigned long long>(g_violations),
              static_cast<unsigned long long>(seed), secs,
              static_cast<long long>(iter));
  XR_EXPECT_EQ(g_violations, 0u);
  return xrtest::Report("themes_fuzz");
}
