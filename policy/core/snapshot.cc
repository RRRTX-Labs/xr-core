// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — snapshot codec implementation (see snapshot.h).
#include "policy/core/snapshot.h"

#include <algorithm>
#include <map>

#include "policy/core/json.h"
#include "policy/core/sha256.h"

namespace xr::policy {
namespace {

constexpr const char* kSchema = "xr-policy-snapshot";
constexpr int kSchemaVersion = 1;

const char* ToStringImpl(SnapshotError e) {
  switch (e) {
    case SnapshotError::kNone: return "kNone";
    case SnapshotError::kMalformedInput: return "kMalformedInput";
    case SnapshotError::kBudgetExceeded: return "kBudgetExceeded";
    case SnapshotError::kHashMismatch: return "kHashMismatch";
    case SnapshotError::kVersionMismatch: return "kVersionMismatch";
    case SnapshotError::kInvalidEntry: return "kInvalidEntry";
  }
  return "kMalformedInput";
}

}  // namespace

const char* ToString(SnapshotError e) { return ToStringImpl(e); }

void SortEntries(std::vector<SnapshotEntry>* entries) {
  std::sort(entries->begin(), entries->end(),
            [](const SnapshotEntry& a, const SnapshotEntry& b) { return a.KeyLess(b); });
}

namespace {

std::string EntriesHash(const std::vector<SnapshotEntry>& entries) {
  // Hash over the canonical serialization of the sorted state.
  JsonValue::Array arr;
  for (const auto& e : entries) {
    JsonValue::Object o;
    o.emplace("identity", JsonValue(e.identity));
    o.emplace("site", JsonValue(e.site));
    o.emplace("trust", JsonValue(e.trust));
    o.emplace("policy", e.policy.ToJson());
    arr.push_back(JsonValue(std::move(o)));
  }
  JsonValue::Object h;
  h.emplace("entries", JsonValue(std::move(arr)));
  return Sha256Hex(JsonValue(std::move(h)).Canonical());
}

JsonValue EntryToJson(const SnapshotEntry& e) {
  JsonValue::Object o;
  o.emplace("identity", JsonValue(e.identity));
  o.emplace("site", JsonValue(e.site));
  o.emplace("trust", JsonValue(e.trust));
  o.emplace("policy", e.policy.ToJson());
  return JsonValue(std::move(o));
}

struct EntryKey {
  std::string identity, site, trust;
  bool operator<(const EntryKey& o) const {
    if (identity != o.identity) return identity < o.identity;
    if (site != o.site) return site < o.site;
    return trust < o.trust;
  }
};

EntryKey KeyOf(const SnapshotEntry& e) { return EntryKey{e.identity, e.site, e.trust}; }

SnapshotEncodeResult FinishBlob(uint64_t seq, uint64_t base_seq, const char* kind,
                                JsonValue payload, const std::vector<SnapshotEntry>& state) {
  SnapshotEncodeResult r;
  JsonValue::Object root;
  root.emplace("schema", JsonValue(kSchema));
  root.emplace("schema_version", JsonValue(kSchemaVersion));
  root.emplace("kind", JsonValue(kind));
  root.emplace("seq", JsonValue(static_cast<int64_t>(seq)));
  root.emplace("base_seq", JsonValue(static_cast<int64_t>(base_seq)));
  root.emplace("hash", JsonValue(EntriesHash(state)));  // hash of RESULT state
  root.emplace(kind == std::string("full") ? "entries" : "delta", std::move(payload));
  r.blob = JsonValue(std::move(root)).Canonical();
  if (r.blob.size() > kSnapshotBudgetBytes) {
    r.error = SnapshotError::kBudgetExceeded;
    r.error_detail = "encoded snapshot is " + std::to_string(r.blob.size()) +
                     " bytes; budget is " + std::to_string(kSnapshotBudgetBytes);
    r.blob.clear();
    return r;
  }
  r.ok = true;
  return r;
}

}  // namespace

SnapshotEncodeResult EncodeFullSnapshot(uint64_t seq, std::vector<SnapshotEntry> entries) {
  SortEntries(&entries);
  JsonValue::Array arr;
  for (const auto& e : entries) arr.push_back(EntryToJson(e));
  return FinishBlob(seq, seq, "full", JsonValue(std::move(arr)), entries);
}

SnapshotEncodeResult EncodeDiffSnapshot(uint64_t seq, uint64_t base_seq,
                                        const std::vector<SnapshotEntry>& base,
                                        std::vector<SnapshotEntry> entries) {
  SortEntries(&entries);
  std::map<EntryKey, const SnapshotEntry*> base_map;
  for (const auto& b : base) base_map[KeyOf(b)] = &b;

  JsonValue::Array added, changed;
  JsonValue::Array removed;
  for (const auto& e : entries) {
    auto it = base_map.find(KeyOf(e));
    if (it == base_map.end()) {
      added.push_back(EntryToJson(e));
    } else if (!(it->second->policy == e.policy)) {
      changed.push_back(EntryToJson(e));
    }
  }
  std::map<EntryKey, bool> new_map;
  for (const auto& e : entries) new_map[KeyOf(e)] = true;
  for (const auto& [k, _] : base_map) {
    if (new_map.find(k) == new_map.end()) {
      JsonValue::Object o;
      o.emplace("identity", JsonValue(k.identity));
      o.emplace("site", JsonValue(k.site));
      o.emplace("trust", JsonValue(k.trust));
      removed.push_back(JsonValue(std::move(o)));
    }
  }
  JsonValue::Object delta;
  delta.emplace("added", JsonValue(std::move(added)));
  delta.emplace("changed", JsonValue(std::move(changed)));
  delta.emplace("removed", JsonValue(std::move(removed)));
  return FinishBlob(seq, base_seq, "diff", JsonValue(std::move(delta)), entries);
}

namespace {

bool ParseEntry(const JsonValue& v, SnapshotEntry* out) {
  if (!v.is_object()) return false;
  const JsonValue* identity = v.find("identity");
  const JsonValue* site = v.find("site");
  const JsonValue* trust = v.find("trust");
  const JsonValue* policy = v.find("policy");
  if (identity == nullptr || !identity->is_string()) return false;
  if (site == nullptr || !site->is_string()) return false;
  if (trust == nullptr || !trust->is_string()) return false;
  if (policy == nullptr) return false;
  auto p = EffectivePolicy::FromJson(*policy);
  if (!p) return false;
  out->identity = identity->as_string();
  out->site = site->as_string();
  out->trust = trust->as_string();
  out->policy = *p;
  return true;
}

bool ParseEnvelope(const JsonValue& root, const char* want_kind, uint64_t* seq,
                   uint64_t* base_seq, std::string* hash, std::string* detail) {
  if (!root.is_object()) { *detail = "root not an object"; return false; }
  // additionalProperties: false — exactly the 7 frozen envelope keys.
  if (root.as_object().size() != 7) {
    *detail = "envelope has unexpected fields (additionalProperties: false)";
    return false;
  }
  const JsonValue* schema = root.find("schema");
  if (schema == nullptr || !schema->is_string() || schema->as_string() != kSchema) {
    *detail = "unknown schema";
    return false;
  }
  const JsonValue* sv = root.find("schema_version");
  if (sv == nullptr || !sv->is_int() || sv->as_int() != kSchemaVersion) {
    *detail = "unknown schema_version";
    return false;
  }
  const JsonValue* kind = root.find("kind");
  if (kind == nullptr || !kind->is_string() || kind->as_string() != want_kind) {
    *detail = std::string("expected kind ") + want_kind;
    return false;
  }
  const JsonValue* s = root.find("seq");
  if (s == nullptr || !s->is_int() || s->as_int() < 0) { *detail = "bad seq"; return false; }
  const JsonValue* b = root.find("base_seq");
  if (b == nullptr || !b->is_int() || b->as_int() < 0) { *detail = "bad base_seq"; return false; }
  const JsonValue* h = root.find("hash");
  if (h == nullptr || !h->is_string() || h->as_string().size() != 64) {
    *detail = "bad hash";
    return false;
  }
  *seq = static_cast<uint64_t>(s->as_int());
  *base_seq = static_cast<uint64_t>(b->as_int());
  *hash = h->as_string();
  return true;
}

}  // namespace

SnapshotDecodeResult DecodeFullSnapshot(const std::string& blob) {
  SnapshotDecodeResult r;
  auto parsed = ParseJson(blob);
  if (!parsed.ok) {
    r.error = SnapshotError::kMalformedInput;
    r.error_detail = "JSON parse: " + parsed.error;
    return r;
  }
  uint64_t seq = 0, base_seq = 0;
  std::string hash;
  if (!ParseEnvelope(parsed.value, "full", &seq, &base_seq, &hash, &r.error_detail)) {
    r.error = SnapshotError::kMalformedInput;
    return r;
  }
  const JsonValue* entries = parsed.value.find("entries");
  if (entries == nullptr || !entries->is_array()) {
    r.error = SnapshotError::kMalformedInput;
    r.error_detail = "full snapshot: entries missing";
    return r;
  }
  for (const auto& e : entries->as_array()) {
    SnapshotEntry out;
    if (!ParseEntry(e, &out)) {
      r.error = SnapshotError::kInvalidEntry;
      r.error_detail = "entry failed strict EffectivePolicy validation";
      return r;
    }
    r.entries.push_back(std::move(out));
  }
  SortEntries(&r.entries);
  if (EntriesHash(r.entries) != hash) {
    r.error = SnapshotError::kHashMismatch;
    r.error_detail = "integrity hash mismatch (corrupt or tampered blob)";
    r.entries.clear();
    return r;
  }
  r.seq = seq;
  r.base_seq = base_seq;
  r.ok = true;
  return r;
}

SnapshotDecodeResult ApplyDiffSnapshot(const std::string& blob,
                                       const std::vector<SnapshotEntry>& base) {
  SnapshotDecodeResult r;
  auto parsed = ParseJson(blob);
  if (!parsed.ok) {
    r.error = SnapshotError::kMalformedInput;
    r.error_detail = "JSON parse: " + parsed.error;
    return r;
  }
  uint64_t seq = 0, base_seq = 0;
  std::string hash;
  if (!ParseEnvelope(parsed.value, "diff", &seq, &base_seq, &hash, &r.error_detail)) {
    r.error = SnapshotError::kMalformedInput;
    return r;
  }
  const JsonValue* delta = parsed.value.find("delta");
  if (delta == nullptr || !delta->is_object()) {
    r.error = SnapshotError::kMalformedInput;
    r.error_detail = "diff snapshot: delta missing";
    return r;
  }
  std::map<EntryKey, SnapshotEntry> state;
  for (const auto& b : base) state[KeyOf(b)] = b;

  auto apply_list = [&](const char* field, bool is_removal) -> bool {
    const JsonValue* list = delta->find(field);
    if (list == nullptr) { r.error_detail = std::string("delta.") + field + " missing"; return false; }
    if (!list->is_array()) { r.error_detail = std::string("delta.") + field + " not array"; return false; }
    for (const auto& e : list->as_array()) {
      if (is_removal) {
        if (!e.is_object()) { r.error_detail = "removed entry not object"; return false; }
        const JsonValue* i = e.find("identity");
        const JsonValue* s = e.find("site");
        const JsonValue* t = e.find("trust");
        if (i == nullptr || !i->is_string() || s == nullptr || !s->is_string() ||
            t == nullptr || !t->is_string()) {
          r.error_detail = "removed entry missing key fields";
          return false;
        }
        state.erase(EntryKey{i->as_string(), s->as_string(), t->as_string()});
      } else {
        SnapshotEntry out;
        if (!ParseEntry(e, &out)) {
          r.error = SnapshotError::kInvalidEntry;
          r.error_detail = "delta entry failed strict validation";
          return false;
        }
        state[KeyOf(out)] = std::move(out);
      }
    }
    return true;
  };
  if (!apply_list("added", false)) {
    if (r.error == SnapshotError::kNone) r.error = SnapshotError::kMalformedInput;
    return r;
  }
  if (!apply_list("changed", false)) {
    if (r.error == SnapshotError::kNone) r.error = SnapshotError::kMalformedInput;
    return r;
  }
  if (!apply_list("removed", true)) {
    if (r.error == SnapshotError::kNone) r.error = SnapshotError::kMalformedInput;
    return r;
  }
  for (const auto& [_, e] : state) r.entries.push_back(e);
  SortEntries(&r.entries);
  if (EntriesHash(r.entries) != hash) {
    r.error = SnapshotError::kHashMismatch;
    r.error_detail = "reconstructed state hash mismatch (wrong base or corrupt diff)";
    r.entries.clear();
    return r;
  }
  r.seq = seq;
  r.base_seq = base_seq;
  r.ok = true;
  return r;
}

}  // namespace xr::policy
