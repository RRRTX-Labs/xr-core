// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — update_host: the JSON-over-stdio façade for the update core
// (P10-T1). SAME protocol conventions as the commands/settings/themes hosts
// (fakes/README.md): one JSON request on argv[1] or stdin, one canonical
// JSON result on stdout, sorted keys, \uXXXX non-ASCII, exit 0 ok/typed ·
// 1 error · 2 usage. No exceptions cross this boundary.
//
// VERIFIER BINDING: this host binds the TEST-ONLY verifier (update/tests,
// named *_testonly). It is the default and ONLY selection here; the
// production binding is the farm row (docs/release/updater-integration.md).
// The refusal of a release channel under a test verifier is CORE POLICY
// (verify_policy.cc) and is exercised through this host by the golden
// vectors. Signing release artifacts with test material is separately
// impossible (build/signing/sign_artifact.py refuses release channels).
//
// Methods (update/host_protocol.md — living contract):
//   flag-status {}                      -> {"xr_updater_v0":"on"|"off"}
//   verify     {envelope,channel,current_version,epoch?,keys[],seen[]?} -> verdict
//   epoch-apply {notice,epoch?,keys[]}  -> updated epoch state
//   cohort     {install_id,channel,buckets} -> {bucket}
//   backoff    {state,now_mono,outcome?}-> {may_check_now,seconds_until_next,state}
//   about-states {}                      -> {states:[...]}   (the About machine)
//   about-state {state,event}            -> {state,reason?,manual_download?}
//
// The `verify` request may carry a `transport` echo — DOCUMENTED AS IGNORED:
// TLS-independence is the law, and the field exists so tests can prove the
// verdict is byte-identical under contradictory transports. Any OTHER
// unknown field is a typed refusal.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "update/core/backoff.h"
#include "update/core/cohort.h"
#include "update/core/epoch.h"
#include "update/core/json.h"
#include "update/core/manifest.h"
#include "update/core/seen.h"
#include "update/core/verify_policy.h"
#include "update/core/sha256.h"

namespace {

using xr::update::Attempt;
using xr::update::BackoffState;
using xr::update::ClientState;
using xr::update::EpochState;
using xr::update::JsonValue;
using xr::update::ParseJson;
using xr::update::PinnedKeys;
using xr::update::RevocationNotice;
using xr::update::SeenSet;
using xr::update::VerifyOutcome;

// Canonical typed error (sorted keys via the JSON model — parity law with
// the Python fake: every byte of output goes through the same serializer).
void EmitError(const char* code, const std::string& detail) {
  JsonValue o(JsonValue::Object{
      {"detail", JsonValue(detail)},
      {"error", JsonValue(std::string(code))},
  });
  std::printf("%s\n", o.Canonical().c_str());
}

// Canonical typed rejection (exit 0, result carries the refusal).
void EmitReject(const std::string& reason) {
  JsonValue o(JsonValue::Object{
      {"error", JsonValue("kRejected")},
      {"reason", JsonValue(reason)},
  });
  std::printf("%s\n", o.Canonical().c_str());
}

std::string ReadAllStdin() {
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) data.append(buf, n);
  return data;
}

int Usage() {
  std::fprintf(stderr,
               "usage: update_host <method> ['<json-args>'] [options]\n"
               "       update_host '<json-with-method>' [options]\n"
               "       update_host [options]   # request JSON on stdin\n"
               "options: --store-dir DIR (absent/empty => disposable, zero bytes)\n"
               "         --flag xr_updater_v0=on|off\n"
               "methods: flag-status verify epoch-apply cohort backoff\n"
               "         about-states about-state\n");
  return 2;
}

const char* kStates[] = {"idle", "checking", "available", "downloading",
                         "ready", "failed", "refused"};

