// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — snapshot codec: full round-trip, diff chains
// (full→d1→d2 reconstructs exactly), the ≤32 KB budget (typed error, never
// truncation), and the corrupt-input battery (truncation, byte flips,
// field tampering, hash mismatch => typed error + deny, never a crash and
// never a guess).
#include <string>
#include <vector>

#include "policy/core/snapshot.h"
#include "harness.h"

using namespace xr::policy;

namespace {

SnapshotEntry Mk(const std::string& id, const std::string& site, const std::string& trust,
                 bool letterbox) {
  SnapshotEntry e;
  e.identity = id;
  e.site = site;
  e.trust = trust;
  e.policy.letterbox = letterbox;  // rest: deny-safe defaults
  return e;
}

bool SameState(const std::vector<SnapshotEntry>& a, const std::vector<SnapshotEntry>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (!a[i].SameKey(b[i])) return false;
    if (!(a[i].policy == b[i].policy)) return false;
  }
  return true;
}

}  // namespace

int main() {
  // ---- full round-trip ----
  {
    std::vector<SnapshotEntry> state{Mk("id1", "a.com", "kStandard", false),
                                     Mk("id1", "b.com", "kFortress", true),
                                     Mk("id2", "a.com", "kShield", false)};
    auto enc = EncodeFullSnapshot(7, state);
    XR_EXPECT(enc.ok);
    auto dec = DecodeFullSnapshot(enc.blob);
    XR_EXPECT(dec.ok);
    XR_EXPECT_EQ(dec.seq, 7u);
    XR_EXPECT(SameState(dec.entries, state));
  }

  // ---- diff chain: full -> d1 -> d2 reconstructs d2's state exactly ----
  {
    std::vector<SnapshotEntry> s0{Mk("id1", "a.com", "kStandard", false),
                                  Mk("id1", "b.com", "kFortress", true)};
    auto f0 = EncodeFullSnapshot(1, s0);
    XR_EXPECT(f0.ok);
    std::vector<SnapshotEntry> s1 = s0;
    s1.push_back(Mk("id2", "c.com", "kShield", false));       // added
    s1[0].policy.letterbox = true;                            // changed
    auto d1 = EncodeDiffSnapshot(2, 1, s0, s1);
    XR_EXPECT(d1.ok);
    auto r1 = ApplyDiffSnapshot(d1.blob, DecodeFullSnapshot(f0.blob).entries);
    XR_EXPECT(r1.ok);
    XR_EXPECT_MSG(SameState(r1.entries, s1), "d1 reconstructs s1");
    std::vector<SnapshotEntry> s2 = s1;
    s2.erase(s2.begin());  // removed entry 0
    auto d2 = EncodeDiffSnapshot(3, 2, s1, s2);
    XR_EXPECT(d2.ok);
    auto r2 = ApplyDiffSnapshot(d2.blob, r1.entries);
    XR_EXPECT(r2.ok);
    XR_EXPECT_MSG(SameState(r2.entries, s2), "d2 reconstructs s2");
    // And a direct full snapshot of s2 equals the chained reconstruction.
    auto f2 = EncodeFullSnapshot(3, s2);
    XR_EXPECT(f2.ok);
    auto dec2 = DecodeFullSnapshot(f2.blob);
    XR_EXPECT(SameState(dec2.entries, r2.entries));
  }

  // ---- diff applied to the WRONG base => hash mismatch (fail closed) ----
  {
    std::vector<SnapshotEntry> s0{Mk("id1", "a.com", "kStandard", false)};
    std::vector<SnapshotEntry> s1{Mk("id1", "a.com", "kStandard", true)};
    auto d = EncodeDiffSnapshot(2, 1, s0, s1);
    XR_EXPECT(d.ok);
    std::vector<SnapshotEntry> wrong_base{Mk("id9", "z.com", "kShield", false)};
    auto r = ApplyDiffSnapshot(d.blob, wrong_base);
    XR_EXPECT(!r.ok);
    XR_EXPECT(r.error == SnapshotError::kHashMismatch);
  }

  // ---- budget: over-budget state => typed error, NEVER truncation ----
  {
    std::vector<SnapshotEntry> big;
    for (int i = 0; i < 4000; ++i) big.push_back(Mk("id" + std::to_string(i), "s.com", "kStandard", false));
    auto enc = EncodeFullSnapshot(1, big);
    XR_EXPECT(!enc.ok);
    XR_EXPECT(enc.error == SnapshotError::kBudgetExceeded);
    XR_EXPECT(enc.blob.empty());
  }
  // Worst-case legal single-blob state: the largest entry count whose full
  // encoding still fits the 32 KB budget must round-trip byte-stable; one
  // entry beyond it => typed kBudgetExceeded (never truncation). Larger
  // live states distribute via incremental diffs (the diff format is the
  // contract's answer to state growth; documented in policy.md).
  {
    int max_fits = 0;
    std::vector<SnapshotEntry> probe;
    for (int n = 1; n <= 200; ++n) {
      probe.push_back(Mk("id" + std::to_string(n), "s" + std::to_string(n) + ".com", "kStandard", false));
      if (EncodeFullSnapshot(1, probe).ok) max_fits = n;
      else break;
    }
    XR_EXPECT_MSG(max_fits >= 30, "a full snapshot must carry a multi-context state "
                                  "(got only " + std::to_string(max_fits) + ")");
    std::vector<SnapshotEntry> fits;
    for (int i = 0; i < max_fits; ++i)
      fits.push_back(Mk("id" + std::to_string(i), "s" + std::to_string(i) + ".com", "kStandard", false));
    SortEntries(&fits);  // canonical order (lexicographic keys)
    auto enc = EncodeFullSnapshot(1, fits);
    XR_EXPECT(enc.ok);
    XR_EXPECT(enc.blob.size() <= kSnapshotBudgetBytes);
    auto dec = DecodeFullSnapshot(enc.blob);
    XR_EXPECT(dec.ok);
    XR_EXPECT(SameState(dec.entries, fits));
    // One beyond => typed error, blob empty (never truncated).
    fits.push_back(Mk("id_over", "over.com", "kStandard", false));
    auto over = EncodeFullSnapshot(1, fits);
    XR_EXPECT(!over.ok);
    XR_EXPECT(over.error == SnapshotError::kBudgetExceeded);
    XR_EXPECT(over.blob.empty());
  }

  // ---- corrupt-input battery: deny + typed error, never crash ----
  {
    std::vector<SnapshotEntry> s{Mk("id1", "a.com", "kStandard", false)};
    std::string good = EncodeFullSnapshot(1, s).blob;
    struct Case {
      std::string name;
      std::string blob;
      SnapshotError want;               // primary expectation
      bool deny_family_ok = false;      // any non-kNone typed error acceptable
    };
    std::vector<Case> cases;
    cases.push_back({"truncated", good.substr(0, good.size() / 2), SnapshotError::kMalformedInput});
    cases.push_back({"empty", "", SnapshotError::kMalformedInput});
    cases.push_back({"not-json", "{{{{", SnapshotError::kMalformedInput});
    {
      std::string b = good;
      b[b.size() / 2] = b[b.size() / 2] == 'a' ? 'b' : 'a';
      // A mid-blob flip can invalidate JSON OR only the hash — both are
      // typed deny outcomes; the LAW is: never ok, never a crash.
      cases.push_back({"byte-flip", b, SnapshotError::kMalformedInput, true});
    }
    {
      // Tamper via JSON-level mutation (length-safe): change an enum value
      // WITHOUT recomputing the hash. Entry validation fires first.
      auto p = ParseJson(good);
      XR_EXPECT(p.ok);
      JsonValue::Object root = p.value.as_object();
      JsonValue::Array entries = root["entries"].as_array();
      XR_EXPECT(!entries.empty());
      JsonValue::Object e0 = entries[0].as_object();
      JsonValue::Object pol = e0["policy"].as_object();
      JsonValue::Object eg = pol["egress"].as_object();
      eg["route"] = JsonValue(std::string("kTeleport"));
      pol["egress"] = JsonValue(std::move(eg));
      e0["policy"] = JsonValue(std::move(pol));
      entries[0] = JsonValue(std::move(e0));
      root["entries"] = JsonValue(std::move(entries));
      cases.push_back({"bad-enum", JsonValue(std::move(root)).Canonical(),
                       SnapshotError::kInvalidEntry});
    }
    {
      // export_allowed=true violates schema const_false (kInvalidEntry).
      auto p = ParseJson(good);
      XR_EXPECT(p.ok);
      JsonValue::Object root = p.value.as_object();
      JsonValue::Array entries = root["entries"].as_array();
      JsonValue::Object e0 = entries[0].as_object();
      JsonValue::Object pol = e0["policy"].as_object();
      JsonValue::Object vs = pol["vault_scope"].as_object();
      vs["export_allowed"] = JsonValue(true);
      pol["vault_scope"] = JsonValue(std::move(vs));
      e0["policy"] = JsonValue(std::move(pol));
      entries[0] = JsonValue(std::move(e0));
      root["entries"] = JsonValue(std::move(entries));
      cases.push_back({"export-true", JsonValue(std::move(root)).Canonical(),
                       SnapshotError::kInvalidEntry});
    }
    {
      // Unknown schema.
      std::string b = good;
      b.replace(b.find("xr-policy-snapshot"), 19, "xr-policy-snapsho2");
      cases.push_back({"unknown-schema", b, SnapshotError::kMalformedInput});
    }
    {
      // Future schema_version.
      std::string b = good;
      auto pos = b.find("\"schema_version\":1");
      XR_EXPECT(pos != std::string::npos);
      b.replace(pos, 18, "\"schema_version\":9");
      cases.push_back({"future-version", b, SnapshotError::kMalformedInput});
    }
    {
      // Extra envelope field (strict: unexpected => malformed).
      std::string b = good;
      b.insert(b.size() - 1, ",\"extra\":1");
      cases.push_back({"extra-field", b, SnapshotError::kMalformedInput});
    }
    {
      // Deep nesting (DoS guard).
      std::string b = "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[0";
      cases.push_back({"deep-nesting", b, SnapshotError::kMalformedInput});
    }
    for (const auto& c : cases) {
      auto r = DecodeFullSnapshot(c.blob);
      XR_EXPECT_MSG(!r.ok, c.name + ": must fail");
      bool ok_err = c.deny_family_ok ? r.error != SnapshotError::kNone : r.error == c.want;
      XR_EXPECT_MSG(ok_err, c.name + ": error " + ToString(r.error) + " want " +
                                ToString(c.want) + " (" + r.error_detail + ")");
    }
  }

  // ---- determinism: same state => identical bytes ----
  {
    std::vector<SnapshotEntry> s{Mk("id1", "a.com", "kStandard", false),
                                 Mk("id2", "b.com", "kShield", true)};
    // Different insertion order must not change encoding (canonical sort).
    std::vector<SnapshotEntry> shuffled{s[1], s[0]};
    auto a = EncodeFullSnapshot(5, s);
    auto b = EncodeFullSnapshot(5, shuffled);
    XR_EXPECT(a.ok && b.ok);
    XR_EXPECT(a.blob == b.blob);
  }

  return xrtest::Report("test_snapshot");
}
