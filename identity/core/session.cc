// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — session store implementation (header has the laws). The
// wire shape is deliberately JSON-free at this layer: the host façade
// serializes; the core validates + snapshots. No clock, no files.
#include "core/session.h"

#include <algorithm>
#include <map>

namespace xr::identity {
namespace {

bool SortableTab(const SessionTab& a, const SessionTab& b) {
  return a.tab_id < b.tab_id;
}

}  // namespace

std::string SerializeSession(const SessionDoc& doc) {
  std::vector<SessionTab> tabs = doc.tabs;
  std::sort(tabs.begin(), tabs.end(), SortableTab);
  // A tiny canonical serializer (tab_id:window:domain;|durable;…) — the
  // HOST layer renders JSON; this is the deterministic core form the
  // vectors pin. Sorted, compact, stable.
  std::string out = "v1|";
  for (const auto& t : tabs) {
    out += std::to_string(t.tab_id) + ":" + t.window + ":" + t.domain + ";";
  }
  out += "|";
  std::vector<std::string> dur = doc.durable_domains;
  std::sort(dur.begin(), dur.end());
  for (const auto& d : dur) out += d + ";";
  return out;
}

SessionDoc Snapshot(const IdentityStore& store, const BindingModel& binding,
                    std::vector<uint64_t>* dropped) {
  SessionDoc doc;
  // The tabs come from the binding audit (append-only: the LATEST change
  // per tab is its current binding); liveness via the store.
  std::map<uint64_t, SessionTab> latest;
  for (const auto& ch : binding.changes()) {
    if (ch.to.empty()) continue;
    const IdentityRecord* r = store.Find(ch.to);
    if (r == nullptr) continue;  // purged since: not snapshot-able
    latest[ch.tab_id] = {ch.tab_id, ch.to, "" /*window: host's to fill*/};
  }
  for (const auto& [tab_id, t] : latest) {
    const IdentityRecord* r = store.Find(t.domain);
    if (r != nullptr && r->in_memory) {
      if (dropped != nullptr) dropped->push_back(tab_id);
      continue;  // disposables are NEVER written to the session
    }
    doc.tabs.push_back(t);
  }
  // The durable set is what the (validated) tabs reference — every live
  // non-disposable identity a restore may re-bind to.
  for (const auto& t : doc.tabs) doc.durable_domains.push_back(t.domain);
  std::sort(doc.durable_domains.begin(), doc.durable_domains.end());
  doc.durable_domains.erase(
      std::unique(doc.durable_domains.begin(), doc.durable_domains.end()),
      doc.durable_domains.end());
  return doc;
}

SessionRestoreResult RestoreSession(const std::string& wire,
                                    const IdentityStore& store,
                                    const BindingModel& binding) {
  (void)binding;
  SessionRestoreResult res;
  // Minimal strict parse of the v1 core form ("v1|t:w:d;…|d;…").
  if (wire.rfind("v1|", 0) != 0) {
    res.error = "kMalformedInput (session: not the v1 shape)";
    return res;
  }
  const size_t mid = wire.find('|', 3);
  if (mid == std::string::npos) {
    res.error = "kMalformedInput (session: missing durable section)";
    return res;
  }
  const std::string tabs_part = wire.substr(3, mid - 3);
  const std::string dur_part = wire.substr(mid + 1);
  SessionDoc doc;
  size_t pos = 0;
  while (pos < tabs_part.size()) {
    const size_t end = tabs_part.find(';', pos);
    if (end == std::string::npos) {
      res.error = "kMalformedInput (session: unterminated tab row)";
      return res;
    }
    const std::string row = tabs_part.substr(pos, end - pos);
    pos = end + 1;
    const size_t c1 = row.find(':');
    const size_t c2 = row.find(':', c1 + 1);
    if (c1 == std::string::npos || c2 == std::string::npos) {
      res.error = "kMalformedInput (session: tab row shape)";
      return res;
    }
    SessionTab t;
    t.tab_id = static_cast<uint64_t>(std::stoull(row.substr(0, c1)));
    t.window = row.substr(c1 + 1, c2 - c1 - 1);
    t.domain = row.substr(c2 + 1);
    doc.tabs.push_back(std::move(t));
  }
  pos = 0;
  while (pos < dur_part.size() && !dur_part.empty()) {
    const size_t end = dur_part.find(';', pos);
    if (end == std::string::npos) {
      res.error = "kMalformedInput (session: durable row shape)";
      return res;
    }
    doc.durable_domains.push_back(dur_part.substr(pos, end - pos));
    pos = end + 1;
  }
  // THE LAW: every restored tab's domain must be a LIVE identity in the
  // store — restore never guesses, never falls back to the default
  // partition (C-14's failure mode), never re-creates a purged one.
  for (const auto& t : doc.tabs) {
    const IdentityRecord* r = store.Find(t.domain);
    if (r == nullptr) {
      res.error = "kMalformedInput (session: tab " + std::to_string(t.tab_id) +
                  " names a non-live identity " + t.domain +
                  " — refusing, never defaulting)";
      return res;
    }
    if (r->in_memory) {
      // A disposable in the session file is a TAMPER or a bug: disposables
      // are never written (Snapshot drops them) and never restored.
      res.error = "kMalformedInput (session: tab " + std::to_string(t.tab_id) +
                  " names a disposable identity — never restorable)";
      return res;
    }
  }
  res.doc = std::move(doc);
  res.ok = true;
  return res;
}

}  // namespace xr::identity
