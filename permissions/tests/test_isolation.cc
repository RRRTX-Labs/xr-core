// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P15 T9.1 — cross-identity negatives. Identity A's operations must leave
// identity B's record AND B's projected view slice byte-identical. The grid
// spans every capability, every B default state (unset/allow/deny/ask), and
// every identity-keyed operation. A planted identity-ignoring lookup must be
// caught by the same grid: otherwise the property has no teeth.
#include <cstdint>
#include <string>
#include <vector>

#include "permissions/core/json.h"
#include "permissions/core/ops.h"
#include "permissions/core/store.h"
#include "policy/tests/harness.h"

using namespace xr::permissions;

namespace {

const char* kA = "xr:00000000-0000-4000-8000-0000000000a1";
const char* kB = "xr:00000000-0000-4000-8000-0000000000b2";
constexpr int64_t kT0 = 9000000;
const char* kCaps[] = {"geolocation", "camera", "microphone", "notifications"};
const char* kStates[] = {"unset", "kAllow", "kDeny", "kAsk"};

GrantRequest G(const char* identity, const char* cap, const char* scope, const char* session = "") {
  GrantRequest r;
  r.identity = identity;
  r.domain = "example.com";  // the SAME site for both identities: the hard case
  r.capability = cap;
  r.scope = scope;
  r.session_id = session;
  return r;
}

// B's entire observable state: the raw record plus its projected slice.
std::string BDigest(const Store& s) {
  std::string out;
  auto it = s.identities.find(kB);
  if (it == s.identities.end()) {
    out = "absent";
  } else {
    const IdentityRecord& r = it->second;
    out += r.denied ? "denied;" : "live;";
    for (const auto& [c, st] : r.defaults) out += std::string(CapabilityName(c)) + "=" + CapStateName(st) + ";";
    for (const auto& g : r.grants) {
      out += g.id + "/" + ScopeName(g.scope) + "/" + std::to_string(g.remaining_uses) + "/" +
             std::to_string(g.expires_at) + "/" + g.session_id + "/" + (g.revoked ? "R" : "L") + ";";
    }
  }
  auto pr = ParseJson(ProjectViewJson(s));
  const JsonValue& v = pr.value;
  for (const char* arr : {"denied_identities", "defaults", "grants"}) {
    const JsonValue* a = v.find(arr);
    if (a == nullptr || !a->is_array()) continue;
    for (const auto& e : a->as_array()) {
      if (e.is_string()) {
        if (e.as_string() == kB) out += "|view-denied";
      } else {
        const JsonValue* ident = e.find("identity");
        if (ident != nullptr && ident->is_string() && ident->as_string() == kB) out += "|view:" + e.Canonical();
      }
    }
  }
  return out;
}

// Real lookup: keyed on the identity. "unset" when that identity has no default.
std::string RealLookup(const Store& s, const char* identity, const char* cap) {
  auto it = s.identities.find(identity);
  if (it == s.identities.end()) return "unset";
  Capability c;
  if (!ParseCapability(cap, &c)) return "unset";
  auto d = it->second.defaults.find(c);
  return d == it->second.defaults.end() ? "unset" : CapStateName(d->second);
}

// PLANTED defect: ignores the identity and reads the first identity in key
// order that has a default. Used only to prove the grid is sensitive.
std::string MutantLookup(const Store& s, const char* cap) {
  Capability c;
  if (!ParseCapability(cap, &c)) return "unset";
  for (const auto& [key, rec] : s.identities) {
    (void)key;
    auto d = rec.defaults.find(c);
    if (d != rec.defaults.end()) return CapStateName(d->second);
  }
  return "unset";
}

Store Build(const char* b_cap, const char* b_state) {
  Store s;
  s = GrantTemp(s, G(kA, "camera", "once"), kT0).store;          // A's once: g1
  s = GrantTemp(s, G(kB, "geolocation", "once"), kT0).store;     // B's once: g2
  s = GrantTemp(s, G(kB, "camera", "7d"), kT0).store;            // B's 7d:   g3
  s = GrantTemp(s, G(kB, "microphone", "session", "sB"), kT0).store;  // B's session: g4
  s = SetDenied(s, kA, false, kT0).store;
  if (std::string(b_state) != "unset") {
    s = SetDefault(s, kB, b_cap, b_state, kT0).store;
  }
  return s;
}

std::vector<OpOutcome> OpsOnA(const Store& s) {
  std::vector<OpOutcome> ops;
  for (const char* cap : kCaps) {
    ops.push_back(SetDefault(s, kA, cap, "kAllow", kT0 + 1));
    ops.push_back(SetDefault(s, kA, cap, "kDeny", kT0 + 1));
    ops.push_back(GrantTemp(s, G(kA, cap, "once"), kT0 + 1));
    ops.push_back(GrantTemp(s, G(kA, cap, "7d"), kT0 + 1));
    ops.push_back(GrantTemp(s, G(kA, cap, "session", "sA"), kT0 + 1));
  }
  ops.push_back(SetDenied(s, kA, true, kT0 + 2));
  ops.push_back(SetDenied(s, kA, false, kT0 + 2));
  ops.push_back(RevokeSite(s, kA, "example.com", kT0 + 3));
  ops.push_back(RevokeAll(s, kA, kT0 + 4));
  ops.push_back(ConsumeOnce(s, "g1", kT0 + 5));  // A's own once grant
  return ops;
}

}  // namespace

int main() {
  int cells = 0;
  int mutant_detected = 0;
  int effectful_on_a = 0;
  for (const char* b_cap : kCaps) {
    for (const char* b_state : kStates) {
      const Store base = Build(b_cap, b_state);
      const std::string b_before = BDigest(base);
      const auto ops = OpsOnA(base);
      for (size_t i = 0; i < ops.size(); ++i) {
        const OpOutcome& o = ops[i];
        ++cells;
        XR_EXPECT_MSG(o.ok, "identity-A op applies (fixture is valid)");
        XR_EXPECT_MSG(BDigest(o.store) == b_before,
                      "cross-identity: A's operation must not change B's record or view slice");
        if (SerializeStore(o.store) != SerializeStore(base)) ++effectful_on_a;
        // Real lookup for B: unchanged for every op (the property above, per cell).
        XR_EXPECT_MSG(RealLookup(o.store, kB, b_cap) == RealLookup(base, kB, b_cap),
                      "real lookup for B is identity-keyed");
        if (MutantLookup(o.store, b_cap) != MutantLookup(base, b_cap)) ++mutant_detected;
      }
    }
  }
  XR_EXPECT_MSG(cells == 4 * 4 * 25, "grid covers every (B-config, A-op) cell");
  XR_EXPECT_MSG(effectful_on_a > 0, "non-vacuous: A's operations really changed A's state");
  XR_EXPECT_MSG(mutant_detected > 0,
                "the planted identity-ignoring lookup is caught by the grid (the property has teeth)");

  // A global sweep is keyed on time and session, not on identity: B's grants
  // survive a sweep whose clock and session do not end them.
  {
    const Store base = Build("camera", "kAllow");
    OpOutcome sw = SweepExpired(base, kT0 + 10, "sB");
    XR_EXPECT_MSG(BDigest(sw.store) == BDigest(base), "B survives a sweep with its own session active");
  }

  return xrtest::Report("test_isolation");
}
