// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// commands bench: the palette ranking core budget.
//
//   Sub-budget (measured here, off-tree, honest hardware line):
//     match(query, 2000 commands), 32-char query, p99 <= 5 ms (5000 us).
//   Plan budget it supports (browser-side, HUMAN-GATED on the farm = HG-31):
//     palette <= 50 ms interactive warm / 150 ms cold.
//   Both numbers are recorded in the output JSON + evidence. This bench
//   measures ONLY the C++ ranking core — it does NOT claim the browser-side
//   50/150 ms budget (that needs the farm; the harness is committed, HG-31).
//
// The 2000-command corpus and the 32-char query pool are generated from a
// fixed, checked-in seed (below) so every run measures the identical input.
// No RNG state leaks across runs; steady_clock; warmup excluded.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "commands/core/matcher.h"
#include "commands/core/registry.h"

using namespace xr::commands;

namespace {

const uint64_t kSeed = 20260908;   // checked-in golden seed (P7)
const int kCorpusSize = 2000;
const int kQueryLen = 32;
const int kQueryPool = 200;
const int kWarmup = 200;
const int kIters = 20000;
const double kBudgetUs = 5000.0;  // 5 ms p99 core sub-budget

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ULL) {}
  uint64_t next() {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return s >> 33;
  }
  uint64_t below(uint64_t n) { return next() % n; }
};

const char* kWords[] = {
    "open",   "new",      "window",   "identity", "trust", "shield",
    "fortress", "tab",    "panel",    "settings", "network", "privacy",
    "history", "clear",   "tor",      "disposable", "help", "index",
    "cheatsheet", "search", "download", "bookmark", "reader", "translate",
    "screenshot", "vault", "extension", "update", "about", "diagnostics"};
constexpr int kWordCount = 30;

std::string BuildQuery(Rng& r) {
  // A 32-char query: a few word fragments (so some queries match), padded
  // deterministically to exactly kQueryLen.
  std::string q;
  for (int w = 0; w < 3 && (int)q.size() < kQueryLen - 4; ++w) {
    const char* word = kWords[r.below(kWordCount)];
    q += word;
    q += " ";
  }
  while ((int)q.size() < kQueryLen) {
    q += static_cast<char>('a' + static_cast<int>(r.below(26)));
  }
  return q.substr(0, kQueryLen);
}

std::string CpuModel() {
  std::FILE* f = std::fopen("/proc/cpuinfo", "r");
  if (!f) return std::string("unknown");
  std::string line, model;
  char buf[512];
  while (std::fgets(buf, sizeof(buf), f)) {
    line = buf;
    if (line.rfind("model name", 0) == 0) {
      auto p = line.find(':');
      if (p != std::string::npos) {
        model = line.substr(p + 1);
        while (!model.empty() && (model.front() == ' ' || model.front() == '\t'))
          model.erase(0, 1);
        while (!model.empty() && (model.back() == ' ' || model.back() == '\t' ||
                                  model.back() == '\n' || model.back() == '\r'))
          model.pop_back();
        break;
      }
    }
  }
  std::fclose(f);
  if (model.empty()) model = "unknown";
  return model;
}

long long Percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  size_t idx = static_cast<size_t>((p / 100.0) * (static_cast<double>(v.size()) - 1));
  return static_cast<long long>(v[idx]);
}

}  // namespace

int main() {
  Rng r(kSeed);
  // Build the 2000-command corpus (Command structs directly — this is a perf
  // corpus, not a registry; the tier-1 cap is a registration rule, not a
  // matcher concern).
  std::vector<Command> corpus;
  corpus.reserve(kCorpusSize);
  for (int i = 0; i < kCorpusSize; ++i) {
    Command c;
    c.id = "gen.cmd" + std::to_string(i);
    std::string title = "Gen " + std::to_string(i) + ": ";
    for (int w = 0; w < 4; ++w) {
      title += kWords[r.below(kWordCount)];
      if (w < 3) title += " ";
    }
    c.title = title;
    c.keywords = {kWords[r.below(kWordCount)], kWords[r.below(kWordCount)]};
    c.group = "Group" + std::to_string(r.below(12));
    c.attention_tier = (i % 5 == 0) ? "tier1" : "tier2";
    c.danger_class = (i % 9 == 0) ? "destructive" : "safe";
    c.surface = "command-palette";
    c.handler = "action.gen." + std::to_string(i);
    c.order = static_cast<size_t>(i);
    c.predicate_id = "always";
    corpus.push_back(c);
  }
  std::vector<const Command*> corpus_ptrs;
  for (auto& c : corpus) corpus_ptrs.push_back(&c);

  std::vector<std::string> queries;
  for (int i = 0; i < kQueryPool; ++i) queries.push_back(BuildQuery(r));

  // Warmup (excluded from the measurement).
  std::vector<RankedMatch> sink;
  for (int i = 0; i < kWarmup; ++i) sink = MatchQuery(queries[i % kQueryPool], corpus_ptrs);
  (void)sink;

  std::vector<double> us;
  us.reserve(kIters);
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < kIters; ++i) {
    const auto a = std::chrono::steady_clock::now();
    sink = MatchQuery(queries[i % kQueryPool], corpus_ptrs);
    const auto b = std::chrono::steady_clock::now();
    us.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count() / 1000.0);
  }
  (void)t0;

  long long p50 = Percentile(us, 50);
  long long p95 = Percentile(us, 95);
  long long p99 = Percentile(us, 99);
  bool met = p99 <= static_cast<long long>(kBudgetUs);

  std::string hardware = CpuModel() + " (nproc via /proc; sandbox)";
  std::string sample_query = queries[0];

  std::ostringstream o;
  o << "{\n";
  o << "  \"tool\": \"commands-bench\",\n";
  o << "  \"sub_budget_us_p99\": " << static_cast<long long>(kBudgetUs) << ",\n";
  o << "  \"plan_budget_note\": \"core sub-budget (5ms p99) supports the plan's 50ms "
     "interactive-warm / 150ms cold browser budget, which is HUMAN-GATED on the farm "
     "(HG-31) and NOT measured here\",\n";
  o << "  \"seed\": " << kSeed << ",\n";
  o << "  \"corpus_size\": " << kCorpusSize << ",\n";
  o << "  \"query_len\": " << kQueryLen << ",\n";
  o << "  \"query_pool\": " << kQueryPool << ",\n";
  o << "  \"iterations\": " << kIters << ",\n";
  o << "  \"warmup_excluded\": " << kWarmup << ",\n";
  o << "  \"sample_query\": \"" << sample_query << "\",\n";
  o << "  \"p50_us\": " << p50 << ",\n";
  o << "  \"p95_us\": " << p95 << ",\n";
  o << "  \"p99_us\": " << p99 << ",\n";
  o << "  \"hardware\": \"" << hardware << "\",\n";
  o << "  \"verdict\": \"" << (met ? "MET" : "EXCEEDED") << "\"\n";
  o << "}\n";
  std::string out = o.str();

  std::printf("%s", out.c_str());
  // Persist the result to bench-results.json (relative to cwd) so evidence can
  // reference a checked-in / generated artifact.
  std::ofstream f("bench-results.json");
  f << out;
  f.close();
  return met ? 0 : 1;
}
