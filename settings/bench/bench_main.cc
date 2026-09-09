// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// settings bench: the search-first ranking core budget (P8-T1).
//
//   Sub-budget (measured here, off-tree, honest hardware line):
//     MatchQuery over a 2000-setting synthetic corpus, 32-char query,
//     p99 <= 5 ms (5000 us) — the SAME core sub-budget the P7 commands
//     matcher carries, so both search cores stay interchangeable.
//   Plan budget it supports (browser-side, HUMAN-GATED on the farm):
//     settings shell interactive budget (HG-35).
//   The REAL v0 corpus is 11 settings (sub-microsecond per query); the
//   synthetic 2000-entry corpus is the future-scale shape (P13/P29), so a
//   regression at scale shows here, not in the browser.
//
// Corpus + query pool are generated from a fixed checked-in seed. No RNG
// state leaks across runs; steady_clock; warmup excluded. Output JSON is
// written to ./bench-results.json (cwd) AND printed (the Makefile runs it
// from build/ so the committed settings/tests/bench-results.json is a
// checked-in snapshot of a real run, commands/bench precedent).
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "settings/core/search.h"

using namespace xr::settings;

namespace {

const uint64_t kSeed = 20260909;   // checked-in golden seed (P8)
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
    "ads",      "adblock",   "blocking",   "trackers",  "privacy",
    "network",  "https",     "upgrade",    "route",     "proxy",
    "letterbox", "storage",  "in-memory",  "vault",     "export",
    "notifications", "ask",  "deny",       "identity",  "kind",
    "container", "ephemeral", "disposable", "persistent", "trust",
    "search",   "settings",  "notify",     "fingerprint", "clear"};
constexpr int kWordCount = 30;

std::string BuildQuery(Rng& r) {
  std::string q;
  for (int w = 0; w < 3 && static_cast<int>(q.size()) < kQueryLen - 4; ++w) {
    q += kWords[r.below(kWordCount)];
    q += " ";
  }
  while (static_cast<int>(q.size()) < kQueryLen) {
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
        while (!model.empty() &&
               (model.back() == ' ' || model.back() == '\t' ||
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
  std::vector<IndexEntry> corpus;
  corpus.reserve(kCorpusSize);
  const char* kSections[] = {"network", "privacy", "identity"};
  for (int i = 0; i < kCorpusSize; ++i) {
    IndexEntry e;
    const std::string sec = kSections[r.below(3)];
    e.key = sec + ".gen" + std::to_string(i);
    e.kind = EntryKind::kSetting;
    e.order = static_cast<size_t>(i);
    e.fields.push_back(e.key);
    e.fields.push_back(sec);
    std::string title;
    for (int w = 0; w < 3; ++w) {
      title += kWords[r.below(kWordCount)];
      if (w < 2) title += " ";
    }
    e.fields.push_back(title);
    corpus.push_back(std::move(e));
  }

  std::vector<std::string> queries;
  for (int i = 0; i < kQueryPool; ++i) queries.push_back(BuildQuery(r));

  // Warmup (excluded from the measurement).
  std::vector<RankedResult> sink;
  for (int i = 0; i < kWarmup; ++i) {
    sink = MatchQuery(queries[i % kQueryPool], corpus);
  }
  (void)sink;

  std::vector<double> us;
  us.reserve(kIters);
  for (int i = 0; i < kIters; ++i) {
    const auto a = std::chrono::steady_clock::now();
    sink = MatchQuery(queries[i % kQueryPool], corpus);
    const auto b = std::chrono::steady_clock::now();
    us.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a)
                     .count() /
                 1000.0);
  }

  long long p50 = Percentile(us, 50);
  long long p95 = Percentile(us, 95);
  long long p99 = Percentile(us, 99);
  bool met = p99 <= static_cast<long long>(kBudgetUs);

  std::string hardware = CpuModel() + " (nproc via /proc; sandbox)";
  std::string sample_query = queries[0];

  std::ostringstream o;
  o << "{\n";
  o << "  \"tool\": \"settings-bench\",\n";
  o << "  \"sub_budget_us_p99\": " << static_cast<long long>(kBudgetUs) << ",\n";
  o << "  \"real_v0_corpus_note\": \"the shipped v0 schema indexes 11 settings "
       "+ 3 sections (sub-microsecond per query); this 2000-entry corpus is "
       "the future-scale shape (P13/P29)\",\n";
  o << "  \"plan_budget_note\": \"core sub-budget (5 ms p99) supports the "
       "settings-shell interactive budget, which is HUMAN-GATED on the farm "
       "(HG-35) and NOT measured here\",\n";
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
  const std::string out = o.str();

  std::printf("%s", out.c_str());
  std::ofstream f("bench-results.json");
  f << out;
  f.close();
  return met ? 0 : 1;
}
