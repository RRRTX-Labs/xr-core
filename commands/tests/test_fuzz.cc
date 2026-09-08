// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S1-test — seeded structure-aware fuzz of the dispatch security edge +
// the JSON parser + the frozen descriptor validator (P7 SECURITY req: "fuzz
// corpus (structure-aware, seeded) asserts no spoofed/unknown id executes, no
// crash"). The seed is checked in so the fuzz is byte-reproducible; the
// invariant counts below prove the fuzz actually exercised each danger class
// (a fuzz that never hits a page-source is a fuzz that proves nothing).
//
// Three lanes, all must end 0-failure:
//   1. DISPATCH: random (id, source, confirmed) — a non-whitelisted source
//      (incl. "page", empty, garbage) is rejected BEFORE the id lookup; an
//      unknown id never executes; a destructive command without confirmed never
//      auto-runs; an authorized outcome's handler matches the registry.
//   2. JSON: random byte strings (structural + garbage + multibyte UTF-8) —
//      ParseJson returns a typed result, never crashes, never throws.
//   3. DESCRIPTOR: directed known-bad + known-good — the frozen validator is
//      strict (unknown field / missing field / bad enum / empty / non-object).
#include <random>
#include <string>
#include <vector>

#include "commands/core/descriptor.h"
#include "commands/core/dispatch.h"
#include "commands/core/json.h"
#include "commands/core/registry.h"
#include "commands/tests/harness.h"

using namespace xr::commands;

namespace {

constexpr uint32_t kSeed = 20260908;  // checked in — reproducible
constexpr int kDispatchIters = 20000;
constexpr int kJsonIters = 20000;

Registry BuildRegistry() {
  Registry r;
  auto add = [](const char* id, const char* danger, const char* tier) {
    Command c;
    c.id = id;
    c.title = id;
    c.attention_tier = tier;
    c.danger_class = danger;
    c.surface = "command-palette";
    c.handler = std::string("action.") + id;
    c.predicate_id = "always";
    return c;
  };
  (void)r.Register(add("a.safe", "safe", "tier2"));
  (void)r.Register(add("b.caution", "caution", "tier2"));
  (void)r.Register(add("c.destructive", "destructive", "tier1"));
  return r;
}

}  // namespace

