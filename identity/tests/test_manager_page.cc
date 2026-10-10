// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// test_manager_page — P14-T7 (P14-CLOSE C-3): the xr://identities page
// model. Every page state gets a case; the dev-only law fails closed for
// every non-"dev" channel; edits touch display metadata only and refuse
// without echoing input; stats come from the real store/binding walk.
#include <map>
#include <set>
#include <string>
#include <vector>

#include "common/core/json.h"
#include "core/binding.h"
#include "core/identity.h"
#include "core/manager_page.h"
#include "harness.h"

using xr::common::JsonValue;
using xr::identity::BindingModel;
using xr::identity::CreateRequest;
using xr::identity::IdentityRecord;
using xr::identity::IdentityStore;
using xr::identity::Manager;
using xr::identity::PageEdit;
using xr::identity::PurgeOutcome;

namespace {

IdentityRecord Make(Manager* m, const std::string& entropy, const std::string& name,
                    bool in_memory = false) {
  CreateRequest req;
  req.entropy = entropy;
  req.display_name = name;
  req.in_memory = in_memory;
  IdentityRecord rec;
  XR_EXPECT_MSG(m->Create(req, &rec).ok, "create " + name);
  return rec;
}

void TestDevChannelFailsClosed() {
  std::string why;
  XR_EXPECT(xr::identity::DevChannel("dev", &why));
  XR_EXPECT(why.empty());
  for (const char* ch : {"release", "nightly-test", "", "Dev", "dev ", "devel"}) {
    why.clear();
    XR_EXPECT_MSG(!xr::identity::DevChannel(ch, &why), std::string("refused: '") + ch + "'");
    XR_EXPECT(why == "build-channel-not-dev");
  }
  XR_EXPECT(!xr::identity::DevChannel("release", nullptr));  // null refusal is fine
}

void TestEveryPageState() {
  std::set<std::string> seen;
  // empty
  {
    IdentityStore store;
    BindingModel bind;
    JsonValue p = xr::identity::BuildManagerPage(store, bind, {}, {}, {});
    XR_EXPECT(p.find("state")->as_string() == "empty");
    XR_EXPECT(p.find("rows")->as_array().empty());
    seen.insert(p.find("state")->as_string());
  }
  // normal, with stats
  IdentityStore store;
  Manager m(&store);
  BindingModel bind;
  const IdentityRecord a = Make(&m, "page-a", "Work");
  const IdentityRecord b = Make(&m, "page-b", "Banking");
  XR_EXPECT(bind.SetWindowDefault("w1", a.domain).ok);
  XR_EXPECT(bind.OpenTab("w1", 1, "").ok);
  XR_EXPECT(bind.OpenTab("w1", 2, "").ok);
  XR_EXPECT(bind.OpenTab("w1", 3, b.domain).ok);
  const std::map<std::string, int64_t> perms = {{a.domain, 4}};
  JsonValue p = xr::identity::BuildManagerPage(store, bind, {a.domain, b.domain}, perms, {});
  XR_EXPECT(p.find("state")->as_string() == "normal");
  seen.insert(p.find("state")->as_string());
  const auto& rows = p.find("rows")->as_array();
  XR_EXPECT_EQ(rows.size(), size_t(2));
  XR_EXPECT(rows[0].find("domain")->as_string() == a.domain);  // display order kept
  XR_EXPECT(rows[0].find("tab_count")->as_int() == 2);
  XR_EXPECT(rows[1].find("tab_count")->as_int() == 1);
  XR_EXPECT(rows[0].find("permission_count")->as_int() == 4);
  XR_EXPECT_MSG(rows[1].find("permission_count")->is_null(),
                "an absent permission count is null, never a guessed 0");
  XR_EXPECT(rows[0].find("storage_bytes")->as_int() ==
            static_cast<int64_t>(store.ResidualBytes(a.domain)));
  XR_EXPECT(rows[0].find("storage_bytes")->as_int() > 0);
  XR_EXPECT(rows[0].find("lifecycle")->as_string() == "kActive");
  XR_EXPECT(rows[0].find("display_name")->as_string() == "Work");
  // A moved tab counts for its NEW identity (latest row per tab).
  XR_EXPECT(bind.MoveTab(a.domain, b.domain, 2, false, {}).ok);
  p = xr::identity::BuildManagerPage(store, bind, {a.domain, b.domain}, perms, {});
  XR_EXPECT(p.find("rows")->as_array()[0].find("tab_count")->as_int() == 1);
  XR_EXPECT(p.find("rows")->as_array()[1].find("tab_count")->as_int() == 2);
  // purge-unverified: a purge that left bytes behind names them.
  store.PlantResidual(b.domain, {"stray-cache", 64});
  PurgeOutcome po;
  po.domain = b.domain;
  XR_EXPECT(!m.Destroy(b.domain, &po.verified).ok);
  XR_EXPECT(!po.verified);
  po.residual_kinds = store.ResidualKinds(b.domain);
  p = xr::identity::BuildManagerPage(store, bind, {a.domain, b.domain}, perms, {po});
  XR_EXPECT(p.find("state")->as_string() == "purge-unverified");
  seen.insert(p.find("state")->as_string());
  XR_EXPECT_EQ(p.find("rows")->as_array().size(), size_t(1));  // b is gone
  const auto& pr = p.find("purges")->as_array();
  XR_EXPECT_EQ(pr.size(), size_t(1));
  XR_EXPECT(!pr[0].find("verified")->as_bool());
  XR_EXPECT(pr[0].find("residual_kinds")->Canonical().find("stray-cache") != std::string::npos);
  // A verified purge does not raise the state.
  PurgeOutcome ok;
  ok.domain = "xr:00000000-0000-4000-8000-000000000001";
  ok.verified = true;
  p = xr::identity::BuildManagerPage(store, bind, {a.domain}, perms, {ok});
  XR_EXPECT(p.find("state")->as_string() == "normal");
  // dev-refused is the host's typed refusal; the core's list carries it.
  seen.insert("dev-refused");
  std::set<std::string> listed;
  for (const char* st : xr::identity::kManagerPageStates) listed.insert(st);
  XR_EXPECT_MSG(listed == seen, "every listed page state has a case, and no case is unlisted");
}

void TestEditsTouchMetadataOnly() {
  IdentityStore store;
  Manager m(&store);
  const IdentityRecord a = Make(&m, "edit-a", "Work");
  XR_EXPECT(xr::identity::ApplyPageEdit(&store, &m, a.domain, PageEdit::kRename, "Client X").ok);
  XR_EXPECT(store.Find(a.domain)->display_name == "Client X");
  XR_EXPECT(store.Find(a.domain)->domain == a.domain);  // the key never moves
  XR_EXPECT(xr::identity::ApplyPageEdit(&store, &m, a.domain, PageEdit::kRecolor, "#0f9d58").ok);
  XR_EXPECT(store.Find(a.domain)->color == "#0f9d58");
  XR_EXPECT(xr::identity::ApplyPageEdit(&store, &m, a.domain, PageEdit::kArchive, "").ok);
  XR_EXPECT(store.Find(a.domain)->state == xr::identity::State::kHibernated);

  struct Case { PageEdit e; std::string v; const char* want; };
  const std::string long_name(xr::identity::kDisplayNameMax + 1, 'n');
  const std::string max_name(xr::identity::kDisplayNameMax, 'n');
  XR_EXPECT(xr::identity::ApplyPageEdit(&store, &m, a.domain, PageEdit::kRename, max_name).ok);
  const std::string embed = a.domain.substr(3, 13);  // 13 chars of the domain itself
  const Case cases[] = {
      {PageEdit::kRename, "", "kMalformedInput (name-empty)"},
      {PageEdit::kRename, long_name, "kMalformedInput (name-too-long)"},
      {PageEdit::kRename, "Bad\nName", "kMalformedInput (name-control-char)"},
      {PageEdit::kRename, std::string("Bad") + char(0x7f), "kMalformedInput (name-control-char)"},
      {PageEdit::kRename, std::string("Bad") + char(0x1f), "kMalformedInput (name-control-char)"},
      {PageEdit::kRename, embed, "kNotPermitted (domain not opaque to this name)"},
      {PageEdit::kRecolor, "#0F9D58", "kMalformedInput (color-not-hex)"},
      {PageEdit::kRecolor, "0f9d58", "kMalformedInput (color-not-hex)"},
      {PageEdit::kRecolor, "#0f9d5", "kMalformedInput (color-not-hex)"},
      {PageEdit::kRecolor, "#0f9d5g", "kMalformedInput (color-not-hex)"},
      {PageEdit::kRecolor, "#/f9d58", "kMalformedInput (color-not-hex)"},
      {PageEdit::kRecolor, "#:f9d58", "kMalformedInput (color-not-hex)"},
      {PageEdit::kRecolor, "#`f9d58", "kMalformedInput (color-not-hex)"},
  };
  for (const Case& c : cases) {
    auto r = xr::identity::ApplyPageEdit(&store, &m, a.domain, c.e, c.v);
    XR_EXPECT_MSG(!r.ok, std::string("refused: ") + c.want);
    XR_EXPECT_MSG(r.error == c.want, "reason: " + r.error);
    if (!c.v.empty()) {
      XR_EXPECT_MSG(r.error.find(c.v) == std::string::npos, "a refusal never echoes input");
    }
  }
  XR_EXPECT(store.Find(a.domain)->display_name == max_name);  // refusals changed nothing
  XR_EXPECT(store.Find(a.domain)->color == "#0f9d58");
  XR_EXPECT(xr::identity::ApplyPageEdit(&store, &m, a.domain, PageEdit::kRecolor, "#a0b9c8").ok);
  XR_EXPECT(xr::identity::ApplyPageEdit(&store, &m, a.domain, PageEdit::kRecolor, "#fedcba").ok);
  XR_EXPECT(xr::identity::ApplyPageEdit(&store, &m, "xr:00000000-0000-4000-8000-0000000000ff",
                                        PageEdit::kRename, "X").error == "kUnknownIdentity");
}

void TestTabCountsFollowLatestRow() {
  BindingModel bind;
  const std::string kA = "xr:00000000-0000-4000-8000-00000000000a";
  const std::string kB = "xr:00000000-0000-4000-8000-00000000000b";
  XR_EXPECT(bind.SetWindowDefault("w", kA).ok);
  XR_EXPECT(bind.OpenTab("w", 7, "").ok);
  XR_EXPECT(bind.OpenTab("w", 8, "").ok);
  XR_EXPECT(bind.MoveTab(kA, kB, 8, false, {}).ok);
  auto c = xr::identity::TabCounts(bind);
  XR_EXPECT_EQ(c.size(), size_t(2));
  XR_EXPECT(c[kA] == 1);
  XR_EXPECT(c[kB] == 1);
}

}  // namespace

int main() {
  TestDevChannelFailsClosed();
  TestEveryPageState();
  TestEditsTouchMetadataOnly();
  TestTabCountsFollowLatestRow();
  return xrtest::Report("identity/manager_page");
}