// The About state machine (docs/webui-e2e.md; tools/about_state_check.py
// checks the view renders EVERY state this list names). Pure transition
// function; "refused" is terminal except by explicit manual intervention
// (the kill switch — threat-model row 14), "failed" always carries a reason
// and the manual-download pointer (no silent failures — law).
bool StepAboutState(const std::string& state, const std::string& event,
                    std::string* next, std::string* reason) {
  if (event == "epoch-revoked") {
    *next = "refused";
    if (reason) *reason = "epoch revoked: manual download required";
    return true;
  }
  if (state == "refused") {  // terminal by design
    *next = "refused";
    if (reason) *reason = "epoch revoked: manual download required";
    return true;
  }
  if (event == "check" && (state == "idle" || state == "failed")) {
    *next = "checking";
    return true;
  }
  if (event == "offer" && state == "checking") {
    *next = "available";
    return true;
  }
  if (event == "noupdate" && state == "checking") {
    *next = "idle";
    if (reason) *reason = "up to date";
    return true;
  }
  if (event == "download-start" && state == "available") {
    *next = "downloading";
    return true;
  }
  if (event == "download-done" && state == "downloading") {
    *next = "ready";
    return true;
  }
  if (event == "error" &&
      (state == "checking" || state == "downloading" || state == "available")) {
    *next = "failed";
    if (reason) *reason = "update failed: reason shown with a manual-download path";
    return true;
  }
  if (event == "install" && state == "ready") {  // browser relaunch = reset
    *next = "idle";
    return true;
  }
  if (event == "reset") {
    *next = "idle";
    return true;
  }
  return false;  // unknown transition is a typed refusal at the caller
}

bool ParseKeys(const JsonValue* arr, PinnedKeys* keys, std::string* err) {
  if (!arr || !arr->is_array() || arr->as_array().empty()) {
    *err = "keys[] required (key material is injected, never from the wire)";
    return false;
  }
  for (const JsonValue& k : arr->as_array()) {
    if (!k.is_object()) return false;
    const JsonValue* id = k.find("key_id");
    const JsonValue* pk = k.find("public_key");
    if (!id || !id->is_string() || !pk || !pk->is_string()) return false;
    keys->Add(id->as_string(), pk->as_string());
  }
  return true;
}

bool ParseEpoch(const JsonValue* v, EpochState* epoch) {
  if (!v || v->is_null()) return true;  // absent => fresh install
  if (!v->is_object()) return false;
  xr::update::EpochFromJson(*v, epoch);
  return true;
}

}  // namespace

// ---- TEST-ONLY verifier binding (update/tests; see verifier.h) -------------
namespace xr::update {
class TestOnlyVerifier : public SignatureVerifier {
 public:
  TestOnlyVerifier() = default;
  bool Verify(const std::string& public_key, const std::string& message,
              const std::string& signature) const override {
    // Deterministic stub: "sig:" + first 16 hex of sha256(key|message). ANY
    // change to the message or the key material invalidates it. This is a
    // FIXTURE, not a scheme: it proves policy ordering, never authenticity.
    return signature ==
           "sig:" + Sha256Hex(public_key + "|" + message).substr(0, 16);
  }
  bool AllowedForChannel(const std::string& channel) const override {
    return channel == "dev" || channel == "nightly-test";
  }
};
}  // namespace xr::update

