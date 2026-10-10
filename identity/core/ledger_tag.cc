// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// identity/core/ledger_tag — see ledger_tag.h for the five laws.
#include "core/ledger_tag.h"

#include <utility>

#include "core/mint.h"

namespace xr::identity {

using xr::common::JsonValue;

namespace {

// The identity id of a row/request, validated. Returns "" with a refusal
// code in *why when the id is absent, empty, not a string or not the minted
// domain shape. Never substitutes a default (L1).
std::string ValidId(const JsonValue& obj, std::string* why) {
  const JsonValue* v = obj.is_object() ? obj.find("identity_id") : nullptr;
  if (v == nullptr || !v->is_string() || v->as_string().empty()) {
    *why = "identity-id-required";
    return {};
  }
  if (!DomainShapeOk(v->as_string())) {
    *why = "identity-id-malformed";
    return {};
  }
  return v->as_string();
}

// The upstream history row exactly as upstream would store it. The overlay
// must never add, drop or reorder a field here (L5).
JsonValue UpstreamRow(const JsonValue& row) {
  JsonValue::Object o;
  for (const char* key : {"url_id", "visit_id", "url", "title", "ts"}) {
    const JsonValue* v = row.find(key);
    if (v != nullptr) o[key] = *v;
  }
  return JsonValue(std::move(o));
}

// The history write path for one user. `overlay` may be null (overlay off).
void WriteHistory(const JsonValue::Array& rows, const std::string& identity,
                  JsonValue::Array* upstream, JsonValue::Array* overlay) {
  for (const JsonValue& row : rows) {
    upstream->push_back(UpstreamRow(row));
    if (overlay == nullptr) continue;
    JsonValue::Object tag;
    tag["identity_id"] = JsonValue(identity);
    const JsonValue* url_id = row.find("url_id");
    const JsonValue* visit_id = row.find("visit_id");
    tag["url_id"] = url_id ? *url_id : JsonValue();
    tag["visit_id"] = visit_id ? *visit_id : JsonValue();
    overlay->push_back(JsonValue(std::move(tag)));
  }
}

}  // namespace

const std::vector<std::string>& LedgerEventClasses() {
  static const std::vector<std::string> kClasses = {
      "kPolicy", "kBlock",    "kPermission", "kIdentity", "kRoute",
      "kVault",  "kGuard",    "kDownload",   "kHistory",  "kBookmark"};
  return kClasses;
}

bool IsLedgerEventClass(std::string_view cls) {
  for (const std::string& c : LedgerEventClasses()) {
    if (c == cls) return true;
  }
  return false;
}

bool TagEvent(const JsonValue& request, JsonValue* out, std::string* refusal) {
  std::string why;
  const std::string id = ValidId(request, &why);
  if (id.empty()) {
    *refusal = why;
    return false;
  }
  const JsonValue* cls = request.find("event_class");
  const std::string cls_s =
      (cls != nullptr && cls->is_string()) ? cls->as_string() : std::string();
  if (!IsLedgerEventClass(cls_s)) {
    *refusal = "unknown-event-class:" + cls_s;
    return false;
  }
  const JsonValue* event = request.find("event");
  if (event == nullptr || !event->is_object()) {
    *refusal = "event-not-object";
    return false;
  }
  JsonValue::Object rec;
  rec["event"] = *event;
  rec["event_class"] = JsonValue(cls_s);
  rec["identity_id"] = JsonValue(id);
  rec["schema"] = JsonValue(std::string(kLedgerOverlaySchema));
  *out = JsonValue(std::move(rec));
  return true;
}

bool FilterOmnibox(const JsonValue& request, JsonValue* out,
                   std::string* refusal) {
  std::string why;
  const std::string current = ValidId(request, &why);
  if (current.empty()) {
    *refusal = why;
    return false;
  }
  const JsonValue* rows = request.find("rows");
  if (rows == nullptr || !rows->is_array()) {
    *refusal = "rows-not-array";
    return false;
  }
  JsonValue::Array mine, unknown;
  for (const JsonValue& row : rows->as_array()) {
    if (!row.is_object()) continue;  // not a row: nothing to show
    std::string row_why;
    const std::string owner = ValidId(row, &row_why);
    if (owner.empty()) {
      JsonValue::Object u = row.as_object();
      u.erase("identity_id");
      u["provenance"] = JsonValue("unknown");
      unknown.push_back(JsonValue(std::move(u)));
    } else if (owner == current) {
      mine.push_back(row);
    }
    // owner != current: dropped and NOT counted (L4).
  }
  JsonValue::Object res;
  res["current"] = JsonValue(std::move(mine));
  res["identity_id"] = JsonValue(current);
  res["unknown"] = JsonValue(std::move(unknown));
  *out = JsonValue(std::move(res));
  return true;
}

bool HistoryOracle(const JsonValue& request, JsonValue* out,
                   std::string* refusal) {
  std::string why;
  const std::string id = ValidId(request, &why);
  if (id.empty()) {
    *refusal = why;
    return false;
  }
  const JsonValue* rows = request.find("upstream_rows");
  if (rows == nullptr || !rows->is_array()) {
    *refusal = "upstream-rows-not-array";
    return false;
  }
  for (const JsonValue& row : rows->as_array()) {
    if (!row.is_object()) {
      *refusal = "upstream-row-not-object";
      return false;
    }
    for (const auto& kv : row.as_object()) {
      if (kv.first != "url_id" && kv.first != "visit_id" && kv.first != "url" &&
          kv.first != "title" && kv.first != "ts") {
        *refusal = "upstream-row-unknown-field:" + kv.first;
        return false;
      }
    }
  }
  JsonValue::Array before, after, overlay;
  WriteHistory(rows->as_array(), id, &before, nullptr);   // overlay off
  WriteHistory(rows->as_array(), id, &after, &overlay);   // overlay on
  const std::string b = JsonValue(before).Canonical();
  const std::string a = JsonValue(after).Canonical();
  JsonValue::Object res;
  res["overlay_rows"] = JsonValue(static_cast<int64_t>(overlay.size()));
  res["upstream_bytes_after"] = JsonValue(static_cast<int64_t>(a.size()));
  res["upstream_bytes_before"] = JsonValue(static_cast<int64_t>(b.size()));
  // Two comparisons: overlay-on vs overlay-off, AND both vs the rows exactly
  // as handed in (upstream's own bytes). Either delta is a DELTA.
  const std::string input = rows->Canonical();
  res["verdict"] = JsonValue(a == b && a == input ? "diff-clean" : "DELTA");
  *out = JsonValue(std::move(res));
  return true;
}

}  // namespace xr::identity
