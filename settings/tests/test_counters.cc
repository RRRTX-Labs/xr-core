// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — the Attention-Budget ledger (P8-T6), LOCAL COUNTERS
// ONLY: day-granular buckets, 90-day retention on save, atomic
// write-tmp->fsync->rename persistence, corrupt-file deny-preserve (Load
// keeps the file; Save refuses to overwrite), and the disposable law:
// counters accumulate in memory and write ZERO persistent bytes until an
// explicit persist — proven here with a filesystem-diff (snapshot before /
// after, expect no new files).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "settings/core/counters.h"
#include "settings/core/json.h"
#include <fstream>
#include "settings/tests/harness.h"

using namespace xr::settings;

namespace {
int Sys(const std::string& cmd) {
  int rc = std::system(cmd.c_str());
  return rc;
}


const char* kStore = "counters-test-store";

std::string Snapshot(const std::string& dir) {
  const std::string cmd = "find \"" + dir + "\" -type f | sort";
  std::FILE* p = popen(cmd.c_str(), "r");
  std::string out;
  if (!p) return out;
  char buf[512];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
  pclose(p);
  return out;
}

}  // namespace

int main() {
  Sys("rm -rf " + std::string(kStore));
  Sys("mkdir -p " + std::string(kStore));

  // LoadResult semantics: absent file / empty dir => error empty (fine);
  // corrupt => ok=false + preserved + non-empty error.
  auto ok_or_absent = [](const CounterStore::LoadResult& lr) {
    return lr.ok || lr.error.empty();
  };

  // 1. Disposable / in-memory store (dir ""): events accumulate, Save is a
  //    no-op success, nothing ever touches disk.
  {
    CounterStore mem;
    XR_EXPECT(mem.in_memory());
    XR_EXPECT_STREQ(mem.Path(), "");
    auto lr = mem.Load();
    XR_EXPECT_MSG(ok_or_absent(lr), lr.error);
    mem.OpenSection("network");
    mem.AcceptQuery();
    mem.SettingChanged("network.adblock");
    std::string err;
    XR_EXPECT_MSG(mem.Save(&err), "disposable Save must no-op OK: " + err);
    XR_EXPECT_EQ(mem.TotalQueries(), 1);
    auto dump = mem.Dump();
    const JsonValue* im = dump.find("in_memory");
    XR_EXPECT(im != nullptr && im->is_bool() && im->as_bool());
  }

  // 2. Filesystem-diff disposable law: events WITHOUT an explicit persist
  //    write nothing; snapshot before == snapshot after.
  {
    const std::string d = std::string(kStore) + "/disposable";
    Sys("mkdir -p \"" + d + "\"");
    const std::string before = Snapshot(d);
    CounterStore cs(d);
    cs.Load();
    cs.OpenSection("privacy");
    cs.AcceptQuery();
    cs.SettingChanged("privacy.notifications");
    // NOTE: no Save() — the disposable/ephemeral law is that counters stay
    // in memory and are discarded unless the context explicitly persists.
    const std::string after = Snapshot(d);
    XR_EXPECT_STREQ(before, after);
    XR_EXPECT_EQ(cs.TotalQueries(), 1);
  }

  // 3. Durable path: an explicit Save persists; a fresh instance reloads
  //    the same day-granular buckets.
  {
    CounterStore cs(kStore);
    auto lr = cs.Load();
    XR_EXPECT_MSG(ok_or_absent(lr), lr.error);
    cs.OpenSection("network");
    cs.AcceptQuery();
    cs.AcceptQuery();
    cs.SettingChanged("network.adblock");
    std::string err;
    XR_EXPECT_MSG(cs.Save(&err), err);
    const std::string today = CounterStore::UtcToday();
    XR_EXPECT_EQ(today.size(), 10);  // YYYY-MM-DD — nothing finer is stored

    CounterStore cs2(kStore);
    auto lr2 = cs2.Load();
    XR_EXPECT_MSG(lr2.ok, lr2.error);
    XR_EXPECT_EQ(cs2.TotalQueries(), 2);
    auto dump = cs2.Dump();
    const JsonValue* days = dump.find("days");
    XR_EXPECT(days != nullptr && days->is_object());
    const JsonValue* today_row = days ? days->find(today) : nullptr;
    XR_EXPECT(today_row != nullptr);
    if (today_row) {
      const JsonValue* queries = today_row->find("queries");
      XR_EXPECT(queries != nullptr && queries->is_int() &&
                queries->as_int() == 2);
      const JsonValue* opened = today_row->find("opened");
      XR_EXPECT(opened != nullptr && opened->is_object());
      const JsonValue* changed = today_row->find("changed");
      XR_EXPECT(changed != nullptr && changed->is_object());
    }
  }

  // 4. Corrupt ledger: Load reports failure + preserved; the file keeps its
  //    exact bytes; Save REFUSES to overwrite (deny-preserve).
  {
    const std::string path = std::string(kStore) + "/settings-counters.json";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    XR_EXPECT(f != nullptr);
    if (f) {
      std::fputs("{{{ not json", f);
      std::fclose(f);
    }
    CounterStore cs(kStore);
    auto lr = cs.Load();
    XR_EXPECT_MSG(!lr.ok, "corrupt ledger must report failure");
    XR_EXPECT_MSG(lr.preserved, "corrupt ledger must be preserved untouched");
    XR_EXPECT(!lr.error.empty());
    cs.OpenSection("network");  // events still record in memory
    std::string err;
    XR_EXPECT_MSG(!cs.Save(&err), "Save must refuse to clobber a corrupt ledger");
    XR_EXPECT(err.find("preserved") != std::string::npos);
    bool ok = false;
    std::string text = xrtest::ReadFile(path, &ok);
    XR_EXPECT(text.find("{{{ not json") != std::string::npos);
  }

  // 5. Schema-mismatch ledger: preserved the same way (downgrade no-op).
  {
    const std::string path = std::string(kStore) + "/settings-counters.json";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    XR_EXPECT(f != nullptr);
    if (f) {
      std::fputs("{\"schema\":\"xr-settings-counters\",\"schema_version\":99,"
                 "\"days\":{}}",
                 f);
      std::fclose(f);
    }
    CounterStore cs(kStore);
    auto lr = cs.Load();
    XR_EXPECT(!lr.ok && lr.preserved);
    std::string err;
    XR_EXPECT(!cs.Save(&err));
    bool ok = false;
    std::string text = xrtest::ReadFile(path, &ok);
    XR_EXPECT(text.find("\"schema_version\":99") != std::string::npos);
  }

  // 6. Retention policy constant + version (file header states the law).
  {
    XR_EXPECT_EQ(CounterStore::kRetentionDays, 90);
    XR_EXPECT_EQ(CounterStore::kVersion, 1);
    XR_EXPECT_STREQ(CounterStore::kSchema, "xr-settings-counters");
  }

  // 7. Kill-safety: a save leaves no *.tmp behind (atomic rename path).
  {
    // repair the store by removing the preserved corrupt file
    Sys("rm -f " + std::string(kStore) + "/settings-counters.json");
    CounterStore cs(kStore);
    cs.Load();
    cs.OpenSection("identity");
    std::string err;
    XR_EXPECT_MSG(cs.Save(&err), err);
    const std::string cmd =
        "ls " + std::string(kStore) + "/*.tmp 2>/dev/null | wc -l";
    std::FILE* p = popen(cmd.c_str(), "r");
    char buf[16] = {0};
    if (p) {
      if (std::fgets(buf, sizeof(buf), p)) {
        XR_EXPECT_STREQ(std::string(buf).substr(0, 1), "0");
      }
      pclose(p);
    }
    // ledger is parseable again after the repair
    CounterStore cs2(kStore);
    auto lr2 = cs2.Load();
    XR_EXPECT_MSG(lr2.ok, lr2.error);
  }

  Sys("rm -rf " + std::string(kStore));
  std::printf("suite: %d checks, %d failures\n", xrtest::g_checks,
              xrtest::g_failures);
  // P15 hardening (mutation survivors): a ledger that parses to a non-object
  // is corrupt and is preserved, never rewritten.
  {
    const std::string d = std::string(kStore) + "/non-object";
    Sys("mkdir -p \"" + d + "\"");
    std::ofstream(d + "/settings-counters.json") << "[]";
    CounterStore cs(d);
    const CounterStore::LoadResult lr = cs.Load();
    XR_EXPECT_MSG(!lr.ok && lr.preserved && lr.error.find("corrupt") != std::string::npos,
                  "non-object ledger is preserved as corrupt");
  }
  // P15 hardening (mutation survivors): a ledger that cannot be written reports
  // the failure (the tmp path is a directory, so the open fails).
  {
    const std::string d = std::string(kStore) + "/tmp-blocked";
    Sys("mkdir -p \"" + d + "\"");
    CounterStore cs(d);
    cs.Load();
    Sys("mkdir -p \"" + cs.Path() + ".tmp\"");
    std::string err;
    XR_EXPECT_MSG(!cs.Save(&err) && !err.empty(), "unwritable tmp ledger fails Save");
  }

  // P15-CLOSE C11: kills for the full-matrix survivors on counters.cc
  // (xr-core c9c51d8, seed 20260911). Each block names the behaviour it pins.
  const std::string c11 = std::string(kStore) + "/c11";
  auto write_file = [](const std::string& path, const std::string& text) {
    std::ofstream o(path, std::ios::binary);
    o << text;
  };
  auto day_count = [](const CounterStore& cs, const std::string& key,
                      const std::string& sub) -> long long {
    const JsonValue dump = cs.Dump();
    const JsonValue* days = dump.find("days");
    if (days == nullptr || !days->is_object()) return -1;
    const JsonValue* day = days->find(CounterStore::UtcToday());
    if (day == nullptr) return 0;
    const JsonValue* map = day->find(key);
    if (map == nullptr || !map->is_object()) return 0;
    const JsonValue* n = map->find(sub);
    return (n != nullptr && n->is_int()) ? n->as_int() : 0;
  };
  auto day_total = [](const CounterStore& cs) -> size_t {
    const JsonValue dump = cs.Dump();
    const JsonValue* days = dump.find("days");
    return (days != nullptr && days->is_object()) ? days->as_object().size() : 99;
  };
  // Counts accumulate: two opens of one section read 2, one change reads 1.
  {
    CounterStore cs;
    cs.Load();
    cs.OpenSection("network");
    cs.OpenSection("network");
    cs.SettingChanged("network.adblock");
    XR_EXPECT_EQ(day_count(cs, "opened", "network"), 2);
    XR_EXPECT_EQ(day_count(cs, "changed", "network.adblock"), 1);
  }
  // No-op on nothing: empty names, and any event before Load, record nothing.
  {
    CounterStore before;
    before.OpenSection("network");
    before.SettingChanged("network.adblock");
    XR_EXPECT_MSG(day_total(before) == 0, "events before Load record nothing");
    CounterStore empty_names;
    empty_names.Load();
    empty_names.OpenSection("");
    empty_names.SettingChanged("");
    XR_EXPECT_MSG(day_total(empty_names) == 0, "empty section/key records nothing");
  }
  // A valid on-disk ledger loads clean and Save may overwrite it.
  {
    Sys("rm -rf \"" + c11 + "\" && mkdir -p \"" + c11 + "/valid\"");
    CounterStore cs(c11 + "/valid");
    write_file(cs.Path(),
               "{\"days\":{},\"schema\":\"xr-settings-counters\",\"schema_version\":1}\n");
    const auto lr = cs.Load();
    XR_EXPECT(lr.ok && !lr.preserved);
    XR_EXPECT(cs.Dump().find("load_error") == nullptr);
    cs.AcceptQuery();
    std::string err;
    XR_EXPECT_MSG(cs.Save(&err), "a cleanly loaded ledger saves: " + err);
  }
  // A corrupt ledger that has since been removed no longer blocks Save.
  {
    Sys("mkdir -p \"" + c11 + "/gone\"");
    CounterStore cs(c11 + "/gone");
    write_file(cs.Path(), "{truncated");
    XR_EXPECT(cs.Load().preserved);
    std::remove(cs.Path().c_str());
    std::string err;
    XR_EXPECT_MSG(cs.Save(&err), "nothing left to preserve, so Save writes: " + err);
  }
  // A store dir that cannot be created fails Save.
  {
    write_file(c11 + "/plain-file", "x");
    CounterStore cs(c11 + "/plain-file/sub");
    cs.Load();
    std::string err;
    XR_EXPECT_MSG(!cs.Save(&err) && err.find("cannot create store dir") == 0,
                  "uncreatable store dir fails Save");
  }
  // The cutoff helper: 90 days back, and a refusal on an unparsable day.
  {
    std::string cut;
    XR_EXPECT(CounterStore::RetentionCutoff("2026-10-11", &cut));
    XR_EXPECT_STREQ(cut, "2026-07-13");
    std::string untouched = "keep";
    XR_EXPECT_MSG(!CounterStore::RetentionCutoff("not-a-day", &untouched) &&
                      untouched == "keep",
                  "an unparsable day refuses and writes no cutoff");
  }
  // Retention boundary: the cutoff day itself survives Save; older days drop.
  {
    Sys("mkdir -p \"" + c11 + "/retain\"");
    std::string cut;
    XR_EXPECT(CounterStore::RetentionCutoff(CounterStore::UtcToday(), &cut));
    CounterStore cs(c11 + "/retain");
    write_file(cs.Path(),
               "{\"days\":{\"2000-01-01\":{\"queries\":1},\"" + cut +
                   "\":{\"queries\":2}},\"schema\":\"xr-settings-counters\","
                   "\"schema_version\":1}\n");
    XR_EXPECT(cs.Load().ok);
    std::string err;
    XR_EXPECT_MSG(cs.Save(&err), err);
    CounterStore back(c11 + "/retain");
    XR_EXPECT(back.Load().ok);
    XR_EXPECT_MSG(back.TotalQueries() == 2, "cutoff day kept, 2000-01-01 dropped");
  }
  // A failed flush (tmp ledger on /dev/full) fails Save and keeps no ledger.
  {
    Sys("mkdir -p \"" + c11 + "/full\"");
    CounterStore cs(c11 + "/full");
    cs.Load();
    cs.AcceptQuery();
    Sys("ln -sf /dev/full \"" + cs.Path() + ".tmp\"");
    std::string err;
    XR_EXPECT_MSG(!cs.Save(&err) && err == "write/fsync failed",
                  "ENOSPC on flush fails Save: " + err);
    std::FILE* f = std::fopen(cs.Path().c_str(), "rb");
    XR_EXPECT_MSG(f == nullptr, "no ledger left behind after a failed flush");
    if (f) std::fclose(f);
  }
  // A rename that cannot land (a directory holds the ledger path) fails Save.
  {
    Sys("mkdir -p \"" + c11 + "/blocked\"");
    CounterStore cs(c11 + "/blocked");
    cs.Load();
    Sys("mkdir -p \"" + cs.Path() + "/occupied\"");
    std::string err;
    XR_EXPECT_MSG(!cs.Save(&err) && err == "rename failed", "rename failure fails Save: " + err);
  }
  Sys("rm -rf \"" + c11 + "\"");

  return xrtest::Report("test_counters");
}
