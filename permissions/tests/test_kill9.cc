// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P15-CLOSE C2 — the REAL kill -9 drill for the clock-owning expiry sweep
// (P15 T3; modelled on identity/tests/test_session_chaos.cc, wired through the
// tools/kill_matrix.py conventions: seeded delays drawn against the measured
// runtime so kills land mid-flight, and both mid and post outcomes counted).
//
// This test forks the BUILT permissions_host (not ops.cc in-process) against a
// store large enough that a sweep takes measurable time, SIGKILLs it at a
// seeded delay, then asserts through the process boundary:
//   1. no partial state: the store file is byte-equal to the pre-sweep
//      generation or to the post-sweep generation, never anything else, and it
//      loads non-corrupt;
//   2. no live grant survives past its expiry: the next host start (same
//      clock) completes the sweep and leaves ZERO grants that are past expiry,
//      spent, or bound to an ended session, and its projection carries none;
//   3. a rolled-back wall clock cannot resurrect anything: after a landed sweep
//      the persisted high-water mark wins (EffectiveNow), so a host started
//      with an earlier clock still sweeps at the later instant.
// At least one kill must land mid-run, or the drill certifies nothing.
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "permissions/core/ops.h"
#include "permissions/core/store.h"
#include "policy/tests/harness.h"

#ifndef XR_PERMISSIONS_HOST_PATH
#error "XR_PERMISSIONS_HOST_PATH must name the built permissions_host"
#endif

using namespace xr::permissions;

namespace {

constexpr int64_t kT = 1790000000000LL;      // the sweep instant (--as-of)
constexpr const char* kLive = "s-live";      // the active session
constexpr int kGrants = 8000;

Store BigStore() {
  Store s;
  for (int i = 0; i < kGrants; ++i) {
    StoredGrant g;
    g.id = "g" + std::to_string(i + 1);
    g.identity = "xr:id-" + std::to_string(i % 37);
    g.domain = "site" + std::to_string(i % 101) + ".example";
    g.capability = static_cast<Capability>(i % 4);
    switch (i % 3) {
      case 0:  // 7d: about half already past expiry at kT (== counts as expired)
        g.scope = Scope::k7d;
        g.created_at = kT - kSevenDaysMillis + (i % 2 == 0 ? 0 : 3600000);
        g.expires_at = g.created_at + kSevenDaysMillis;
        break;
      case 1:  // session: half bound to an ended session
        g.scope = Scope::kSession;
        g.session_id = (i % 2 == 0) ? "s-old" : kLive;
        break;
      default:  // once: some spent tombstones
        g.scope = Scope::kOnce;
        g.remaining_uses = (i % 5 == 0) ? 0 : 1;
        break;
    }
    s.identities[g.identity].grants.push_back(g);
  }
  s.next_seq = kGrants + 1;
  return s;
}

std::string Slurp(const std::string& path) {
  std::string out;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return out;
  char buf[8192];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

void Spit(const std::string& path, const std::string& bytes) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return;
  std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
}

pid_t Spawn(const std::string& dir, int64_t as_of, const char* method, const char* args) {
  pid_t pid = fork();
  if (pid == 0) {
    if (std::freopen("/dev/null", "w", stdout) == nullptr) _exit(126);
    const std::string asof = std::to_string(as_of);
    execl(XR_PERMISSIONS_HOST_PATH, XR_PERMISSIONS_HOST_PATH, "--store-dir", dir.c_str(),
          "--as-of", asof.c_str(), method, args, static_cast<char*>(nullptr));
    _exit(127);
  }
  return pid;
}

int RunToEnd(const std::string& dir, int64_t as_of, const char* method, const char* args) {
  int status = 0;
  waitpid(Spawn(dir, as_of, method, args), &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Grants that must not be live at `now` under the active session.
int PastExpiry(const Store& s, int64_t now) {
  int n = 0;
  for (const auto& [key, rec] : s.identities) {
    (void)key;
    for (const auto& g : rec.grants) {
      if (g.revoked) continue;
      if ((g.scope == Scope::k7d && g.expires_at <= now) ||
          (g.scope == Scope::kSession && g.session_id != kLive) ||
          (g.scope == Scope::kOnce && g.remaining_uses <= 0)) {
        ++n;
      }
    }
  }
  return n;
}

}  // namespace

int main() {
  const Store pre = BigStore();
  const std::string pre_bytes = SerializeStore(pre);
  const std::string post_bytes = SerializeStore(SweepExpired(pre, kT, kLive).store);
  XR_EXPECT_MSG(PastExpiry(pre, kT) > 0, "the fixture must hold grants past expiry");
  XR_EXPECT_MSG(pre_bytes != post_bytes, "the sweep must have work to do");

  const char* tmpdir = std::getenv("TMPDIR");
  std::string tmpl_s = std::string(tmpdir != nullptr && *tmpdir ? tmpdir : "/tmp") +
                       "/xr-perm-kill9-XXXXXX";
  std::vector<char> tmpl(tmpl_s.begin(), tmpl_s.end());
  tmpl.push_back('\0');
  const char* root_c = mkdtemp(tmpl.data());
  XR_EXPECT_MSG(root_c != nullptr, "mkdtemp");
  if (root_c == nullptr) return xrtest::Report("test_kill9");
  const std::string root = root_c;
  const char* kSweepArgs = "{\"session_id\":\"s-live\"}";

  // Measure one full sweep so kill delays straddle the real runtime.
  const std::string probe = root + "/probe";
  mkdir(probe.c_str(), 0700);
  Spit(probe + "/permissions-store.json", pre_bytes);
  const auto t0 = std::chrono::steady_clock::now();
  XR_EXPECT_EQ(RunToEnd(probe, kT, "sweep", kSweepArgs), 0);
  const double rt_us = std::chrono::duration<double, std::micro>(
                           std::chrono::steady_clock::now() - t0).count();
  XR_EXPECT_MSG(Slurp(probe + "/permissions-store.json") == post_bytes,
                "an uninterrupted host sweep equals the core's sweep");

  std::mt19937 rng(15151);  // seeded: the drill is reproducible
  std::uniform_real_distribution<double> frac(0.05, 1.25);
  int mid = 0, post = 0;
  const int iterations = 30;
  for (int i = 0; i < iterations; ++i) {
    const std::string dir = root + "/cell" + std::to_string(i);
    mkdir(dir.c_str(), 0700);
    Spit(dir + "/permissions-store.json", pre_bytes);
    const pid_t pid = Spawn(dir, kT, "sweep", kSweepArgs);
    std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(rt_us * frac(rng))));
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL) ++mid; else ++post;

