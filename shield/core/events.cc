// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/events — see events.h. The canonical event JSON is
// the FROZEN mojom shape; the Python fake mirrors these bytes exactly.
#include "shield/core/events.h"

#include <utility>

namespace xr::shield {

using common::JsonValue;

const char* BlockActionName(BlockAction a) {
  switch (a) {
    case BlockAction::kBlocked: return "kBlocked";
    case BlockAction::kAllowed: return "kAllowed";
    case BlockAction::kRedirected: return "kRedirected";
    case BlockAction::kUpgraded: return "kUpgraded";
  }
  return "kBlocked";  // unreachable; keeps -Werror switches honest
}

std::string RedactTarget(const UrlParts& parts) {
  // scheme://host/path — query and fragment never reach the ledger
  return parts.scheme + "://" + parts.host + parts.path;
}

BlockEvent MakeEvent(const RequestContext& ctx, long long tab_id,
                     long long ts_millis, const std::string& rule,
                     const std::string& list_id, BlockAction action) {
  BlockEvent e;
  e.ts_millis = ts_millis;
  e.identity = ctx.identity.value;
  e.tab_id = tab_id;
  e.origin = ctx.origin;
  e.target = RedactTarget(ctx.parts);
  e.rule = rule;
  e.list_provenance = list_id;
  e.action = action;
  e.request_class = ctx.request_class;
  return e;
}

void RingAppend(EventRing* ring, BlockEvent event) {
  ring->events.push_back(std::move(event));
  while (ring->events.size() > EventRing::kCapacity)
    ring->events.erase(ring->events.begin());  // FIFO eviction
}

JsonValue EventToJson(const BlockEvent& e) {
  JsonValue::Object o{
      {"action", JsonValue(BlockActionName(e.action))},
      {"identity", JsonValue(JsonValue::Object{{"value", JsonValue(e.identity)}})},
      {"list_provenance", JsonValue(e.list_provenance)},
      {"origin", JsonValue(JsonValue::Object{
                     {"registrable_domain", JsonValue(e.origin.registrable_domain)},
                     {"scheme", JsonValue(e.origin.scheme)},
                 })},
      {"request_class", JsonValue(RequestClassName(e.request_class))},
      {"rule", JsonValue(e.rule)},
      {"tab_id", JsonValue(static_cast<int>(e.tab_id))},
      {"target", JsonValue(e.target)},
      {"ts_millis", JsonValue(static_cast<int>(e.ts_millis))},
  };
  return JsonValue(o);
}

std::vector<JsonValue> RingView(const EventRing& ring,
                                const std::string& identity, int max_events) {
  std::vector<JsonValue> out;
  size_t budget = 0;
  const int cap = max_events < 0 ? 0 : max_events;
  for (size_t i = ring.events.size(); i-- > 0;) {
    if (static_cast<int>(out.size()) >= cap) break;
    const BlockEvent& e = ring.events[i];
    if (!identity.empty() && e.identity != identity) continue;
    JsonValue j = EventToJson(e);
    size_t sz = j.Canonical().size();
    if (!out.empty() && budget + sz > kChunkBudgetBytes) break;
    budget += sz;
    out.push_back(std::move(j));
  }
  return out;
}

JsonValue RingToJson(const EventRing& ring) {
  JsonValue::Array a;
  for (const auto& e : ring.events) a.push_back(EventToJson(e));
  return JsonValue(a);
}

bool ParseRing(const JsonValue& v, EventRing* out, std::string* detail) {
  *out = EventRing{};
  if (!v.is_array()) {
    *detail = "ring-not-array";
    return false;
  }
  static const char* kEv[] = {"ts_millis", "identity", "tab_id", "origin",
                              "target", "rule", "list_provenance", "action",
                              "request_class"};
  for (const JsonValue& ev : v.as_array()) {
    if (!ev.is_object()) {
      *detail = "event-not-object";
      return false;
    }
    for (const auto& kv : ev.as_object()) {
      bool ok = false;
      for (const char* a : kEv) {
        if (kv.first == a) { ok = true; break; }
      }
      if (!ok) {
        *detail = "unknown-field:" + kv.first;
        return false;
      }
    }
    BlockEvent e;
    const JsonValue* ts = ev.find("ts_millis");
    const JsonValue* idv = ev.find("identity");
    const JsonValue* tab = ev.find("tab_id");
    const JsonValue* org = ev.find("origin");
    const JsonValue* tgt = ev.find("target");
    const JsonValue* rule = ev.find("rule");
    const JsonValue* prov = ev.find("list_provenance");
    const JsonValue* act = ev.find("action");
    const JsonValue* rc = ev.find("request_class");
    if (ts == nullptr || !ts->is_int() || tab == nullptr || !tab->is_int() ||
        idv == nullptr || !idv->is_object() || tgt == nullptr ||
        !tgt->is_string() || rule == nullptr || !rule->is_string() ||
        prov == nullptr || !prov->is_string() || act == nullptr ||
        !act->is_string() || rc == nullptr || !rc->is_string() ||
        org == nullptr || !org->is_object()) {
      *detail = "bad-event-field";
      return false;
    }
    e.ts_millis = ts->as_int();
    e.tab_id = tab->as_int();
    const JsonValue* idval = idv->find("value");
    const JsonValue* osch = org->find("scheme");
    const JsonValue* ord = org->find("registrable_domain");
    if (idval == nullptr || !idval->is_string() || osch == nullptr ||
        !osch->is_string() || ord == nullptr || !ord->is_string()) {
      *detail = "bad-event-nested";
      return false;
    }
    e.identity = idval->as_string();
    e.origin.scheme = osch->as_string();
    e.origin.registrable_domain = ord->as_string();
    e.target = tgt->as_string();
    e.rule = rule->as_string();
    e.list_provenance = prov->as_string();
    std::string an = act->as_string();
    if (an == "kBlocked") e.action = BlockAction::kBlocked;
    else if (an == "kAllowed") e.action = BlockAction::kAllowed;
    else if (an == "kRedirected") e.action = BlockAction::kRedirected;
    else if (an == "kUpgraded") e.action = BlockAction::kUpgraded;
    else { *detail = "unknown-block-action:" + an; return false; }
    std::string rcn = rc->as_string();
    if (!RequestClassFromName(rcn, &e.request_class)) {
      *detail = "unknown-request-class:" + rcn;
      return false;
    }
    RingAppend(out, std::move(e));
  }
  return true;
}

}  // namespace xr::shield
