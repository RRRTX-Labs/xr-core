// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// TEST HELPER (update/tests only): builds XR update envelopes in C++ that
// are byte-identical to the golden-vector generator (tools/
// gen_update_vectors.py) and the Python fake — same canonical JSON, same
// TEST-ONLY stub signature. Tests compare against the same vector file,
// so this helper is the third independent path to the same bytes.
#pragma once

#include <string>

#include "update/core/json.h"
#include "update/core/sha256.h"

namespace xrtest_update {

inline std::string Canonical(const xr::update::JsonValue& v) {
  return v.Canonical();
}

// Fixture signature for `public_key` (default ROOT-PUB, matching key_id
// xr-root-1 in tests): "sig:" + sha256(key|canonical response)[:16].
inline std::string StubSigFor(const std::string& public_key,
                              const xr::update::JsonValue& response) {
  return "sig:" +
         xr::update::Sha256Hex(public_key + "|" + response.Canonical())
             .substr(0, 16);
}

inline std::string StubSig(const xr::update::JsonValue& response) {
  return StubSigFor("ROOT-PUB", response);
}

// Material map for the test key ids (mirrors the vectors' KEYS block).
inline std::string KeyMaterialFor(const std::string& key_id) {
  if (key_id == "xr-signing-2026-09") return "SIGNING-PUB";
  return "ROOT-PUB";  // xr-root-1 and the default
}

// The base valid response (mirror of gen_update_vectors.BASE_RESPONSE).
// Built bottom-up as JsonValue::Object maps (the model takes Object/Array
// by value; explicit JV() wrappers keep brace-init unambiguous).
inline xr::update::JsonValue Package(const std::string& version,
                                     const std::string& hash) {
  using JV = xr::update::JsonValue;
  using Obj = JV::Object;
  using Arr = JV::Array;
  JV pkg{Obj{{"fp", JV("1.abc")},
             {"hash_sha256", JV(hash)},
             {"name", JV("xr-" + version + ".crx")},
             {"size", JV(static_cast<int64_t>(1024))}}};
  JV packages{Obj{{"package", JV(Arr{pkg})}}};
  JV manifest{Obj{{"packages", packages},
                  {"run", JV("")},
                  {"version", JV(version)}}};
  JV url{Obj{{"codebase", JV("https://updates.example/xr/")}}};
  JV urls{Obj{{"url", JV(Arr{url})}}};
  JV uc{Obj{{"manifest", manifest}, {"status", JV("ok")}, {"urls", urls}}};
  JV app0{Obj{{"appid", JV("labs.rrrtx.xr")},
              {"status", JV("ok")},
              {"updatecheck", uc}}};
  JV daystart{Obj{{"elapsed_days", JV(static_cast<int64_t>(7000))}}};
  JV resp{Obj{{"app", JV(Arr{app0})},
              {"daystart", daystart},
              {"protocol", JV("3.1")},
              {"server", JV("pub")}}};
  return resp;
}

inline xr::update::JsonValue BaseResponse(
    const std::string& version,
    const std::string& hash = std::string(64, 'a')) {
  return Package(version, hash);
}

inline std::string Envelope(const xr::update::JsonValue& response,
                            const std::string& epoch_id = "epoch-2026-09",
                            const std::string& key_id = "xr-root-1",
                            long long seq = 3,
                            const std::string& sig_value = "") {
  // The fixture signs with the material matching key_id (key substitution
  // is observable: signing under the wrong key fails verification).
  using JV = xr::update::JsonValue;
  using Obj = JV::Object;
  JV epoch{Obj{{"epoch_id", JV(epoch_id)},
               {"key_id", JV(key_id)},
               {"seq", JV(static_cast<int64_t>(seq))}}};
  JV sig{Obj{{"alg", JV("minisign-ed25519")},
             {"key_id", JV(key_id)},
             {"sig", JV(sig_value.empty() ? StubSig(response) : sig_value)}}};
  JV doc{Obj{{"epoch", epoch},
             {"schema", JV("xr-update-envelope")},
             {"schema_version", JV(static_cast<int64_t>(1))},
             {"signature", sig},
             {"response", response}}};
  return doc.Canonical();
}

}  // namespace xrtest_update