int main() {
  Registry reg = BuildRegistry();
  Dispatcher d(reg);
  const auto& allowed = Dispatcher::AllowedSources();

  std::mt19937 rng(kSeed);
  auto idx = [&](int n) { return static_cast<size_t>(rng() % n); };

  // --- Lane 1: dispatch security fuzz -------------------------------------
  const std::vector<std::string> id_space = {
      "a.safe", "b.caution", "c.destructive", "", "no.such.id",
      "action.a.safe", "page", " ", "a.safe\u0000evil", "PA GE"};
  const std::vector<std::string> source_space = {
      "ui-chrome", "palette", "menu", "shortcut", "test", "page", "PAGE",
      "", "xss", "\u0000", " "} ;
  const std::vector<char> id_chars = {'a', 'z', '.', '0', '9', ' ', '-', '_',
                                      'e', 'v', 'i', 'l'};
  const std::vector<char> src_chars = {'p', 'a', 'g', 'e', 'x', ' ', 's'};

  int saw_bad_source = 0, saw_unknown_id = 0, saw_destructive_unconfirmed = 0,
      authorized_matched = 0;
  for (int i = 0; i < kDispatchIters; ++i) {
    std::string id, source;
    if (rng() % 2) id = id_space[idx(id_space.size())];
    else { for (int k = 0; k < 6; ++k) id += id_chars[idx(id_chars.size())]; }
    if (rng() % 2) source = source_space[idx(source_space.size())];
    else { for (int k = 0; k < 5; ++k) source += src_chars[idx(src_chars.size())]; }
    bool confirmed = (rng() % 2) == 0;

    InvokeOutcome o = d.Invoke(id, source, confirmed);  // must not throw/crash

    bool source_allowed = false;
    for (const auto& a : allowed) if (a == source) source_allowed = true;
    if (!source_allowed) {
      ++saw_bad_source;
      XR_EXPECT_MSG(!o.authorized && o.status == "rejected",
                    "non-whitelisted source must be rejected before id lookup");
    }
    if (!reg.KnownId(id)) {
      ++saw_unknown_id;
      XR_EXPECT_MSG(!o.authorized, "unknown id must never execute");
    }
    const Command* c = reg.Find(id);
    if (c && c->danger_class == "destructive" && !confirmed) {
      ++saw_destructive_unconfirmed;
      XR_EXPECT_MSG(!o.authorized,
                    "destructive without confirmed must not auto-run");
    }
    if (o.authorized) {
      ++authorized_matched;
      XR_EXPECT_MSG(c != nullptr && o.handler == c->handler,
                    "authorized outcome's handler must match the registry");
    }
  }

  // --- Lane 2: JSON parse no-crash fuzz -----------------------------------
  const unsigned char kChars[] = {
      '{', '}', '[', ']', '"', ':', ',', '-', '.', '0', '1', '9',
      't', 'r', 'u', 'e', 'f', 'a', 'l', 's', 'n', '\n', '\t', ' ', '\\',
      '/', 0x80, 0x81, 0xFF, 0x00};  // structural + garbage + multibyte/edge
  int json_ok = 0, json_err = 0;
  for (int i = 0; i < kJsonIters; ++i) {
    std::string doc;
    size_t len = rng() % 40;
    for (size_t k = 0; k < len; ++k) doc += static_cast<char>(kChars[idx(sizeof(kChars))]);
    JsonParseResult p = ParseJson(doc);  // typed result, never throws/crashes
    if (p.ok) ++json_ok; else ++json_err;
  }
  (void)json_ok;
  (void)json_err;

  // --- Lane 3: descriptor strictness (directed) ---------------------------
  const char* kGood =
      R"({"id":"x.y","title":"T","attention_tier":"tier2","danger_class":"safe","surface":"s","handler":"h"})";
  XR_EXPECT_MSG(ValidateDescriptor(ParseJson(kGood).value).ok,
                "a conforming descriptor validates");
  const std::vector<std::string> kBad = {
      std::string("{") + "}",                                            // empty object (missing fields)
      R"({"id":"x","title":"T","attention_tier":"tier2","danger_class":"safe","surface":"s","handler":"h","rogue":1})",  // unknown field
      R"({"id":"x","title":"T","attention_tier":"tier9","danger_class":"safe","surface":"s","handler":"h})",  // bad tier enum
      R"({"id":"x","title":"T","attention_tier":"tier2","danger_class":"nuclear","surface":"s","handler":"h})",  // bad danger enum
      R"({"id":"","title":"T","attention_tier":"tier2","danger_class":"safe","surface":"s","handler":"h})",  // empty id
      R"([1,2,3])",                                                          // not an object
  };
  for (const auto& b : kBad) {
    JsonParseResult p = ParseJson(b);
    bool rejected = !p.ok || !ValidateDescriptor(p.value).ok;
    XR_EXPECT_MSG(rejected, ("a non-conforming descriptor must be rejected: " + b).c_str());
  }

  // Prove the fuzz exercised each danger class (a 0-count means the corpus is
  // hollow and proves nothing).
  XR_EXPECT_MSG(saw_bad_source > 0, "fuzz exercised non-whitelisted sources");
  XR_EXPECT_MSG(saw_unknown_id > 0, "fuzz exercised unknown ids");
  XR_EXPECT_MSG(saw_destructive_unconfirmed > 0, "fuzz exercised destructive-without-confirm");
  XR_EXPECT_MSG(authorized_matched > 0, "fuzz exercised authorized outcomes");

  std::printf("  fuzz: seed %u, dispatch %d iters (bad-source %d, unknown-id %d, "
              "destructive %d, authorized %d), json %d iters, descriptor 7 directed\n",
              kSeed, kDispatchIters, saw_bad_source, saw_unknown_id,
              saw_destructive_unconfirmed, authorized_matched, kJsonIters);
  return xrtest::Report("test_fuzz");
}