    // 1. whole generation only
    const std::string after = Slurp(dir + "/permissions-store.json");
    XR_EXPECT_MSG(after == pre_bytes || after == post_bytes,
                  "kill -9 left a store that is neither the pre- nor the post-sweep generation");
    XR_EXPECT_MSG(!LoadStore(after).corrupt, "kill -9 left a store that does not load");

    // 2. restart at the same clock: nothing past expiry survives
    XR_EXPECT_EQ(RunToEnd(dir, kT, "sweep", kSweepArgs), 0);
    const Store healed = LoadStore(Slurp(dir + "/permissions-store.json"));
    XR_EXPECT_MSG(!healed.corrupt, "the restarted sweep wrote a loadable store");
    XR_EXPECT_EQ(PastExpiry(healed, kT), 0);
    XR_EXPECT_MSG(SerializeStore(healed) == post_bytes, "restart converges to the post generation");

    // 3. clock rolled back by three days: the high-water mark still decides
    const std::string clock = Slurp(dir + "/permissions-clock.json");
    XR_EXPECT_MSG(clock == "{\"last_seen_ms\":" + std::to_string(kT) + "}",
                  "the landed sweep persisted its clock high-water mark");
    XR_EXPECT_EQ(RunToEnd(dir, kT - 3 * kMillisPerDay, "sweep", kSweepArgs), 0);
    XR_EXPECT_EQ(PastExpiry(LoadStore(Slurp(dir + "/permissions-store.json")), kT), 0);
  }
  std::printf("kill9: %d iteration(s), %d killed mid-run, %d after exit (runtime %.0f us)\n",
              iterations, mid, post, rt_us);
  XR_EXPECT_MSG(mid > 0, "no kill landed mid-run: the drill would certify nothing");

  std::string rm = "rm -rf '" + root + "'";
  XR_EXPECT_EQ(std::system(rm.c_str()), 0);
  return xrtest::Report("test_kill9");
}
