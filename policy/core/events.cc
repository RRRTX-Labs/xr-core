// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — event generator implementation (see events.h).
#include "policy/core/events.h"
#include "policy/core/resolve.h"  // kContractVersion

namespace xr::policy {

std::vector<std::pair<std::string, std::string>> FlattenPolicy(const EffectivePolicy& p) {
  std::vector<std::pair<std::string, std::string>> out;
  out.emplace_back("blocking.block_network_ads", std::string(p.block_network_ads ? "true" : "false"));
  out.emplace_back("blocking.block_trackers", std::string(p.block_trackers ? "true" : "false"));
  out.emplace_back("blocking.upgrade_to_https", std::string(p.upgrade_to_https ? "true" : "false"));
  out.emplace_back("cosmetic.hide_cosmetic", std::string(p.hide_cosmetic ? "true" : "false"));
  out.emplace_back("cosmetic.block_scriptlets", std::string(p.block_scriptlets ? "true" : "false"));
  out.emplace_back("permissions.geolocation", ToString(p.geolocation));
  out.emplace_back("permissions.camera", ToString(p.camera));
  out.emplace_back("permissions.microphone", ToString(p.microphone));
  out.emplace_back("permissions.notifications", ToString(p.notifications));
  out.emplace_back("egress.block_third_party", std::string(p.block_third_party ? "true" : "false"));
  out.emplace_back("egress.route", ToString(p.route));
  out.emplace_back("fingerprint.mode", ToString(p.fingerprint_mode));
  out.emplace_back("storage_scope.scope", ToString(p.storage_scope));
  out.emplace_back("storage_scope.in_memory", std::string(p.in_memory ? "true" : "false"));
  out.emplace_back("vault_scope.autofill_allowed", std::string(p.autofill_allowed ? "true" : "false"));
  out.emplace_back("process_policy.site_isolated", std::string(p.site_isolated ? "true" : "false"));
  out.emplace_back("process_policy.dedicated_process", std::string(p.dedicated_process ? "true" : "false"));
  out.emplace_back("letterbox", std::string(p.letterbox ? "true" : "false"));
  // NOTE: version.contract_version and vault_scope.export_allowed are
  // contract-invariant in v1 (never delta-worthy); omitting them is
  // deliberate. export_allowed is const false by schema.
  return out;
}

namespace {

std::string PermissionWord(const std::string& to) {
  if (to == "kDeny") return "blocked";
  if (to == "kAllow") return "allowed";
  return "ask-every-time";
}

std::string BaseName(const std::string& path) {
  auto pos = path.find('.');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string RouteWord(const std::string& to) {
  if (to == "kDirect") return "direct";
  if (to == "kProxy") return "proxy";
  if (to == "kWireguard") return "wireguard";
  return "tor";
}

std::string FingerprintWord(const std::string& from, const std::string& to) {
  int rank = [](const std::string& s) {
    if (s == "kOff") return 0;
    if (s == "kReduce") return 1;
    return 2;
  }(to);
  int from_rank = [](const std::string& s) {
    if (s == "kOff") return 0;
    if (s == "kReduce") return 1;
    return 2;
  }(from);
  if (rank > from_rank) return "stricter fingerprint protection";
  return "reduced fingerprint protection";
}

}  // namespace

std::vector<PolicyDelta> DiffPolicies(const EffectivePolicy& old_p, const EffectivePolicy& new_p) {
  std::vector<PolicyDelta> out;
  auto a = FlattenPolicy(old_p);
  auto b = FlattenPolicy(new_p);
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
    if (a[i].second == b[i].second) continue;
    const std::string& path = a[i].first;
    PolicyDelta d;
    d.path = path;
    d.from = a[i].second;
    d.to = b[i].second;
    if (path.rfind("permissions.", 0) == 0) {
      d.change = PermissionWord(d.to);
      d.human = BaseName(path) + " now " + d.change;
    } else if (path == "egress.route") {
      d.change = "rerouted";
      d.human = "route now " + RouteWord(d.to);
    } else if (path == "fingerprint.mode") {
      d.change = d.to;
      d.human = FingerprintWord(d.from, d.to);
    } else if (path == "storage_scope.scope") {
      d.change = d.to;
      d.human = "storage now " + BaseName(path) + " " + d.to;
    } else if (path == "letterbox") {
      d.change = d.to == "true" ? "on" : "off";
      d.human = d.to == "true" ? "letterbox isolation on" : "letterbox isolation off";
    } else {
      // Generic boolean: direction words, field-named.
      bool on = d.to == "true";
      d.change = on ? "on" : "off";
      if (path.rfind("blocking.", 0) == 0) {
        d.human = BaseName(path) + (on ? " on" : " off");
      } else if (path.rfind("cosmetic.", 0) == 0) {
        d.human = BaseName(path) + (on ? " on" : " off");
      } else if (path == "process_policy.dedicated_process") {
        d.human = on ? "stricter isolation" : "relaxed isolation";
      } else if (path == "process_policy.site_isolated") {
        d.human = on ? "site isolation on" : "site isolation off";
      } else if (path == "storage_scope.in_memory") {
        d.human = on ? "storage kept in memory only" : "storage written to disk";
      } else {
        d.human = path + " " + d.change;
      }
    }
    out.push_back(std::move(d));
  }
  return out;
}

std::string BuildSummary(const std::vector<PolicyDelta>& deltas) {
  std::string out;
  for (const auto& d : deltas) {
    if (!out.empty()) out += " · ";
    out += d.human;
  }
  return out;
}

JsonValue BuildPolicyChangeEvent(const std::string& identity, const std::string& site,
                                 const std::string& previous_trust, const std::string& new_trust,
                                 const EffectivePolicy& old_p, const EffectivePolicy& new_p,
                                 bool undo_allowed) {
  auto deltas = DiffPolicies(old_p, new_p);
  JsonValue::Array darr;
  for (const auto& d : deltas) {
    JsonValue::Object o;
    o.emplace("path", JsonValue(d.path));
    o.emplace("from", JsonValue(d.from));
    o.emplace("to", JsonValue(d.to));
    o.emplace("change", JsonValue(d.change));
    o.emplace("human", JsonValue(d.human));
    darr.push_back(JsonValue(std::move(o)));
  }
  JsonValue::Object root;
  root.emplace("event", JsonValue("policy_changed"));
  root.emplace("contract_version", JsonValue(static_cast<int64_t>(kContractVersion)));
  root.emplace("identity", JsonValue(identity));
  root.emplace("site", JsonValue(site));
  root.emplace("previous_trust", JsonValue(previous_trust));
  root.emplace("new_trust", JsonValue(new_trust));
  root.emplace("deltas", JsonValue(std::move(darr)));
  root.emplace("summary", JsonValue(BuildSummary(deltas)));
  root.emplace("undo", JsonValue(undo_allowed));
  return JsonValue(std::move(root));
}

}  // namespace xr::policy
