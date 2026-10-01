// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// identity model + lifecycle suite (P14-T1): provisioning laws (opaque
// keys, fail-closed mint), the state machine, PURGE-AND-VERIFY (§1.4: a
// destroy that leaves bytes is a FAILURE — the planted-leftover negative),
// the per-identity prefs fail-safe (unreadable ⇒ no overrides, never all
// defaults allowed), and the in-memory promote refusal.
#include <map>
#include <string>

#include "harness.h"
#include "core/identity.h"

using xr::identity::CallResult;
using xr::identity::CreateRequest;
using xr::identity::Grade;
using xr::identity::IdentityRecord;
using xr::identity::IdentityStore;
using xr::identity::Manager;
using xr::identity::State;
using xr::identity::SurfaceBlob;

int main() {
  // 1. Create: entropy path mints an opaque domain; record carries surface.
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest req;
    req.entropy = "host-os-entropy-a";
    req.display_name = "Work";
    IdentityRecord rec;
    CallResult r = m.Create(req, &rec);
    XR_EXPECT_MSG(r.ok, r.error.c_str());
    XR_EXPECT_EQ(rec.domain.size(), size_t(39));
    XR_EXPECT_EQ(rec.state, State::kActive);
    XR_EXPECT_MSG(!rec.surface.empty(), "fresh identity owns a byte surface");
    bool has_session = false;
    for (const auto& b : rec.surface) has_session |= b.kind == "session_data";
    XR_EXPECT_MSG(has_session, "on-disk identity carries session_data");
    // Prefs namespace starts readable (per-identity namespace exists).
    XR_EXPECT(rec.prefs.readable);
  }
  // 1b. In-memory (disposable) identities carry NO on-disk session_data.
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest req;
    req.entropy = "e";
    req.in_memory = true;
    IdentityRecord rec;
    XR_EXPECT(m.Create(req, &rec).ok);
    bool has_session = false;
    for (const auto& b : rec.surface) has_session |= b.kind == "session_data";
    XR_EXPECT_MSG(!has_session, "in-memory identity has no durable session data");
  }
  // 1c. No entropy and no replay ⇒ refused (the host cannot accidentally
  // mint from the fixture table).
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest req;
    CallResult r = m.Create(req, nullptr);
    XR_EXPECT_MSG(!r.ok, "no-entropy create refused");
    XR_EXPECT_MSG(r.error.find("entropy") != std::string::npos,
                  "error names the cause");
  }
  // 1d. Replay path: the frozen fixture sequence, then exhaustion refused.
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest req;
    req.replay = true;
    IdentityRecord a, b, c;
    XR_EXPECT(m.Create(req, &a).ok);
    XR_EXPECT(m.Create(req, &b).ok);
    XR_EXPECT(m.Create(req, &c).ok);
    XR_EXPECT_STREQ(a.domain.c_str(),
                    "xr:00000000-0000-4000-8000-000000000001");
    XR_EXPECT_STREQ(c.domain.c_str(),
                    "xr:00000000-0000-4000-8000-00000000000ef");
    CallResult r = m.Create(req, nullptr);
    XR_EXPECT_MSG(!r.ok, "fixture table exhaustion is refused");
  }

  // 2. Store boundary: the opacity law is enforced, not advisory.
  {
    IdentityStore store;
    IdentityRecord bad;
    bad.domain = "work";  // name-keyed — the §1.1 bug, made unrepresentable
    bad.display_name = "Work";
    std::string err;
    XR_EXPECT_MSG(!store.Insert(bad, &err), "name-keyed insert refused");
    // ...and a domain that literally embeds the display name is refused
    // with the opacity reason (the brute-force shape).
    IdentityRecord embed;
    embed.domain = "xr:0000w0rk-0000-4000-8000-000000000001";
    embed.display_name = "w0rk";
    XR_EXPECT_MSG(!store.Insert(embed, &err), "name-embedded domain refused");
    XR_EXPECT_MSG(err.find("opaque") != std::string::npos,
                  "error says the domain is not opaque");
    // A mint-shaped domain with an unrelated name passes.
    IdentityRecord good;
    good.domain = "xr:00000000-0000-4000-8000-000000000001";
    good.display_name = "Work";
    XR_EXPECT_MSG(store.Insert(good, &err), "opaque domain accepted");
    XR_EXPECT_MSG(!store.Insert(good, &err), "duplicate refused");
  }

  // 3. Lifecycle errors: unknown identity, typed codes.
  {
    IdentityStore store;
    Manager m(&store);
    CallResult r = m.Activate("xr:00000000-0000-4000-8000-00000000dead");
    XR_EXPECT_MSG(!r.ok, "unknown identity refused");
    XR_EXPECT_STREQ(r.error.c_str(), "kUnknownIdentity");
  }

  // 4. Destroy = purge AND verify: the happy path verifies zero residual.
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest req;
    req.entropy = "destroy-me";
    IdentityRecord rec;
    XR_EXPECT(m.Create(req, &rec).ok);
    bool verified = false;
    CallResult r = m.Destroy(rec.domain, &verified);
    XR_EXPECT_MSG(r.ok, r.error.c_str());
    XR_EXPECT_MSG(verified, "zero residual verified on the happy path");
    XR_EXPECT_EQ(store.size(), size_t(0));
    // After destroy, every lifecycle call on it is kUnknownIdentity.
    XR_EXPECT_STREQ(m.Activate(rec.domain).error.c_str(), "kUnknownIdentity");
  }
  // 4b. THE NEGATIVE (§1.4 / security req 4): a purge that returns success
  // while bytes remain must FAIL destroy. Plant an out-of-place leftover
  // (the "cookie jar left behind" shape) before destroying.
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest req;
    req.entropy = "planted";
    IdentityRecord rec;
    XR_EXPECT(m.Create(req, &rec).ok);
    store.PlantResidual(rec.domain, {"cookies", 512});  // the forgotten jar
    bool verified = true;
    CallResult r = m.Destroy(rec.domain, &verified);
    XR_EXPECT_MSG(!r.ok, "destroy FAILS when bytes remain");
    XR_EXPECT_MSG(!verified, "verification is honest");
    XR_EXPECT_MSG(r.error.find("residual") != std::string::npos,
                  "error names the residual bytes");
  }

  // 5. Hibernation preserves partition state, discards the volatile surface.
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest req;
    req.entropy = "hib";
    IdentityRecord rec;
    XR_EXPECT(m.Create(req, &rec).ok);
    XR_EXPECT(m.Hibernate(rec.domain).ok);
    const IdentityRecord* h = store.Find(rec.domain);
    XR_EXPECT_MSG(h != nullptr, "record survives hibernation");
    XR_EXPECT_EQ(h->state, State::kHibernated);
    bool cache = false, cookies = false, prefs = false;
    for (const auto& b : h->surface) {
      cache |= b.kind == "cache";
      cookies |= b.kind == "cookies";
      prefs |= b.kind == "prefs";
    }
    XR_EXPECT_MSG(!cache, "volatile surface (cache) discarded");
    XR_EXPECT_MSG(cookies && prefs, "partition state preserved for wake");
    XR_EXPECT(m.Activate(rec.domain).ok);  // wake path
    XR_EXPECT_EQ(store.Find(rec.domain)->state, State::kActive);
  }

  // 6. Promote: in-memory (disposable) identities cannot hold durable
  // profiles — promoting would persist the volatile.
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest req;
    req.entropy = "eph";
    req.in_memory = true;
    IdentityRecord rec;
    XR_EXPECT(m.Create(req, &rec).ok);
    CallResult r = m.PromoteToFortressProfile(rec.domain, nullptr);
    XR_EXPECT_MSG(!r.ok, "in-memory promote refused");
    XR_EXPECT_MSG(r.error.find("in-memory") != std::string::npos,
                  "error names the volatile-persist reason");
    // The on-disk twin promotes fine.
    CreateRequest req2;
    req2.entropy = "durable";
    IdentityRecord rec2;
    XR_EXPECT(m.Create(req2, &rec2).ok);
    IdentityRecord promoted;
    XR_EXPECT(m.PromoteToFortressProfile(rec2.domain, &promoted).ok);
    XR_EXPECT_EQ(promoted.grade, Grade::kFortress);
  }

  // 7. Prefs fail-safe (security req 5): an unreadable namespace yields NO
  // OVERRIDES — the caller applies the restrictive default — never the
  // global permissive set.
  {
    IdentityStore store;
    Manager m(&store);
    CreateRequest req;
    req.entropy = "prefs";
    req.prefs_overrides = {{"shield.trust_level", "strict"}};
    IdentityRecord rec;
    XR_EXPECT(m.Create(req, &rec).ok);
    auto v = m.ResolvePref(rec.domain, "shield.trust_level");
    XR_EXPECT_MSG(v.has_value() && *v == "strict", "override readable");
    XR_EXPECT_MSG(!m.ResolvePref(rec.domain, "absent.key").has_value(),
                  "absent key = no override");
    // Corrupt the namespace: reads must yield nothing, never a guess.
    IdentityRecord* r2 = store.Find(rec.domain);
    r2->prefs.readable = false;
    auto v2 = m.ResolvePref(rec.domain, "shield.trust_level");
    XR_EXPECT_MSG(!v2.has_value(),
                  "unreadable namespace = NO overrides (fail-safe: closed)");
    // Unknown identity: same closed answer.
    XR_EXPECT_MSG(
        !m.ResolvePref("xr:00000000-0000-4000-8000-00000000dead", "k").has_value(),
        "unknown identity = no overrides");
  }

  return xrtest::Report("identity/lifecycle");
}
