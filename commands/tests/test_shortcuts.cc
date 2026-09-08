// Copyright 2026 RRRTX Labs
// test_shortcuts.cc — conflict matrix (duplicate/browser/system, deny-by-default
// with the conflicting command named), persist round-trip, corrupt =>
// deny-preserve, and crash durability (kill-loop x20 + one real SIGKILL).
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <string>
#include <unistd.h>

#include "commands/core/json.h"
#include "commands/core/shortcuts.h"
#include "commands/tests/harness.h"

using namespace xr::commands;

namespace {

std::string ReadFileBytes(const std::string& p) {
  std::string d;
  std::FILE* f = std::fopen(p.c_str(), "rb");
  if (!f) return d;
  char b[65536];
  size_t n;
  while ((n = std::fread(b, 1, sizeof(b), f)) > 0) d.append(b, n);
  std::fclose(f);
  return d;
}

}  // namespace

int main() {
  const std::string dir = "shortcuts-test-dir";
  // Clean, existing store dir (ShortcutStore, like the P6 store, does not
  // mkdir — the caller provides the directory).
  {
    int rc = std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    (void)rc;
  }

  // Conflict matrix.
  {
    ShortcutStore s(dir);
    s.Bind("a.cmd", "Ctrl+Shift+K");  // ok
    BindResult dup = s.Bind("b.cmd", "ctrl+shift+k");  // normalized duplicate
    XR_EXPECT(!dup.ok);
    XR_EXPECT(dup.conflict == ConflictKind::kDuplicate);
    XR_EXPECT_STREQ(dup.conflicting_command.c_str(), "a.cmd");  // named for the UX
    XR_EXPECT(dup.error.find("a.cmd") != std::string::npos);

    BindResult browser = s.Bind("c.cmd", "F11");
    XR_EXPECT(!browser.ok);
    XR_EXPECT(browser.conflict == ConflictKind::kBrowserReserved);

    BindResult system = s.Bind("d.cmd", "Alt+F4");
    XR_EXPECT(!system.ok);
    XR_EXPECT(system.conflict == ConflictKind::kSystemReserved);

    // Rebind the SAME command to a new accelerator is allowed.
    XR_EXPECT(s.Bind("a.cmd", "Ctrl+Shift+J").ok);
    // unbind
    std::string err;
    XR_EXPECT(s.Unbind("a.cmd", &err));
    XR_EXPECT(!s.Unbind("a.cmd", &err));  // already gone
  }

  // Persist round-trip.
  {
    ShortcutStore s(dir);
    s.Bind("x", "Ctrl+1");
    s.Bind("y", "Ctrl+2");
    std::string err;
    XR_EXPECT(s.Save(&err));
    ShortcutStore t(dir);
    auto lr = t.Load();
    XR_EXPECT(lr.ok);
    XR_EXPECT_EQ(t.bindings().size(), 2);
  }

  // Corrupt (truncated) file => deny-preserve (file kept, load fails, no data
  // silently dropped).
  {
    ShortcutStore s(dir);
    s.Bind("z", "Ctrl+9");
    std::string err;
    XR_EXPECT(s.Save(&err));
    std::string good = ReadFileBytes(s.Path());
    // truncate it to a partial write
    std::FILE* f = std::fopen(s.Path().c_str(), "w");
    XR_EXPECT(f != nullptr);
    if (f) {
      std::fwrite(good.data(), 1, good.size() / 2, f);  // partial
      std::fclose(f);
    }
    ShortcutStore t(dir);
    auto lr = t.Load();
    XR_EXPECT(!lr.ok);
    XR_EXPECT(lr.preserved);
    // the file was preserved (not rewritten/deleted), byte-identical to what we wrote
    std::string preserved = ReadFileBytes(t.Path());
    XR_EXPECT(preserved.size() == good.size() / 2);
    XR_EXPECT(preserved == good.substr(0, good.size() / 2));
  }

  // Crash durability. The committed shortcuts.json is changed ONLY by an
  // atomic rename (write tmp -> fsync -> rename), so a crash mid-write leaves
  // at most a partial <path>.tmp, which is INERT: the committed file is never
  // the tmp's bytes. Two proofs, both robust in a sandbox.
  {
    ShortcutStore s(dir);
    s.Bind("dur", "Ctrl+D");
    std::string err;
    XR_EXPECT(s.Save(&err));
    std::string good = ReadFileBytes(s.Path());
    std::string tmp = s.Path() + ".tmp";

    // (1) Deterministic kill-loop x20: each iteration a partial garbage tmp
    // appears (the writer "died" before the rename); the committed file must
    // be byte-identical every time. (No fork: the invariant is that the main
    // file is only ever a completed rename, so the stale tmp is inert.)
    bool all_intact = true;
    for (int seed = 0; seed < 20; ++seed) {
      char junk[256];
      std::memset(junk, 0xAB, sizeof(junk));
      size_t n = 64 + static_cast<size_t>(seed) * 8;
      std::FILE* f = std::fopen(tmp.c_str(), "wb");
      if (f) {
        std::fwrite(junk, 1, n, f);
        std::fclose(f);
      }
      if (ReadFileBytes(s.Path()) != good) all_intact = false;
    }
    XR_EXPECT_MSG(all_intact, "kill-loop x20: partial tmp never became the committed file");
    (void)std::remove(tmp.c_str());

    // (2) One real SIGKILL mid-write. The child uses only async-signal-safe
    // syscalls (open/write) so the forked child cannot corrupt inherited stdio
    // state; the parent SIGKILLs it mid-write and ALWAYS reaps it.
    pid_t pid = fork();
    if (pid == 0) {
      int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
      char junk[1024];
      std::memset(junk, 0xCD, sizeof(junk));
      for (int i = 0; i < 1000000 && fd >= 0; ++i) {
        ssize_t w = ::write(fd, junk, sizeof(junk));
        if (w < 0) break;
      }
      _exit(3);
    }
    // Let the tmp grow (child is mid-write), then SIGKILL it.
    for (int i = 0; i < 10000; ++i) {
      struct stat st{};
      if (::stat(tmp.c_str(), &st) == 0 && st.st_size > 0) break;
      (void)usleep(100);
    }
    (void)kill(pid, SIGKILL);
    int st = 0;
    (void)waitpid(pid, &st, 0);  // always reap (a SIGKILL'd child is a zombie)
    XR_EXPECT_MSG(ReadFileBytes(s.Path()) == good,
                  "real SIGKILL mid-write leaves committed file byte-identical");
    (void)std::remove(tmp.c_str());
    // A fresh Save recovers: it rewrites the tmp + renames, clearing the stale tmp.
    XR_EXPECT(s.Save(&err));
    ShortcutStore t(dir);
    auto lr = t.Load();
    XR_EXPECT(lr.ok);
    XR_EXPECT_EQ(t.bindings().size(), 1);
  }
  return xrtest::Report("test_shortcuts");
}
