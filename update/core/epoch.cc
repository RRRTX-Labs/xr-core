// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
#include "update/core/epoch.h"

namespace xr::update {

NoticeResult ParseRevocationNotice(const std::string& raw, RevocationNotice* out) {
  JsonParseResult pr = ParseJson(raw);
  if (!pr.ok) return NoticeResult::kMalformed;
  const JsonValue& doc = pr.value;
  if (!doc.is_object()) return NoticeResult::kMalformed;
  static const char* kKeys[] = {"schema", "schema_version", "epoch_id",
                                "key_id", "seq", "reason"};
  for (const auto& [k, _] : doc.as_object()) {
    bool ok = false;
    for (const char* a : kKeys) ok = ok || (k == a);
    if (!ok) return NoticeResult::kUnknownField;
  }
  const JsonValue* schema = doc.find("schema");
  const JsonValue* sv = doc.find("schema_version");
  const JsonValue* eid = doc.find("epoch_id");
  const JsonValue* kid = doc.find("key_id");
  const JsonValue* seq = doc.find("seq");
  const JsonValue* reason = doc.find("reason");
  if (!schema || !schema->is_string() || schema->as_string() != "xr-epoch-revocation")
    return NoticeResult::kMalformed;
  if (!sv || !sv->is_int() || sv->as_int() != 1) return NoticeResult::kMalformed;
  if (!eid || !eid->is_string() || eid->as_string().empty()) return NoticeResult::kMalformed;
  if (!kid || !kid->is_string() || kid->as_string().empty()) return NoticeResult::kMalformed;
  if (!seq || !seq->is_int() || seq->as_int() < 0) return NoticeResult::kMalformed;
  if (!reason || !reason->is_string()) return NoticeResult::kMalformed;
  out->epoch_id = eid->as_string();
  out->key_id = kid->as_string();
  out->seq = seq->as_int();
  out->reason = reason->as_string();
  out->canonical_body = doc.Canonical();
  return NoticeResult::kOk;
}

NoticeResult ApplyRevocation(const RevocationNotice& notice, EpochState* state,
                             bool* notice_applied) {
  if (notice_applied) *notice_applied = false;
  if (notice.seq < 0 || notice.epoch_id.empty()) return NoticeResult::kMalformed;
  // Stale (older than what we already know): ignored, not an error.
  if (state->seq >= 0 && notice.seq <= state->seq && !state->revoked) {
    return NoticeResult::kOk;
  }
  state->epoch_id = notice.epoch_id;
  state->key_id = notice.key_id;
  state->seq = notice.seq;
  state->revoked = true;
  state->manual_path = true;  // the kill switch: no auto path past this point
  if (notice_applied) *notice_applied = true;
  return NoticeResult::kOk;
}

bool EpochAcceptable(const EpochState& state, const std::string& epoch_id,
                     const std::string& key_id, long long seq) {
  if (state.manual_path || state.revoked) return false;
  if (epoch_id.empty() || key_id.empty() || seq < 0) return false;
  if (state.seq >= 0 && seq < state.seq) return false;  // epoch regression
  if (!state.epoch_id.empty() && epoch_id != state.epoch_id) return false;
  // NOTE: the KEY check is deliberately NOT here — a manifest claiming our
  // epoch under a different key is a key-substitution attempt and is
  // reported by the policy as unknown-signing-key (more precise, same
  // deny). EpochAcceptable judges epoch identity and ordering only.
  return true;
}

JsonValue EpochToJson(const EpochState& s) {
  return JsonValue(JsonValue::Object{
      {"epoch_id", JsonValue(s.epoch_id)},
      {"key_id", JsonValue(s.key_id)},
      {"manual_path", JsonValue(s.manual_path)},
      {"revoked", JsonValue(s.revoked)},
      {"seq", JsonValue(static_cast<int64_t>(s.seq))},
  });
}

void EpochFromJson(const JsonValue& v, EpochState* out) {
  out->epoch_id = v.find("epoch_id") ? v.find("epoch_id")->as_string() : "";
  out->key_id = v.find("key_id") ? v.find("key_id")->as_string() : "";
  out->seq = v.find("seq") ? v.find("seq")->as_int() : -1;
  out->revoked = v.find("revoked") ? v.find("revoked")->as_bool() : false;
  out->manual_path = v.find("manual_path") ? v.find("manual_path")->as_bool() : false;
}

}  // namespace xr::update