int main(int argc, char** argv) {
  using namespace xr::update;
  std::string store_dir;
  std::string flag = "on";
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--store-dir" && i + 1 < argc) store_dir = argv[++i];
    else if (a == "--flag" && i + 1 < argc) {
      std::string kv = argv[++i];
      auto eq = kv.find('=');
      if (eq == std::string::npos) {
        std::fprintf(stderr, "usage error: --flag k=v\n");
        return 2;
      }
      if (kv.substr(0, eq) == "xr_updater_v0") flag = kv.substr(eq + 1);
      else {
        std::fprintf(stderr, "usage error: unknown flag %s\n", kv.substr(0, eq).c_str());
        return 2;
      }
    } else if (a.rfind("--", 0) == 0) {
      std::fprintf(stderr, "usage error: unknown option %s\n", a.c_str());
      return 2;
    } else {
      positional.push_back(a);
    }
  }
  if (flag != "on" && flag != "off") {
    std::fprintf(stderr, "usage error: xr_updater_v0 must be on|off\n");
    return 2;
  }

  std::string method;
  std::string args_json = "{}";
  if (positional.empty()) {
    const std::string raw = ReadAllStdin();
    JsonParseResult p = ParseJson(raw);
    if (!p.ok) {
      EmitError("kMalformedInput", "bad stdin frame");
      return 1;
    }
    const JsonValue* m = p.value.find("method");
    const JsonValue* a = p.value.find("args");
    method = (m && m->is_string()) ? m->as_string() : "";
    args_json = a ? a->Canonical() : std::string("{}");
  } else {
    JsonParseResult p = ParseJson(positional[0]);
    if (p.ok && p.value.is_object() && p.value.find("method")) {
      const JsonValue* m = p.value.find("method");
      const JsonValue* a = p.value.find("args");
      method = (m && m->is_string()) ? m->as_string() : "";
      args_json = a ? a->Canonical() : std::string("{}");
    } else if (positional.size() == 2 || positional.size() == 1) {
      method = positional[0];
      if (positional.size() == 2) args_json = positional[1];
    } else {
      return Usage();
    }
  }
  if (method.empty()) return Usage();

  JsonParseResult ap = ParseJson(args_json);
  if (!ap.ok || !ap.value.is_object()) {
    EmitError("kMalformedInput", "args not an object");
    return 1;
  }
  const JsonValue& args = ap.value;

  if (method == "flag-status") {
    std::printf("{\"xr_updater_v0\":\"%s\"}\n", flag.c_str());
    return 0;
  }
  if (method == "about-states") {
    JsonValue::Array arr;
    for (const char* s : kStates) arr.push_back(JsonValue(s));
    std::printf("%s\n", JsonValue(JsonValue::Object{{"states", JsonValue(std::move(arr))}}).Canonical().c_str());
    return 0;
  }

  // Every other method is a no-op under the flag-off state (typed, like the
  // settings host) — the both-flags parity law.
  if (flag != "on" &&
      (method == "verify" || method == "epoch-apply" || method == "backoff")) {
      EmitReject("feature flag xr_updater_v0 is off — stock chrome update behavior");
    return 0;
  }

  if (method == "verify") {
    static const char* kVerifyArgKeys[] = {"envelope", "channel",
                                           "current_version", "epoch", "keys",
                                           "seen", "transport"};
    for (const auto& [k, _] : args.as_object()) {
      bool ok = false;
      for (const char* a : kVerifyArgKeys) ok = ok || (k == a);
      if (!ok) {
        EmitError("kMalformedInput", "unknown verify arg " + k);
        return 1;
      }
    }
    const JsonValue* env = args.find("envelope");
    const JsonValue* channel = args.find("channel");
    const JsonValue* cur = args.find("current_version");
    const JsonValue* keys = args.find("keys");
    if (!env || !env->is_string() || env->as_string().empty() || !channel ||
        !channel->is_string() || channel->as_string().empty() || !cur ||
        !cur->is_string() || !keys) {
      EmitError("kMalformedInput", "verify needs envelope, channel, current_version, keys");
      return 1;
    }
    PinnedKeys pinned;
    std::string err;
    if (!ParseKeys(keys, &pinned, &err)) {
      EmitError("kMalformedInput", "bad keys[]");
      return 1;
    }
    EpochState epoch;
    if (!ParseEpoch(args.find("epoch"), &epoch)) {
      EmitError("kMalformedInput", "bad epoch");
      return 1;
    }
    SeenSet seen(store_dir);
    {
      std::string load_err;
      seen.Load(&load_err);  // corrupt => preserved + refused updates
    }
    if (const JsonValue* inline_seen = args.find("seen")) {
      if (inline_seen->is_array()) {
        for (const JsonValue& id : inline_seen->as_array()) {
          if (id.is_string()) (void)seen.Insert(id.as_string(), nullptr);
        }
      }
    }
    ClientState state;
    state.channel = channel->as_string();
    state.current_version = cur->as_string();
    state.epoch = epoch;
    state.seen = &seen;
    TestOnlyVerifier verifier;  // TEST-ONLY binding (see header comment)
    const VerifyOutcome out = VerifyUpdateResponse(
        env->as_string(), state, pinned, verifier, state.channel);
    JsonValue::Object o;
    o["verifier"] = JsonValue("test-only: XR UPDATE HOST IS A TEST FIXTURE, "
                              "NOT A RELEASE SIGNING PATH");
    o["verdict"] = JsonValue(out.verdict == Verdict::kAccept ? "accept" : "deny");
    o["reason"] = JsonValue(out.reason);
    o["manual_path"] = JsonValue(out.manual_path);
    if (!out.manifest_id.empty()) o["manifest_id"] = JsonValue(out.manifest_id);
    o["seen_size"] = JsonValue(static_cast<int64_t>(seen.size()));
    std::printf("%s\n", JsonValue(o).Canonical().c_str());
    return 0;
  }

  if (method == "epoch-apply") {
    const JsonValue* notice = args.find("notice");
    const JsonValue* keys = args.find("keys");
    if (!notice || !notice->is_string() || !keys) {
      EmitError("kMalformedInput", "epoch-apply needs notice, keys");
      return 1;
    }
    // The notice arrives as an envelope-style {"body":{...},"signature":{
    // ...}} wrapper (unknown WRAPPER keys refused here); the strict
    // notice schema applies to the BODY, whose canonical bytes are signed.
    JsonParseResult np = ParseJson(notice->as_string());
    JsonValue body;
    std::string sig_value;
    if (np.ok && np.value.is_object()) {
      static const char* kWrapperKeys[] = {"body", "signature"};
      for (const auto& [k, _] : np.value.as_object()) {
        bool ok = false;
        for (const char* a : kWrapperKeys) ok = ok || (k == a);
        if (!ok) {
          EmitError("kMalformedInput", "bad notice (unknown-field)");
          return 1;
        }
      }
      const JsonValue* b = np.value.find("body");
      if (b && b->is_object()) body = *b;
      if (const JsonValue* s = np.value.find("signature")) {
        const JsonValue* v = s->find("sig");
        if (v && v->is_string()) sig_value = v->as_string();
      }
    }
    RevocationNotice rn;
    const NoticeResult nr =
        ParseRevocationNotice(body.is_object() ? body.Canonical() : "", &rn);
    if (nr != NoticeResult::kOk) {
      EmitError("kMalformedInput",
                nr == NoticeResult::kUnknownField ? "bad notice (unknown-field)"
                                                : "bad notice (malformed)");
      return 1;
    }
    PinnedKeys pinned;
    std::string err;
    if (!ParseKeys(keys, &pinned, &err)) {
      EmitError("kMalformedInput", "bad keys[]");
      return 1;
    }
    EpochState epoch;
    if (!ParseEpoch(args.find("epoch"), &epoch)) {
      EmitError("kMalformedInput", "bad epoch");
      return 1;
    }
    TestOnlyVerifier verifier;
    const std::string signed_bytes = body.Canonical();
    if (sig_value.empty() ||
        !verifier.Verify(pinned.Material(rn.key_id), signed_bytes, sig_value) ||
        rn.key_id.empty() || !pinned.Has(rn.key_id)) {
      EmitReject("signature-invalid");
      return 0;
    }
    bool applied = false;
    const NoticeResult ar = ApplyRevocation(rn, &epoch, &applied);
    if (ar != NoticeResult::kOk) {
      EmitError("kMalformedInput", "bad notice");
      return 1;
    }
    JsonValue::Object o;
    o["applied"] = JsonValue(applied);
    o["epoch"] = EpochToJson(epoch);
    std::printf("%s\n", JsonValue(o).Canonical().c_str());
    return 0;
  }

  if (method == "cohort") {
    const JsonValue* iid = args.find("install_id");
    const JsonValue* ch = args.find("channel");
    const JsonValue* bk = args.find("buckets");
    if (!iid || !iid->is_string() || iid->as_string().empty() || !ch ||
        !ch->is_string() || ch->as_string().empty() || !bk || !bk->is_int() ||
        bk->as_int() <= 0 || bk->as_int() > 100000) {
      EmitError("kMalformedInput", "cohort needs install_id, channel, buckets");
      return 1;
    }
    const int bucket = CohortBucket(iid->as_string(), ch->as_string(),
                                    static_cast<int>(bk->as_int()));
    std::printf("{\"bucket\":%d}\n", bucket);
    return 0;
  }

  if (method == "backoff") {
    const JsonValue* st = args.find("state");
    const JsonValue* now = args.find("now_mono");
    if (!st || !st->is_object() || !now || !now->is_int()) {
      EmitError("kMalformedInput", "backoff needs state, now_mono");
      return 1;
    }
    BackoffState bs;
    if (const JsonValue* v = st->find("last_check_mono")) bs.last_check_mono = v->as_int();
    if (const JsonValue* v = st->find("next_allowed_mono")) bs.next_allowed_mono = v->as_int();
    if (const JsonValue* v = st->find("fail_streak")) bs.fail_streak = static_cast<int>(v->as_int());
    if (const JsonValue* outcome = args.find("outcome")) {
      if (outcome->is_string()) {
        RecordOutcome(&bs, now->as_int(),
                      outcome->as_string() == "success" ? Attempt::kSuccess
                                                        : Attempt::kFailure);
      }
    }
    JsonValue::Object so;
    so["fail_streak"] = JsonValue(static_cast<int64_t>(bs.fail_streak));
    so["last_check_mono"] = JsonValue(static_cast<int64_t>(bs.last_check_mono));
    so["next_allowed_mono"] = JsonValue(static_cast<int64_t>(bs.next_allowed_mono));
    JsonValue::Object o;
    o["may_check_now"] = JsonValue(MayCheckNow(bs, now->as_int()));
    o["seconds_until_next"] =
        JsonValue(static_cast<int64_t>(SecondsUntilNextCheck(bs, now->as_int())));
    o["state"] = JsonValue(std::move(so));
    std::printf("%s\n", JsonValue(o).Canonical().c_str());
    return 0;
  }

  if (method == "about-state") {
    const JsonValue* st = args.find("state");
    const JsonValue* ev = args.find("event");
    if (!st || !st->is_string() || st->as_string().empty() || !ev ||
        !ev->is_string() || ev->as_string().empty()) {
      EmitError("kMalformedInput", "about-state needs state, event");
      return 1;
    }
    std::string next, reason;
    if (!StepAboutState(st->as_string(), ev->as_string(), &next, &reason)) {
      EmitReject("unknown transition");
      return 0;
    }
    JsonValue::Object o;
    o["state"] = JsonValue(next);
    if (!reason.empty()) o["reason"] = JsonValue(reason);
    if (next == "failed") {
      o["manual_download"] = JsonValue(JsonValue::Object{
          {"path", JsonValue("xr://about -> manual download")}});
      o["check_again"] = JsonValue(true);
    }
    if (next == "refused") {
      o["manual_download"] = JsonValue(JsonValue::Object{
          {"path", JsonValue("xr://about -> manual download")}});
    }
    std::printf("%s\n", JsonValue(o).Canonical().c_str());
    return 0;
  }

  EmitError("kUnknownMethod", method);
  return 1;
}
