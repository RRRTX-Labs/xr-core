// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Palette ranking core. See matcher.h for the scoring model. Pure, integer,
// deterministic: no RNG, no clock, no I/O. The stable tie-break (score desc,
// registration order asc) is what makes the corpus bench and golden rankings
// reproducible across ×100 runs.
#include "commands/core/matcher.h"

#include <algorithm>
#include <cctype>

namespace xr::commands {
namespace {

const long long kConsecutive = 5;
const long long kBoundary = 10;
const long long kKindScale = 1000000;  // kind always dominates within-kind score

bool IsSeparator(char c) {
  return c == ' ' || c == '-' || c == '_' || c == '/' || c == '.' || c == '\t';
}

// Greedy left-to-right subsequence: matched target positions, or empty.
std::vector<int> SubseqPositions(const std::string& q, const std::string& t) {
  std::vector<int> pos;
  pos.reserve(q.size());
  size_t ti = 0;
  for (char qc : q) {
    if (ti >= t.size()) return {};
    bool found = false;
    for (; ti < t.size(); ++ti) {
      if (t[ti] == qc) {
        pos.push_back(static_cast<int>(ti));
        ++ti;
        found = true;
        break;
      }
    }
    if (!found) return {};
  }
  return pos;
}

long long FzfScore(const std::string& q, const std::string& t,
                   const std::vector<int>& pos) {
  long long s = 0;
  for (size_t i = 0; i < pos.size(); ++i) {
    int p = pos[i];
    s += 1;  // base match
    if (i > 0 && pos[i - 1] == p - 1) s += kConsecutive;
    if (p == 0) {
      s += kBoundary;  // start
    } else {
      char prev = t[static_cast<size_t>(p - 1)];
      if (IsSeparator(prev)) {
        s += kBoundary;
      } else if (prev >= 'a' && prev <= 'z' && t[static_cast<size_t>(p)] >= 'A' &&
                 t[static_cast<size_t>(p)] <= 'Z') {
        s += kBoundary;  // camelCase boundary
      }
    }
  }
  s -= static_cast<long long>((t.size() - q.size()) / 2);  // tightness
  if (s < 1) s = 1;
  return s;
}

}  // namespace

std::string LowerAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::vector<RankedMatch> MatchQuery(
    const std::string& query, const std::vector<const Command*>& corpus) {
  std::vector<RankedMatch> out;
  const std::string q = LowerAscii(query);

  for (const Command* c : corpus) {
    RankedMatch m;
    m.id = c->id;
    m.order = c->order;
    m.title = c->title;

    if (q.empty()) {
      // Empty query: show everything in registration order.
      m.kind = 1;
      m.score = kKindScale;
      out.push_back(m);
      continue;
    }

    const std::string t = LowerAscii(c->title);
    auto pos = SubseqPositions(q, t);
    if (!pos.empty()) {
      m.kind = t.find(q) != std::string::npos ? 2 : 1;
      m.score = kKindScale * m.kind + FzfScore(q, t, pos);
    } else {
      std::string k = LowerAscii(c->id);
      for (const auto& kw : c->keywords) k += " " + LowerAscii(kw);
      auto posk = SubseqPositions(q, k);
      if (posk.empty()) continue;  // not a subsequence anywhere: no match
      m.kind = 0;
      m.score = kKindScale * 0 + FzfScore(q, k, posk);
    }
    out.push_back(m);
  }

  std::stable_sort(out.begin(), out.end(), [](const RankedMatch& a,
                                              const RankedMatch& b) {
    if (a.score != b.score) return a.score > b.score;  // score desc
    return a.order < b.order;                          // stable tie-break
  });
  return out;
}

}  // namespace xr::commands
