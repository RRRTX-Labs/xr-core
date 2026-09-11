// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: replay protection + the seen-set durability law (P10-T1,
// security req 3). Replayed manifests are refused; the seen-set persists
// with the P6 store law (write-tmp -> fsync -> rename); a corrupt store is
// preserved, never rewritten; a disposable session writes ZERO bytes.
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iterator>

#include "update/core/seen.h"

#include "harness.h"

using namespace xr::update;

namespace {
std::string TempDir(const char* name) {
  const char* base = std::getenv("TMPDIR");
  const std::string dir = std::string(base ? base : "/tmp") + "/xrupdate_" +
                          name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}
}  // namespace

int main() {
  // insert + contains + persistence across instances
  const std::string dir = TempDir("replay");
  {
    SeenSet s(dir);
    std::string err;
    XR_EXPECT_MSG(s.Insert("id-a", &err), "insert a");
    XR_EXPECT_MSG(s.Insert("id-b", &err), "insert b");
    XR_EXPECT_MSG(s.Contains("id-a") && s.Contains("id-b"), "both contained");
  }
  {
    SeenSet s(dir);
    std::string err;
    XR_EXPECT_MSG(s.Load(&err), "loads clean");
    XR_EXPECT_MSG(s.Contains("id-a") && s.Contains("id-b"),
                  "seen-set survived the process boundary");
  }

  // the store file is canonical JSON with the schema mark
  {
    std::ifstream f(dir + "/update-seen.json");
    std::string bytes((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    auto pr = ParseJson(bytes);
    XR_EXPECT_MSG(pr.ok && pr.value.is_object() &&
                      pr.value.find("schema") &&
                      pr.value.find("schema")->as_string() == "xr-update-seen",
                  "store is schema-marked canonical JSON");
  }

  // corrupt store: deny-preserve (never rewritten)
  {
    const std::string path = dir + "/update-seen.json";
    const std::string corrupt = "{\"ids\":[\"trunc";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << corrupt;
    f.close();
    SeenSet s(dir);
    std::string err;
    XR_EXPECT_MSG(!s.Load(&err), "corrupt store refused");
    XR_EXPECT_MSG(err.find("preserved") != std::string::npos,
                  "the refusal names the preserve law");
    std::ifstream f2(path);
    std::string bytes((std::istreambuf_iterator<char>(f2)),
                      std::istreambuf_iterator<char>());
    XR_EXPECT_MSG(bytes == corrupt, "corrupt file preserved byte-exact");
  }

  // FIFO bound
  {
    SeenSet s("");
    std::string err;
    for (size_t i = 0; i < SeenSet::kMaxEntries + 100; ++i) {
      (void)s.Insert("id-" + std::to_string(i), &err);
    }
    XR_EXPECT_MSG(s.size() == SeenSet::kMaxEntries, "set bounded at the cap");
    XR_EXPECT_MSG(!s.Contains("id-0"), "oldest evicted FIFO");
  }

  // disposable: zero bytes
  {
    const std::string sandbox = TempDir("replay_disposable");
    const std::string cwd = sandbox + "/cwd";
    std::filesystem::create_directories(cwd);
    {
      SeenSet s("");
      std::string err;
      (void)s.Insert("id-a", &err);
      XR_EXPECT_MSG(s.Contains("id-a"), "disposable set works in memory");
      XR_EXPECT_MSG(s.disposable(), "reports disposable");
    }
    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(sandbox, ec);
    size_t files = 0;
    for (; it != std::filesystem::recursive_directory_iterator(); ++it) {
      if (it->is_regular_file()) ++files;
    }
    XR_EXPECT_MSG(files == 0, "disposable session left ZERO files");
  }

  // insert refuses an empty id
  {
    SeenSet s("");
    std::string err;
    XR_EXPECT_MSG(!s.Insert("", &err), "empty id refused");
  }

  return xrtest::Report("test_replay");
}
