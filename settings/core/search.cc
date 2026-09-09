// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Settings search implementation (see search.h). Integer-only scoring,
// deterministic tie-breaks; byte-parity with fakes/settings.py.
#include "settings/core/search.h"

#include <algorithm>
#include <cctype>

namespace xr::settings {
namespace {

bool IsSubseq(const std::string& needle, const std::string& hay) {
  size_t pos = 0;
  for (const char c : needle) {
    bool found = false;
    for (; pos < hay.size(); ++pos) {
      if (hay[pos] == c) {
        ++pos;
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

// Lowercase + drop ' ' (byte-level; Arabic bytes are >= 0x80 and pass
// through untouched).
std::string JoinNoSpaces(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    if (c != ' ') out.push_back(c);
  }
  return out;
}

}  // namespace

std::string LowerAscii(const std::string& s) {
  std::string out = s;
  for (auto& c : out) {
    unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x80) c = static_cast<char>(std::tolower(u));
  }
  return out;
}

long long WordTokenScore(const std::string& word, const std::string& token) {
  if (word.empty() || token.empty()) return 0;
  if (token == word) return 3;  // exact, any length
  if (word.size() >= 3 && token.size() > word.size() &&
      token.compare(0, word.size(), word) == 0) {
    return 2;  // prefix
  }
  if (word.size() >= 3 && IsSubseq(word, token)) return 1;  // fzf-class
  return 0;
}

long long WordFieldBest(const std::string& word, const std::string& field) {
  long long best = 0;
  size_t start = 0;
  while (start <= field.size()) {
    size_t end = field.find(' ', start);
    if (end == std::string::npos) end = field.size();
    long long v = WordTokenScore(word, field.substr(start, end - start));
    if (v > best) best = v;
    if (end == field.size()) break;
    start = end + 1;
  }
  const std::string joined = JoinNoSpaces(field);
  if (joined != field) {
    long long v = WordTokenScore(word, joined);
    if (v > best) best = v;
  }
  return best;
}

std::vector<IndexEntry> BuildIndex(const SettingsSchema& schema) {
  std::vector<IndexEntry> out;
  size_t order = 0;
  auto add = [&out, &order](IndexEntry e) {
    e.parsed.reserve(e.fields.size());
    for (const std::string& f : e.fields) {
      const std::string low = LowerAscii(f);
      IndexEntry::ParsedField pf;
      pf.joined = JoinNoSpaces(low);
      size_t start = 0;
      while (start <= low.size()) {
        size_t end = low.find(' ', start);
        if (end == std::string::npos) end = low.size();
        const std::string tok = low.substr(start, end - start);
        if (!tok.empty()) pf.tokens.push_back(tok);
        if (end == low.size()) break;
        start = end + 1;
      }
      e.parsed.push_back(std::move(pf));
    }
    out.push_back(std::move(e));  // fields kept (diagnostics/tests) + parsed
  };
  for (const SectionDef* s : schema.Sections()) {
    IndexEntry e;
    e.key = s->id;
    e.kind = EntryKind::kSection;
    e.order = order++;
    e.fields.push_back(s->id);
    add(std::move(e));
  }
  for (const SettingDef* st : schema.Settings()) {
    IndexEntry e;
    e.key = st->key;
    e.kind = EntryKind::kSetting;
    e.order = order++;
    e.fields.push_back(st->key);
    e.fields.push_back(st->section);
    for (const std::string& a : st->aliases) e.fields.push_back(a);
    add(std::move(e));
  }
  return out;
}

std::vector<RankedResult> MatchQuery(const std::string& query,
                                     const std::vector<IndexEntry>& corpus) {
  const std::string q = LowerAscii(query);
  // Words: non-empty, length >= 2 (a 1-char word never decides).
  std::vector<std::string> words;
  size_t start = 0;
  while (start <= q.size()) {
    size_t end = q.find(' ', start);
    if (end == std::string::npos) end = q.size();
    const std::string w = q.substr(start, end - start);
    if (!w.empty() && w.size() > 1) words.push_back(w);
    if (end == q.size()) break;
    start = end + 1;
  }
  const std::string q_joined = JoinNoSpaces(q);
  std::vector<RankedResult> out;
  if (q_joined.empty()) return out;  // empty/whitespace query: no results
  for (const IndexEntry& e : corpus) {
    long long total = 0;
    for (const std::string& w : words) {
      long long best = 0;
      for (const IndexEntry::ParsedField& pf : e.parsed) {
        for (const std::string& tok : pf.tokens) {
          long long v = WordTokenScore(w, tok);
          if (v > best) best = v;
        }
        long long vj = WordTokenScore(w, pf.joined);
        if (vj > best) best = vj;
      }
      total += best * best;
    }
    long long phrase = 0;
    for (const IndexEntry::ParsedField& pf : e.parsed) {
      const std::string& fj = pf.joined;
      if (fj.empty()) continue;
      if (fj == q_joined) {
        phrase = 300;
        break;
      }
      if (phrase < 150 && IsSubseq(q_joined, fj)) phrase = 150;
    }
    const long long score = total + phrase;
    if (score <= 0) continue;
    RankedResult r;
    r.key = e.key;
    r.kind = e.kind;
    r.score = score;
    r.order = e.order;
    out.push_back(r);
  }
  // Deterministic: score desc, then schema order asc.
  std::stable_sort(out.begin(), out.end(),
                   [](const RankedResult& a, const RankedResult& b) {
                     if (a.score != b.score) return a.score > b.score;
                     return a.order < b.order;
                   });
  return out;
}

}  // namespace xr::settings
