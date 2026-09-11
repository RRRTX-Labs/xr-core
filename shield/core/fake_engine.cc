// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/fake_engine — the v1 reference matcher (see
// fake_engine.h for the semantics contract). Every branch here is pinned
// by the golden vectors and mirrored by fakes/shield.py.
#include "shield/core/fake_engine.h"

namespace xr::shield {

namespace {

// Match `seg` starting exactly at `pos`; returns the end index, or -1.
// kSep matches one char of "/:" or, zero-width, end-of-string when it is
// the segment's last character.
long long MatchSegAt(const std::string& t, const std::string& seg,
                     size_t pos) {
  size_t p = pos;
  for (size_t i = 0; i < seg.size(); ++i) {
    char c = seg[i];
    if (c == kSep) {
      if (p == t.size()) {
        if (i + 1 == seg.size()) return static_cast<long long>(p);
        return -1;  // end-of-string kSep must be the segment's last char
      }
      if (t[p] != '/' && t[p] != ':') return -1;
      ++p;
      continue;
    }
    if (p >= t.size() || t[p] != c) return -1;
    ++p;
  }
  return static_cast<long long>(p);
}

// Earliest start >= `from` where seg matches; returns its end, or -1.
// Earliest-start maximizes the remainder, so greedy is complete for
// ordered containment (fake_engine.h).
long long FindSeg(const std::string& t, const std::string& seg, size_t from) {
  for (size_t s = from; s <= t.size(); ++s) {
    long long e = MatchSegAt(t, seg, s);
    if (e >= 0) return e;
  }
  return -1;
}

}  // namespace

bool MatchSegmentsV1(const std::vector<std::string>& segs,
                     const std::string& target, bool left_anchor,
                     bool right_anchor) {
  if (segs.empty()) return false;
  // a trailing wildcard ("...*|" shape) voids the right anchor
  bool ra = right_anchor && !segs.back().empty();
  if (ra) {
    // the last segment must END the target: find the earliest start whose
    // match ends exactly at target.size(), then fit the earlier segments
    // before it (their earliest ends are minimal, so one pass decides).
    const std::string& last = segs.back();
    long long anchor_start = -1;
    for (size_t s = 0; s <= target.size(); ++s) {
      if (MatchSegAt(target, last, s) == static_cast<long long>(target.size())) {
        anchor_start = static_cast<long long>(s);
        break;
      }
    }
    if (anchor_start < 0) return false;
    size_t pos = 0;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
      const std::string& seg = segs[i];
      long long e = (i == 0 && left_anchor)
                        ? MatchSegAt(target, seg, pos)
                        : FindSeg(target, seg, pos);
      if (e < 0 || e > anchor_start) return false;
      pos = static_cast<size_t>(e);
    }
    return true;
  }
  size_t pos = 0;
  for (size_t i = 0; i < segs.size(); ++i) {
    const std::string& seg = segs[i];
    long long e = (i == 0 && left_anchor) ? MatchSegAt(target, seg, pos)
                                          : FindSeg(target, seg, pos);
    if (e < 0) return false;
    pos = static_cast<size_t>(e);
  }
  return true;
}

bool FilterMatchV1(const ParsedFilter& f, const UrlParts& parts) {
  const std::string match_url =
      parts.scheme + "://" + parts.host + parts.path;
  if (!f.domain_anchor) {
    return MatchSegmentsV1(f.segments, match_url, f.left_anchor,
                           f.right_anchor);
  }
  // domain anchor: segments[0] carries "d[rest]" — d ends at the first
  // separator sentinel or '/' (host text cannot contain either)
  const std::string& seg0 = f.segments.front();
  size_t cut = seg0.find_first_of(std::string(1, kSep) + "/");
  std::string d = cut == std::string::npos ? seg0 : seg0.substr(0, cut);
  bool host_ok = d.empty() || parts.host == d ||
                 (parts.host.size() > d.size() &&
                  parts.host.compare(parts.host.size() - d.size(), d.size(),
                                     d) == 0 &&
                  parts.host[parts.host.size() - d.size() - 1] == '.');
  if (!host_ok) return false;
  std::string rest = cut == std::string::npos ? "" : seg0.substr(cut);
  if (f.right_anchor && rest.empty() && f.segments.size() == 1) {
    // "||d|" — the bare domain: SplitUrl canonicalizes no-path to "/"
    return parts.path == "/";
  }
  std::vector<std::string> segs{rest};
  for (size_t i = 1; i < f.segments.size(); ++i)
    segs.push_back(f.segments[i]);
  // rest matches left-anchored against the PATH (the host part is already
  // consumed by the dot-boundary check above)
  return MatchSegmentsV1(segs, parts.path, /*left_anchor=*/true,
                         f.right_anchor);
}

bool TableEngine::Match(const RequestContext& ctx, EngineHit* hit) {
  if (!alive() || bundle_ == nullptr) return false;  // no opinion
  bool have = false;
  EngineHit first;
  for (const BundleList& list : bundle_->lists) {
    for (const Rule& rule : list.rules) {
      if (rule.kind == "cosmetic") continue;  // network engine ignores it
      if (!rule.domains.empty()) {
        bool ok = false;
        for (const auto& d : rule.domains) {
          if (d == ctx.origin.registrable_domain || d == ctx.parts.host) {
            ok = true;
            break;
          }
        }
        if (!ok) continue;
      }
      if (!rule.exclude_domains.empty()) {
        bool ex = false;
        for (const auto& d : rule.exclude_domains) {
          if (d == ctx.origin.registrable_domain || d == ctx.parts.host) {
            ex = true;
            break;
          }
        }
        if (ex) continue;
      }
      if (!FilterMatchV1(rule.parsed, ctx.parts)) continue;
      EngineHit cand;
      cand.action = rule.action;
      cand.rule_id = rule.id;
      cand.list_id = list.name;
      cand.redirect_resource = rule.resource;
      if (rule.action == Action::kAllow) {
        *hit = cand;  // allow overrides block — earliest allow wins
        return true;
      }
      if (!have) {
        first = cand;
        have = true;
      }
    }
  }
  if (have) {
    *hit = first;
    return true;
  }
  return false;
}

}  // namespace xr::shield
