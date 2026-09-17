// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: renderer/cosmetic/host/cosmetic_host — the JSON-over-stdio façade
// for the cosmetic core (P12-T4). Same protocol conventions as shield_host and
// update_host (fakes/README.md + renderer/cosmetic/host_protocol.md): one JSON
// request on argv[1..] or stdin, one canonical JSON line on stdout, exit 0 for
// ok-or-typed-rejection · 1 typed error · 2 usage.
//
// WHY THIS EXISTS. fakes/cosmetic.py is the Python reference consumer of
// cosmetic-blob-v1. This host is the C++ one. tools/cosmetic_vectors_check.py
// replays every golden vector against BOTH and requires identical stdout and
// identical exit codes — that is what makes the vectors a parity test rather
// than a self-consistency test. Any divergence between this file and the fake
// is therefore a red vector, not a silent drift.
//
// DETERMINISM. No clock, no RNG, no environment. Every input rides in the
// request. The host is stateless: there is no store, no cache, no persisted
// scope. That is what lets the vectors be byte-stable across machines.
//
// NO ENGINE, NO BROWSER. This host links the cosmetic CORE only (selector,
// pseudo, scope_key). It does not link the vendored adblock-rust engine and it
// does not touch a DOM. `gn` and `ninja` are absent from the sandbox this was
// built in, so nothing here claims a rendered result: it validates and decides,
// and the page-level assertions are HG-31.
//
// Methods (table in renderer/cosmetic/host_protocol.md):
//   blob-validate {blob[, frame_scope]}  -> {applied,enabled,refused,...}
//   selector-parse {selector}            -> {ok:true,compounds,...} | refusal
//   scope-key {frame_site,frame_identity,trust,navigation,url_class}
//                                        -> {hex,partition}
#pragma once

#include <string>

namespace xr::cosmetic {

// Runs the host loop. Returns the process exit code.
int HostMain(int argc, char** argv);

// Validates one blob request and writes the canonical result. Exposed so the
// unit tests can drive it without spawning a process — the parity checker uses
// the binary, the tests use this.
std::string ValidateBlobJson(const std::string& request_json,
                             bool have_frame_scope, int* exit_code);

}  // namespace xr::cosmetic
