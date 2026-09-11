// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0-test — minimal deterministic assert harness (no gtest; zero
// dependencies per the phase DEPENDENCY RULES). Single source of counters
// per binary; deterministic ordering (no parallelism); exit 0 iff all pass.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace xrtest {

inline int g_checks = 0;
inline int g_failures = 0;
inline std::vector<std::string> g_messages;

inline void Record(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    g_messages.push_back(what);
  }
}

inline bool Expect(bool cond, const char* what, const char* file, int line) {
  Record(cond, std::string(file) + ":" + std::to_string(line) + ": FAILED " + what);
  return cond;
}

inline bool Expect(bool cond, const std::string& what, const char* file, int line) {
  Record(cond, std::string(file) + ":" + std::to_string(line) + ": FAILED " + what);
  return cond;
}

inline int Report(const char* suite) {
  for (const auto& m : g_messages) std::fprintf(stderr, "  %s\n", m.c_str());
  std::printf("%s: %d checks, %d failures\n", suite, g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

inline std::string ReadFile(const std::string& path, bool* ok) {
  *ok = false;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return {};
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  std::fclose(f);
  *ok = !data.empty();
  return data;
}

}  // namespace xr::test

#define XR_EXPECT(cond) ::xrtest::Expect((cond), #cond, __FILE__, __LINE__)
#define XR_EXPECT_MSG(cond, msg) ::xrtest::Expect((cond), (msg), __FILE__, __LINE__)
#define XR_EXPECT_EQ(a, b) \
  ::xrtest::Expect((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define XR_EXPECT_STREQ(a, b)                                         \
  ::xrtest::Expect(std::string(a) == std::string(b),                  \
                   std::string(#a " == ") + std::string(b) +          \
                       (std::string(a) == std::string(b)              \
                            ? ""                                      \
                            : " (got: " + std::string(a) + ")"),      \
                   __FILE__, __LINE__)
