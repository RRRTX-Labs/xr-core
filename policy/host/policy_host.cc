// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — policy_host: the JSON-over-stdio façade for the C++ policy
// core. Speaks the SAME protocol as the Python fakes (fakes/README.md:
// one JSON request on argv[1] or stdin, one canonical JSON result on
// stdout, exit 0 always for resolve — the typed result carries the
// outcome), so xrctl --backend fake|cpp drives either interchangeably and
// the P5 parity harness extends unchanged. Subcommands:
//   policy_host [resolve] '<json>'   — resolve (default; fake-compatible)
//   policy_host dump [--managed] [--json] [--store-dir DIR]
//   policy_host watch [--store-dir DIR]  — generation counters (poll mode;
//                                          long-running IPC watch = P11+)
//   policy_host snapshot [--diff] [--store-dir DIR] [--verify '<blob>']
// Exit: 0 ok/typed-result · 1 error · 2 usage. No exceptions cross this
// boundary; results are typed.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "policy/core/events.h"
#include "policy/core/service.h"

namespace {

std::string ReadAllStdin() {
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) data.append(buf, n);
  return data;
}

int Usage() {
  std::fprintf(stderr,
               "usage: policy_host [resolve] '<json-request>'\n"
               "       policy_host dump [--managed] [--json] [--store-dir DIR]\n"
               "       policy_host watch [--store-dir DIR]\n"
               "       policy_host snapshot [--diff] [--verify '<blob>'] [--store-dir DIR]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace xr::policy;
  if (argc < 2) {
    // Fake-protocol parity: no args => one JSON request on stdin.
    PolicyResolverService service;
    std::printf("%s\n", service.ResolveText(ReadAllStdin()).c_str());
    return 0;
  }
  std::string cmd = argv[1];
  std::string store_dir;
  bool include_managed = false;
  bool json_out = false;
  bool diff_mode = false;
  std::string verify_blob;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--store-dir" && i + 1 < argc) store_dir = argv[++i];
    else if (a == "--managed") include_managed = true;
    else if (a == "--json") json_out = true;
    else if (a == "--diff") diff_mode = true;
    else if (a == "--verify" && i + 1 < argc) verify_blob = argv[++i];
    else return Usage();
  }

  PolicyResolverService service(store_dir);

  if (cmd == "resolve" || cmd == "dump" || cmd == "watch" || cmd == "snapshot") {
    // fall through to handlers below
  } else {
    // Fake-compatible default: the argument IS the request JSON.
    std::string req = (cmd == "resolve") ? "" : cmd;
    if (req.empty()) {
      if (argc == 2) req = ReadAllStdin();
      else return Usage();
    }
    std::printf("%s\n", service.ResolveText(req).c_str());
    return 0;
  }

  if (cmd == "resolve") {
    std::string req = (argc >= 3 && argv[2][0] != '-') ? argv[2] : ReadAllStdin();
    if (!verify_blob.empty()) return Usage();
    std::printf("%s\n", service.ResolveText(req).c_str());
    return 0;
  }
  if (cmd == "dump") {
    if (json_out) {
      std::printf("%s\n", service.DumpJson(include_managed).Canonical().c_str());
    } else {
      std::printf("%s", service.Dump(include_managed).c_str());
    }
    return 0;
  }
  if (cmd == "watch") {
    // Poll-mode watch: current generation counters (documented limitation:
    // a blocking multi-process watch arrives with P11 IPC adoption).
    auto s = service.cache().GetStats();
    std::printf("global_generation=%llu invalidations_all=%llu invalidations_identity=%llu\n",
                static_cast<unsigned long long>(s.global_generation),
                static_cast<unsigned long long>(s.invalidations_all),
                static_cast<unsigned long long>(s.invalidations_identity));
    return 0;
  }
  if (cmd == "snapshot") {
    if (!verify_blob.empty()) {
      auto r = DecodeFullSnapshot(verify_blob);
      if (!r.ok) {
        std::printf("{\"error\":\"%s\",\"detail\":\"%s\"}\n", ToString(r.error),
                    r.error_detail.c_str());
        return 1;
      }
      std::printf("{\"ok\":{\"entries\":%lld,\"seq\":%lld}}\n",
                  static_cast<long long>(r.entries.size()),
                  static_cast<long long>(r.seq));
      return 0;
    }
    auto r = diff_mode ? service.SnapshotDiff() : service.SnapshotFull();
    if (!r.ok) {
      std::printf("{\"error\":\"%s\",\"detail\":\"%s\"}\n", ToString(r.error),
                  r.error_detail.c_str());
      return 1;
    }
    std::printf("%s\n", r.blob.c_str());
    return 0;
  }
  return Usage();
}
