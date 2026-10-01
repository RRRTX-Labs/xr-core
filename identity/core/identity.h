// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — the identity MODEL and lifecycle (P14-T1): provisioning
// (overlay rows: prefs/policy/visual defaults applied at creation), the
// state machine (mojom identity.mojom v1: kActive/kHibernated/kDestroyed),
// and Destroy's PURGE-AND-VERIFY (the §1.4 law: a purge that returns success
// while leaving bytes is a FAILURE, so destruction verifies the byte
// surface, not the purge's own return code). The model is deliberately
// explicit about the byte surface a real partition owns (cookie jar, cache,
// prefs, storage, session data) — the browser half (real partitions) is the
// NOT-RUN half of the seam decision; THIS half, the state machine and the
// verify-the-store law, is pure core and fully tested here.
//
// Per-identity prefs namespace (security req §5, P14): an identity whose
// namespace cannot be read yields "no overrides" — the identity keeps its
// restrictive overlay — NEVER "all defaults allowed". Fail-safe direction:
// unreadable ⇒ closed.
//
// Storage-key confinement (§1.1 law "the partition domain is opaque"): the
// namespace keys are the OPAQUE domain (never the display name, never a
// site) — a test plants a name-keyed namespace and reddens.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/mint.h"

namespace xr::identity {

enum class State { kActive, kHibernated, kDestroyed };
enum class Grade { kStandard, kFortress };

// One blob of the byte surface a real partition would own (name is what the
// verify step prints; e.g. "cookies", "cache", "prefs", "storage").
struct SurfaceBlob {
  std::string kind;
  size_t bytes = 0;
};

// The per-identity prefs namespace. `readable=false` models a corrupt/
// unreadable store; every read path must treat that as NO-OVERRIDES.
struct PrefsNamespace {
  bool readable = true;
  std::map<std::string, std::string> rows;  // key -> value
};

struct IdentityRecord {
  std::string domain;          // opaque partition domain (mint)
  std::string display_name;    // user-chosen; NEVER a storage key
  std::string template_id;     // "" = none; else templates.cc id
  Grade grade = Grade::kStandard;
  bool in_memory = false;      // ephemeral/disposable partition
  State state = State::kActive;
  PrefsNamespace prefs;
  std::vector<SurfaceBlob> surface;
  // visual identity marks (T4 consumes; provisioning sets)
  std::string color;           // hex, e.g. "#78288c"
  std::string glyph;           // short label, e.g. "W"
};

// Result shape shared by every lifecycle call: `ok` false ⇒ `error` is the
// mojom-compatible code (kUnknownIdentity, kNotPermitted, kMalformedInput).
struct CallResult {
  bool ok = false;
  std::string error;
};

// The store the manager owns. Keyed by the OPAQUE domain only.
class IdentityStore {
 public:
  // Every key must be an opaque domain (LooksOpaque vs. the probe of the
  // matching record's display name); a name-keyed insert is refused.
  bool Insert(const IdentityRecord& rec, std::string* error);
  IdentityRecord* Find(std::string_view domain);
  const IdentityRecord* Find(std::string_view domain) const;
  bool Erase(std::string_view domain);

  // Purge-and-verify support: the bytes the store still holds FOR ONE
  // identity after a purge (walking the store — the purge's return code is
  // not trusted, §1.4). Counts BOTH the record's own surface and the
  // out-of-place bytes a naive purge misses (residual_ = temp files, crash
  // dumps, shader caches — places a real purge forgets; the negative test
  // plants one).
  void PlantResidual(std::string_view domain, SurfaceBlob blob);
  size_t ResidualBytes(std::string_view domain) const;
  std::vector<std::string> ResidualKinds(std::string_view domain) const;

  size_t size() const { return records_.size(); }

 private:
  std::map<std::string, IdentityRecord> records_;  // domain -> record
  std::map<std::string, std::vector<SurfaceBlob>> residual_;  // stray bytes
};

// Provisioning (create) inputs: overlay rows come from the template when
// `template_id` is set (see templates.h) plus any user overrides.
// `entropy` is caller-supplied (the production host passes OS entropy per
// call). `replay` selects the fixture-table mint — the vectors' path; the
// HOST FAÇADE never sets it (no entropy + no replay ⇒ kMalformedInput,
// fail-closed: the host cannot accidentally mint from the fixture table).
struct CreateRequest {
  std::string entropy;
  bool replay = false;
  std::string display_name;
  std::string template_id;
  Grade grade = Grade::kStandard;
  bool in_memory = false;
  std::map<std::string, std::string> prefs_overrides;
};

class Manager {
 public:
  explicit Manager(IdentityStore* store) : store_(store) {}

  // Lifecycle (mojom v1 shapes; MoveTab lives in binding.h — T3).
  CallResult Create(const CreateRequest& req, IdentityRecord* out);
  CallResult Activate(std::string_view domain);
  CallResult Hibernate(std::string_view domain);
  // Destroy purges the byte surface and VERIFIES the store; a planted
  // leftover (TestPlantResidual) makes this fail — that is the law.
  CallResult Destroy(std::string_view domain, bool* zero_residual_verified);
  CallResult PromoteToFortressProfile(std::string_view domain,
                                      IdentityRecord* out);

  // Per-identity prefs resolve (the fail-safe): returns nullopt when the
  // identity's namespace must NOT be read (unreadable) or the key is absent
  // — "no overrides", never the global permissive default. The caller
  // applies the template's restrictive row on nullopt (fail-safe: closed).
  std::optional<std::string> ResolvePref(std::string_view domain,
                                         std::string_view key) const;

  // Store access for the scheduler (state checks without exposing the
  // store itself).
  IdentityRecord* FindInStore(std::string_view domain);
  const IdentityRecord* FindInStore(std::string_view domain) const;

 private:
  IdentityStore* store_;
  uint64_t fixture_sequence_ = 0;  // replay path only (explicit req.replay)
};

}  // namespace xr::identity
