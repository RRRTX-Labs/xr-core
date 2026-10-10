// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// test_ledger_tag — P14-T5 (P14-CLOSE C-2): the identity overlay laws L1–L5
// from core/ledger_tag.h. Every event class gets a case (L2 is a closed set,
// so the test enumerates it and fails if the set grows without a case).
#include <set>
#include <string>

#include "common/core/json.h"
#include "core/ledger_tag.h"
#include "harness.h"

using xr::common::JsonValue;
using xr::common::ParseJson;

namespace {

const char* kA = "xr:00000000-0000-4000-8000-00000000000a";
const char* kB = "xr:00000000-0000-4000-8000-00000000000b";

JsonValue J(const std::string& text) {
  auto r = ParseJson(text);
  XR_EXPECT_MSG(r.ok, "fixture JSON parses: " + text);
  return r.value;
}

void TestEveryClassTagsAndRequiresId() {
  const auto& classes = xr::identity::LedgerEventClasses();
  XR_EXPECT(classes.size() == 10);  // 8 frozen ActivityKind + kHistory/kBookmark
  std::set<std::string> seen;
  for (const auto& cls : classes) {
    seen.insert(cls);
    JsonValue out;
    std::string why;
    const std::string req = std::string("{\"identity_id\":\"") + kA +
                            "\",\"event_class\":\"" + cls +
                            "\",\"event\":{\"k\":1}}";
    XR_EXPECT_MSG(xr::identity::TagEvent(J(req), &out, &why), "tags " + cls);
    XR_EXPECT(out.find("identity_id")->as_string() == kA);
    XR_EXPECT(out.find("schema")->as_string() == "ledger-identity-overlay-v1");
    XR_EXPECT(out.find("event")->Canonical() == "{\"k\":1}");  // L3 byte-carry
    // L1: the same event with NO identity is refused, per class.
    const std::string bare = "{\"event_class\":\"" + cls + "\",\"event\":{}}";
    XR_EXPECT(!xr::identity::TagEvent(J(bare), &out, &why));
    XR_EXPECT_MSG(why == "identity-id-required", "no default identity for " + cls);
  }
  XR_EXPECT(seen.size() == classes.size());  // no duplicates in the closed set
}

void TestRefusals() {
  JsonValue out;
  std::string why;
  XR_EXPECT(!xr::identity::TagEvent(
      J("{\"identity_id\":\"\",\"event_class\":\"kBlock\",\"event\":{}}"), &out, &why));
  XR_EXPECT(why == "identity-id-required");
  XR_EXPECT(!xr::identity::TagEvent(
      J("{\"identity_id\":\"work\",\"event_class\":\"kBlock\",\"event\":{}}"), &out, &why));
  XR_EXPECT(why == "identity-id-malformed");  // a display name is not an id
  XR_EXPECT(!xr::identity::TagEvent(
      J(std::string("{\"identity_id\":\"") + kA + "\",\"event_class\":\"kTelemetry\",\"event\":{}}"),
      &out, &why));
  XR_EXPECT(why == "unknown-event-class:kTelemetry");
  XR_EXPECT(!xr::identity::TagEvent(
      J(std::string("{\"identity_id\":\"") + kA + "\",\"event_class\":\"kBlock\",\"event\":[1]}"),
      &out, &why));
  XR_EXPECT(why == "event-not-object");
}

void TestOmniboxCannotSeeAcrossIdentities() {
  const std::string rows = std::string("[") +
      "{\"identity_id\":\"" + kA + "\",\"url\":\"https://a.example/\"}," +
      "{\"identity_id\":\"" + kB + "\",\"url\":\"https://b-secret.example/\"}," +
      "{\"url\":\"https://untagged.example/\"}," +
      "{\"identity_id\":\"garbage\",\"url\":\"https://bad.example/\"}]";
  JsonValue out;
  std::string why;
  XR_EXPECT(xr::identity::FilterOmnibox(
      J(std::string("{\"identity_id\":\"") + kA + "\",\"rows\":" + rows + "}"), &out, &why));
  const auto& cur = out.find("current")->as_array();
  XR_EXPECT(cur.size() == 1);
  for (const auto& r : cur) {
    XR_EXPECT_MSG(r.find("identity_id")->as_string() == kA,
                  "a query in A returned a row owned by someone else");
  }
  // B's row is gone and not even counted: its URL is nowhere in the answer.
  XR_EXPECT(out.Canonical().find("b-secret") == std::string::npos);
  XR_EXPECT(out.Canonical().find(kB) == std::string::npos);
  // Untagged and malformed rows are UNKNOWN, never shown as A's.
  const auto& unk = out.find("unknown")->as_array();
  XR_EXPECT(unk.size() == 2);
  for (const auto& r : unk) {
    XR_EXPECT(r.find("provenance")->as_string() == "unknown");
    XR_EXPECT(r.find("identity_id") == nullptr);
  }
  // A query with no identity sees nothing (refused), not everything.
  XR_EXPECT(!xr::identity::FilterOmnibox(J("{\"rows\":" + rows + "}"), &out, &why));
  XR_EXPECT(why == "identity-id-required");
}

void TestHistoryOracleZeroDelta() {
  const std::string req = std::string("{\"identity_id\":\"") + kA +
      "\",\"upstream_rows\":[" +
      "{\"title\":\"A\",\"ts\":1,\"url\":\"https://a.example/\",\"url_id\":1,\"visit_id\":10}," +
      "{\"title\":\"\\u00e9\",\"ts\":2,\"url\":\"https://b.example/\",\"url_id\":2,\"visit_id\":11}]}";
  JsonValue out;
  std::string why;
  XR_EXPECT(xr::identity::HistoryOracle(J(req), &out, &why));
  XR_EXPECT(out.find("verdict")->as_string() == "diff-clean");
  XR_EXPECT(out.find("overlay_rows")->as_int() == 2);
  XR_EXPECT(out.find("upstream_bytes_after")->as_int() ==
            out.find("upstream_bytes_before")->as_int());
  // An upstream row with a field upstream does not have is refused: the
  // oracle will not certify a write path it does not recognise.
  XR_EXPECT(!xr::identity::HistoryOracle(
      J(std::string("{\"identity_id\":\"") + kA +
        "\",\"upstream_rows\":[{\"url_id\":1,\"identity_id\":\"x\"}]}"), &out, &why));
  XR_EXPECT(why == "upstream-row-unknown-field:identity_id");
}

}  // namespace

int main() {
  TestEveryClassTagsAndRequiresId();
  TestRefusals();
  TestOmniboxCannotSeeAcrossIdentities();
  TestHistoryOracleZeroDelta();
  return xrtest::Report("identity/ledger_tag");
}
