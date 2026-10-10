// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — manager page model implementation (header has the laws).
#include "core/manager_page.h"

#include "core/mint.h"

namespace xr::identity {
namespace {

using xr::common::JsonValue;

const char* LifecycleName(State s) {
  switch (s) {
    case State::kActive: return "kActive";
    case State::kHibernated: return "kHibernated";
    case State::kDestroyed: return "kDestroyed";
  }
  return "kDestroyed";
}

bool IsLowerHexColor(std::string_view v) {
  if (v.size() != 7 || v[0] != '#') return false;
  for (size_t i = 1; i < v.size(); ++i) {
    const char c = v[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// The refusal reason for a display name, or "" when it is acceptable.
std::string NameRefusal(std::string_view domain, std::string_view name) {
  if (name.empty()) return "kMalformedInput (name-empty)";
  if (name.size() > kDisplayNameMax) return "kMalformedInput (name-too-long)";
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7f) return "kMalformedInput (name-control-char)";
  }
  if (!LooksOpaque(domain, name)) {
    return "kNotPermitted (domain not opaque to this name)";
  }
  return "";
}

}  // namespace

bool DevChannel(std::string_view channel, std::string* refusal) {
  if (channel == "dev") return true;
  if (refusal) *refusal = "build-channel-not-dev";
  return false;
}

CallResult ApplyPageEdit(IdentityStore* store, Manager* manager,
                         std::string_view domain, PageEdit edit,
                         std::string_view value) {
  CallResult res;
  IdentityRecord* r = store->Find(domain);
  if (r == nullptr) {
    res.error = "kUnknownIdentity";
    return res;
  }
  if (r->state == State::kDestroyed) {
    res.error = "kNotPermitted";
    return res;
  }
  switch (edit) {
    case PageEdit::kRename: {
      const std::string why = NameRefusal(domain, value);
      if (!why.empty()) {
        res.error = why;
        return res;
      }
      r->display_name = std::string(value);
      break;
    }
    case PageEdit::kRecolor:
      if (!IsLowerHexColor(value)) {
        res.error = "kMalformedInput (color-not-hex)";
        return res;
      }
      r->color = std::string(value);
      break;
    case PageEdit::kArchive:
      return manager->Hibernate(domain);
  }
  res.ok = true;
  return res;
}

std::map<std::string, int64_t> TabCounts(const BindingModel& binding) {
  std::map<uint64_t, std::string> latest;
  for (const auto& ch : binding.changes()) latest[ch.tab_id] = ch.to;
  std::map<std::string, int64_t> counts;
  for (const auto& [tab, domain] : latest) counts[domain] += 1;
  return counts;
}

JsonValue BuildManagerPage(const IdentityStore& store,
                           const BindingModel& binding,
                           const std::vector<std::string>& order,
                           const std::map<std::string, int64_t>& permission_counts,
                           const std::vector<PurgeOutcome>& purges) {
  const std::map<std::string, int64_t> tabs = TabCounts(binding);
  JsonValue::Array rows;
  for (const std::string& domain : order) {
    const IdentityRecord* r = store.Find(domain);
    if (r == nullptr) continue;  // destroyed: the purge list reports it
    JsonValue::Object row;
    row["color"] = JsonValue(r->color);
    row["display_name"] = JsonValue(r->display_name);
    row["domain"] = JsonValue(r->domain);
    row["glyph"] = JsonValue(r->glyph);
    row["in_memory"] = JsonValue(r->in_memory);
    row["lifecycle"] = JsonValue(LifecycleName(r->state));
    auto pc = permission_counts.find(domain);
    row["permission_count"] =
        pc == permission_counts.end() ? JsonValue() : JsonValue(pc->second);
    row["storage_bytes"] = JsonValue(static_cast<int64_t>(store.ResidualBytes(domain)));
    auto tc = tabs.find(domain);
    row["tab_count"] = JsonValue(tc == tabs.end() ? int64_t{0} : tc->second);
    row["template_id"] = JsonValue(r->template_id);
    rows.push_back(JsonValue(std::move(row)));
  }
  JsonValue::Array purged;
  bool unverified = false;
  for (const PurgeOutcome& p : purges) {
    JsonValue::Array kinds;
    for (const std::string& k : p.residual_kinds) kinds.push_back(JsonValue(k));
    JsonValue::Object o;
    o["domain"] = JsonValue(p.domain);
    o["residual_kinds"] = JsonValue(std::move(kinds));
    o["verified"] = JsonValue(p.verified);
    purged.push_back(JsonValue(std::move(o)));
    unverified = unverified || !p.verified;
  }
  const char* state = unverified      ? kManagerPageStates[2]
                      : rows.empty()  ? kManagerPageStates[1]
                                      : kManagerPageStates[0];
  JsonValue::Object page;
  page["purges"] = JsonValue(std::move(purged));
  page["rows"] = JsonValue(std::move(rows));
  page["state"] = JsonValue(state);
  return JsonValue(std::move(page));
}

}  // namespace xr::identity
