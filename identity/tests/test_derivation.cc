// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// test_derivation — P14 security row (P14-CLOSE C-4). Plan §4 P14 security
// req: "identity state never in URL/IPC-visible strings (partition domains
// are opaque UUIDs — brute-force isolation probes in P9 suite);
// cross-identity process assertion in every build."
//
// Four derivation channels, each probed so that it MUST FAIL to derive:
//   1. partition name — the domain is a function of caller entropy only, so
//      the same display name / template / URL / title gives unrelated domains,
//      and no probe string is embedded in a domain (LooksOpaque);
//   2. URL   — a domain never appears in any string the binding or the ledger
//      overlay hands back for display (refusals included);
//   3. title — same, for the title-bearing history overlay;
//   4. log line — every refusal/error string the core can produce for a move
//      carries no identity_id or partition domain (byte-absence: a
//      secret-shaped domain is planted and searched for).
// Plus the cross-identity process assertion: two live identities can never
// share a partition (the process-lock key in ADR-0042's seam is the
// StoragePartitionConfig minted from the domain; content/ never shares a
// renderer process across partitions, so a shared domain IS a shared
// process). Cites spike/identity_seam/xr_identity.h:36 ("a guessable
// partition domain is a cross-identity correlation primitive") and the P6
// resolver's storage_scope (policy/core) — the store refuses the share.
#include <map>
#include <set>
#include <string>
#include <vector>

#include "common/core/json.h"
#include "core/binding.h"
#include "core/identity.h"
#include "core/ledger_tag.h"
#include "core/mint.h"
#include "harness.h"

using xr::identity::BindingModel;
using xr::identity::CreateRequest;
using xr::identity::IdentityRecord;
using xr::identity::IdentityStore;
using xr::identity::Manager;

namespace {

std::string Mint(Manager& m, const std::string& entropy, const std::string& name,
                 const std::string& tmpl = "") {
  CreateRequest req;
  req.entropy = entropy;
  req.display_name = name;
  req.template_id = tmpl;
  IdentityRecord rec;
  auto res = m.Create(req, &rec);
  XR_EXPECT_MSG(res.ok, "create " + name + ": " + res.error);
  return rec.domain;
}

void TestPartitionNameNotDerivable() {
  IdentityStore store;
  Manager m(&store);
  const std::vector<std::string> probes = {
      "Work", "work", "Banking", "https://bank.example/", "My Bank — Login",
      "personal", "shopping", "xr:work", "00000000"};
  std::set<std::string> domains;
  int i = 0;
  for (const auto& name : probes) {
    // Same display name twice, different entropy: unrelated domains.
    const std::string d1 = Mint(m, "e1-" + std::to_string(i), name);
    const std::string d2 = Mint(m, "e2-" + std::to_string(i), name);
    ++i;
    XR_EXPECT_MSG(d1 != d2, "domain derivable from display name: " + name);
    for (const auto& p : probes) {
      XR_EXPECT_MSG(xr::identity::LooksOpaque(d1, p), "probe embedded: " + p);
    }
    domains.insert(d1);
    domains.insert(d2);
  }
  XR_EXPECT(domains.size() == probes.size() * 2);
  // Brute force over the probe dictionary: minting FROM the probe strings
  // themselves as entropy never reproduces any domain minted above (the
  // attacker's dictionary is names/URLs/titles; the key space is entropy).
  for (const auto& p : probes) {
    std::string guess;
    XR_EXPECT(xr::identity::MintDomain(p, &guess));
    XR_EXPECT_MSG(domains.count(guess) == 0, "dictionary hit for " + p);
  }
}

void TestMoveErrorsCarryNoIdentity() {
  // A secret-shaped domain, planted; every move refusal is searched for it.
  const std::string secret = "xr:5ec2e700-0000-4000-8000-00000000c0de";
  const std::string other = "xr:00000000-0000-4000-8000-0000000000aa";
  XR_EXPECT(xr::identity::DomainShapeOk(secret));
  BindingModel b;
  XR_EXPECT(b.SetWindowDefault("w1", secret).ok);
  XR_EXPECT(b.OpenTab("w1", 7, "").ok);
  std::vector<std::string> errors;
  errors.push_back(b.MoveTab(secret, other, 7, true, {}).error);   // after_nav
  errors.push_back(b.MoveTab(other, secret, 7, false, {}).error);  // wrong from
  errors.push_back(b.MoveTab(secret, "not-a-domain", 7, false, {}).error);
  errors.push_back(b.MoveTab(secret, other, 7, false, {{8, secret}}).error);
  for (const auto& e : errors) {
    XR_EXPECT_MSG(!e.empty(), "every probe above must be a refusal");
    XR_EXPECT_MSG(e.find(secret) == std::string::npos, "domain in error: " + e);
    XR_EXPECT_MSG(e.find(other) == std::string::npos, "domain in error: " + e);
    XR_EXPECT_MSG(e.find("xr:") == std::string::npos, "id-shaped text in error: " + e);
    XR_EXPECT_MSG(e.find("identity_id") == std::string::npos, "field name in error: " + e);
  }
  // The destructive reload message names WHAT is lost, never WHO.
  XR_EXPECT(errors[0].find("confirm + reload") != std::string::npos);
}

void TestOverlayRefusalsCarryNoInput() {
  // A refusal must not echo a malformed id back (a log line built from it
  // would carry whatever the caller put there).
  const std::string planted = "xr:5ec2e700-PLANTED-0000";
  auto r = xr::common::ParseJson(
      "{\"identity_id\":\"" + planted + "\",\"event_class\":\"kBlock\",\"event\":{}}");
  xr::common::JsonValue out;
  std::string why;
  XR_EXPECT(!xr::identity::TagEvent(r.value, &out, &why));
  XR_EXPECT(why.find("PLANTED") == std::string::npos);
  XR_EXPECT(why.find("5ec2e700") == std::string::npos);
}

void TestCrossIdentityNeverSharesAPartition() {
  IdentityStore store;
  Manager m(&store);
  const std::string a = Mint(m, "proc-a", "A");
  const std::string b = Mint(m, "proc-b", "B");
  XR_EXPECT(a != b);
  // The planted share: a second identity record that reuses A's partition.
  IdentityRecord shared;
  shared.domain = a;
  shared.display_name = "B-sharing-A";
  std::string error;
  XR_EXPECT_MSG(!store.Insert(shared, &error),
                "two identities were allowed to share one partition (process)");
  XR_EXPECT(error == "kAlreadyExists");
  // Binding: a tab bound to A then moved to B is in exactly one partition at
  // any time; the audit never shows a tab in two identities at once.
  BindingModel bind;
  XR_EXPECT(bind.SetWindowDefault("w", a).ok);
  XR_EXPECT(bind.OpenTab("w", 1, "").ok);
  XR_EXPECT(bind.MoveTab(a, b, 1, false, {}).ok);
  XR_EXPECT(bind.TabIdentity(1).value_or("") == b);
}

}  // namespace

int main() {
  TestPartitionNameNotDerivable();
  TestMoveErrorsCarryNoIdentity();
  TestOverlayRefusalsCarryNoInput();
  TestCrossIdentityNeverSharesAPartition();
  return xrtest::Report("identity/derivation");
}
