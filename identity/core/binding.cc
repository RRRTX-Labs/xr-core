// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — binding implementation (header has the laws). The
// never-auto-switch law is enforced in ChangeTabIdentity: the only entry
// point that mutates a tab's identity REFUSES the suggestion cause.
#include "core/binding.h"

#include <utility>

namespace xr::identity {

CallResult BindingModel::SetWindowDefault(std::string_view window,
                                          std::string_view domain) {
  CallResult res;
  if (!DomainShapeOk(domain)) {
    res.error = "kMalformedInput";
    return res;
  }
  window_default_[std::string(window)] = std::string(domain);
  res.ok = true;
  return res;
}

CallResult BindingModel::OpenTab(std::string_view window, uint64_t tab_id,
                                 std::string_view opener_domain) {
  CallResult res;
  std::string domain(opener_domain);
  if (domain.empty()) {
    auto it = window_default_.find(std::string(window));
    if (it == window_default_.end()) { res.error = "kMalformedInput"; return res; }
    domain = it->second;
  }
  return ChangeTabIdentity(tab_id, domain, ChangeCause::kWindowDefault);
}

CallResult BindingModel::MoveTab(std::string_view from, std::string_view to,
                                 uint64_t tab_id, bool after_nav,
                                 const std::map<uint64_t, std::string>& group_peers) {
  CallResult res;
  // Order mirrors the frozen fake (fakes/identity.py move_tab): the P4
  // timing finding is checked FIRST so a navigated tab is kNotPermitted
  // whatever the caller's other args look like.
  if (after_nav) {
    // Destructive law: a non-destructive attach to a navigated tab does not
    // exist. REFUSE — the UI must confirm + reload; we never reload here.
    res.error = "kNotPermitted (after_nav: move requires confirm + reload)";
    return res;
  }
  auto cur = tab_identity_.find(tab_id);
  if (cur == tab_identity_.end() || cur->second != std::string(from)) {
    res.error = "kUnknownIdentity";
    return res;
  }
  if (!DomainShapeOk(to)) { res.error = "kMalformedInput"; return res; }
  for (const auto& [peer_id, peer_domain] : group_peers) {
    if (peer_domain != std::string(to)) {
      res.error = "kNotPermitted (group split: peer " +
                  std::to_string(peer_id) + " is in another identity)";
      return res;
    }
  }
  return ChangeTabIdentity(tab_id, to, ChangeCause::kUserConfirmedMove);
}

CallResult BindingModel::RestoreTab(uint64_t tab_id, std::string_view domain) {
  return ChangeTabIdentity(tab_id, domain, ChangeCause::kRestore);
}

CallResult BindingModel::RecordSuggestion(std::string_view site,
                                          std::string_view domain) {
  CallResult res;
  if (!DomainShapeOk(domain)) { res.error = "kMalformedInput"; return res; }
  suggestions_.push_back({std::string(site), std::string(domain), true});
  res.ok = true;
  return res;
}

std::optional<std::string> BindingModel::TabIdentity(
    uint64_t tab_id) const {
  auto it = tab_identity_.find(tab_id);
  if (it == tab_identity_.end()) return std::nullopt;
  return it->second;
}

CallResult BindingModel::TestPlantedAutoSwitch(uint64_t tab_id,
                                               std::string_view to) {
  // The forbidden path: a suggestion-caused change. This MUST fail — the
  // test asserts it; success would mean the never-auto-switch law broke.
  return ChangeTabIdentity(tab_id, to, ChangeCause::kSuggestion);
}

CallResult BindingModel::ChangeTabIdentity(uint64_t tab_id,
                                           std::string_view to,
                                           ChangeCause cause) {
  CallResult res;
  if (cause == ChangeCause::kSuggestion) {
    // STRUCTURAL law: the suggestion cause can never change a binding —
    // not "is not used", CANNOT be used. Auto-switch is unrepresentable.
    res.error = "kNotPermitted (suggestion-caused change: never auto-switch)";
    return res;
  }
  std::string from;
  auto it = tab_identity_.find(tab_id);
  if (it != tab_identity_.end()) from = it->second;
  tab_identity_[tab_id] = std::string(to);
  changes_.push_back({tab_id, from, std::string(to), cause});
  res.ok = true;
  return res;
}

}  // namespace xr::identity
