// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/engine — the C ABI the vendored adblock-rust engine
// exposes to the C++ decision core (P11-T2 skeleton; the parity-measured
// binding lands with T8). The Rust side (lib.rs) builds as a cdylib in the
// HOSTED lane (core-hardening shield-vendor job) against
// //xr/third_party/rust/adblock/0.13.3 — there is no local cargo build and
// nothing in-tree claims one.
//
// Contract laws:
//   * total: no function here may throw/panic across the boundary — the
//     Rust side catches panics at every export and reports them through
//     xr_shield_engine_alive() == 0 (engine death is REPRESENTABLE, the
//     posture core's fail-open law consumes it);
//   * deterministic: same inputs, same outputs; no clock, no RNG, no I/O;
//   * the match surface is the v1 redaction: scheme://host/path,
//     lowercased, port/query/fragment stripped BY THE CALLER (the core's
//     UrlParts) — the engine never sees browsing-data suffixes;
//   * allow-overrides-block lives in the engine (ABP exception semantics),
//     mirroring shield/core/fake_engine.h exactly — T8's parity job
//     measures the two against the >=1,500-case corpus.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque engine handle (Rust Box<Engine> behind the pointer).
typedef struct XrShieldEngine XrShieldEngine;

// A match hit, filled by xr_shield_engine_match on return 1.
typedef struct XrShieldHit {
  // action: 0 block · 1 allow · 2 redirect · 3 replace
  // (mirrors xr::shield::Action; kUpgraded has no list-rule producer)
  uint8_t action;
  // indices into the rules JSON the engine was created from, so the C++
  // side can recover rule_id/list_id/filter text without copying strings
  uint32_t list_index;
  uint32_t rule_index;
  // redirect/replace resource: NUL-terminated, borrowed from the engine
  // (valid until the next match call or free); "" when action < 2
  const char* redirect_resource;
} XrShieldHit;

// Create an engine over a normalized xr-list-bundle-v1 document (the SAME
// canonical bytes the core parsed — utf8, NUL-terminated). Returns NULL on
// any parse/build failure (total; the reason rides in *detail_out, a
// static string from the closed refusal vocabulary).
XrShieldEngine* xr_shield_engine_create(const char* bundle_json,
                                        const char** detail_out);

// Liveness: 0 once the engine has died (panic caught, poisoned internal
// state, OOM during load). When 0, match MUST NOT be called; the posture
// core owns the consequence (fail-open, amber).
int xr_shield_engine_alive(const XrShieldEngine* engine);

// Match one request. Inputs are the caller-redacted URL parts (all utf8,
// NUL-terminated): match_url = "scheme://host/path", host, path, plus the
// rule-option dimensions (registrable_domain). Returns 1 with *hit_out
// filled when a rule matched, 0 when the engine has no opinion
// (allow-by-default is the engine contract; blocking is rule-driven).
int xr_shield_engine_match(const XrShieldEngine* engine, const char* match_url,
                           const char* host, const char* path,
                           const char* registrable_domain,
                           XrShieldHit* hit_out);

// Destroy. Safe on NULL. After this, every borrowed pointer is invalid.
void xr_shield_engine_free(XrShieldEngine* engine);

// Test seam (hosted lane only): force the alive flag to 0 — the C++
// posture/fail-open property tests drive engine death through the SAME
// observable, whether the binding is the TableEngine fake or this shim.
void xr_shield_engine_kill_for_test(XrShieldEngine* engine);

#ifdef __cplusplus
}  // extern "C"
#endif
