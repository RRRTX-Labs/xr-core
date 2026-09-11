// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/core/apply — see apply.h. Refusal tokens closed
// vocabulary; mirrored byte-for-byte by the Python fake.
#include "shield/core/apply.h"

#include <cstdio>
#include <utility>

namespace xr::shield {

using common::JsonValue;

namespace {

bool ParseSlot(const JsonValue& v, const char* name, BundleSlot* slot,
               std::string* detail) {
  *slot = BundleSlot{};
  if (!v.is_object()) {
    *detail = std::string("slot-not-object:") + name;
    return false;
  }
  static const char* kSlot[] = {"present", "bundle_id", "digest", "version"};
  for (const auto& kv : v.as_object()) {
    bool ok = false;
    for (const char* a : kSlot) {
      if (kv.first == a) { ok = true; break; }
    }
    if (!ok) {
      *detail = "unknown-field:" + kv.first;
      return false;
    }
  }
  const JsonValue* p = v.find("present");
  if (p == nullptr || !p->is_bool()) {
    *detail = std::string("bad-present:") + name;
    return false;
  }
  slot->present = p->as_bool();
  if (!slot->present) return true;  // empty slot: nothing else required
  const JsonValue* b = v.find("bundle_id");
  const JsonValue* d = v.find("digest");
  const JsonValue* ver = v.find("version");
  if (b == nullptr || !b->is_string() || b->as_string().empty() ||
      d == nullptr || !d->is_string() || d->as_string().size() != 64 ||
      ver == nullptr || !ver->is_int() || ver->as_int() < 1) {
    *detail = std::string("bad-slot-fields:") + name;
    return false;
  }
  slot->bundle_id = b->as_string();
  slot->digest = d->as_string();
  slot->version = ver->as_int();
  return true;
}

}  // namespace

StateInvariant CheckInvariants(const ApplyState& state, std::string* detail) {
  if (state.pins.size() > 2) {
    *detail = "too-many-pins";
    return StateInvariant::kTooManyPins;
  }
  if (state.active.present) {
    bool pinned = false;
    for (const auto& pin : state.pins) {
      if (pin.present && pin.bundle_id == state.active.bundle_id &&
          pin.version == state.active.version) {
        pinned = true;
        break;
      }
    }
    if (!pinned) {
      *detail = "pins-missing-active";  // the hot-pin-out refusal
      return StateInvariant::kHotPinOut;
    }
  }
  if (state.lkg.present && state.active.present &&
      state.lkg.bundle_id == state.active.bundle_id &&
      state.lkg.version == state.active.version) {
    *detail = "lkg-duplicates-active";
    return StateInvariant::kLkgEqualsActiveSameSlot;
  }
  return StateInvariant::kOk;
}

bool ParseApplyState(const JsonValue& args, ApplyState* out,
                     std::string* detail) {
  *out = ApplyState{};
  if (!args.is_object()) {
    *detail = "state-args-not-object";
    return false;
  }
  static const char* kTop[] = {"state"};
  for (const auto& kv : args.as_object()) {
    bool ok = false;
    for (const char* a : kTop) {
      if (kv.first == a) { ok = true; break; }
    }
    if (!ok) {
      *detail = "unknown-field:" + kv.first;
      return false;
    }
  }
  const JsonValue* st = args.find("state");
  if (st == nullptr || !st->is_object()) {
    *detail = "state-not-object";
    return false;
  }
  static const char* kState[] = {"active", "lkg", "pins", "last_apply_mono"};
  for (const auto& kv : st->as_object()) {
    bool ok = false;
    for (const char* a : kState) {
      if (kv.first == a) { ok = true; break; }
    }
    if (!ok) {
      *detail = "unknown-field:" + kv.first;
      return false;
    }
  }
  const JsonValue* act = st->find("active");
  if (act == nullptr || !ParseSlot(*act, "active", &out->active, detail))
    return false;
  const JsonValue* lkg = st->find("lkg");
  if (lkg == nullptr || !ParseSlot(*lkg, "lkg", &out->lkg, detail))
    return false;
  const JsonValue* pins = st->find("pins");
  if (pins == nullptr || !pins->is_array()) {
    *detail = "pins-not-array";
    return false;
  }
  for (const JsonValue& pv : pins->as_array()) {
    BundleSlot slot;
    char nm[16];
    snprintf(nm, sizeof(nm), "pins[%zu]", out->pins.size());
    if (!ParseSlot(pv, nm, &slot, detail)) return false;
    out->pins.push_back(std::move(slot));
  }
  const JsonValue* lam = st->find("last_apply_mono");
  if (lam == nullptr || !lam->is_int() || lam->as_int() < -1) {
    *detail = "bad-last-apply-mono";
    return false;
  }
  out->last_apply_mono = lam->as_int();
  return true;
}

ApplyResult ApplyBundle(const NormalizedBundle& candidate,
                        const ApplyState& state, long long now_mono,
                        ApplyState* out, std::string* detail) {
  *out = state;  // start from a copy; refusals leave the caller's state
  if (state.last_apply_mono >= 0 && now_mono < state.last_apply_mono) {
    *detail = "stale-apply-clock";
    return ApplyResult::kRejectedStaleClock;
  }
  if (state.active.present) {
    if (candidate.name == state.active.bundle_id) {
      if (candidate.bundle_version < state.active.version) {
        *detail = "bundle-version-downgrade";
        return ApplyResult::kRejectedDowngrade;
      }
      if (candidate.bundle_version == state.active.version) {
        *detail = "bundle-version-equal-reoffer";
        return ApplyResult::kRejectedEqualReoffer;
      }
    }
    // activation: the old active becomes LKG; pins keep last-two
    out->lkg = state.active;
  }
  BundleSlot ns;
  ns.present = true;
  ns.bundle_id = candidate.name;  // the normalized doc's name IS the slot id
  ns.digest = candidate.digest;
  ns.version = candidate.bundle_version;
  out->active = ns;
  out->pins.clear();
  if (out->lkg.present) out->pins.push_back(out->lkg);
  out->pins.push_back(ns);
  out->last_apply_mono = now_mono;
  return ApplyResult::kOk;
}

JsonValue SlotToJson(const BundleSlot& slot) {
  JsonValue::Object o{{"present", JsonValue(slot.present)}};
  if (slot.present) {
    o["bundle_id"] = JsonValue(slot.bundle_id);
    o["digest"] = JsonValue(slot.digest);
    o["version"] = JsonValue(static_cast<int>(slot.version));
  }
  return JsonValue(o);
}

JsonValue ApplyStateToJson(const ApplyState& state) {
  JsonValue::Array pins;
  for (const auto& p : state.pins) pins.push_back(SlotToJson(p));
  JsonValue::Object o{
      {"active", SlotToJson(state.active)},
      {"last_apply_mono", JsonValue(static_cast<int>(state.last_apply_mono))},
      {"lkg", SlotToJson(state.lkg)},
      {"pins", JsonValue(pins)},
  };
  return JsonValue(o);
}

}  // namespace xr::shield
