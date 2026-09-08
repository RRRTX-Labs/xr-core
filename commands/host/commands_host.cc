// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// commands_host — stdio JSON façade (command-host-protocol v1). See
// host_protocol.md. Forms:
//   commands_host <method> ['<json-args>'] [options]
//   commands_host '<json-with-method>' [options]
//   commands_host [options]            (request JSON on stdin)
// Options: --store-dir DIR  --flag k=v  --roster PATH
// Exit: 0 typed result · 1 error · 2 usage.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "commands/host/protocol.h"

#ifndef XR_DEFAULT_ROSTER
#define XR_DEFAULT_ROSTER "commands/core/roster_v1.json"
#endif

namespace {

using xr::commands::JsonParseResult;
using xr::commands::JsonValue;
using xr::commands::ParseJson;
using xr::commands::host::Context;
using xr::commands::host::HandleMethod;
using xr::commands::host::LoadContext;

std::string ReadAllStdin() {
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) data.append(buf, n);
  return data;
}

bool IsMethod(const std::string& s) {
  static const char* m[] = {"flag-status", "list", "query", "invoke",
                            "bindings-set", "bindings-list", "bindings-clear",
                            "menu-model", "register"};
  for (const char* x : m) if (s == x) return true;
  return false;
}

int Usage() {
  std::fprintf(stderr,
               "usage: commands_host <method> ['<json-args>'] [options]\n"
               "       commands_host '<json-with-method>' [options]\n"
               "       commands_host [options]            # request JSON on stdin\n"
               "methods: flag-status list query invoke bindings-set bindings-list\n"
               "         bindings-clear menu-model register\n"
               "options: --store-dir DIR  --flag k=v  --roster PATH\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string store_dir;
  std::string roster = XR_DEFAULT_ROSTER;
  std::map<std::string, std::string> flags;
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--store-dir" && i + 1 < argc) store_dir = argv[++i];
    else if (a == "--roster" && i + 1 < argc) roster = argv[++i];
    else if (a == "--flag" && i + 1 < argc) {
      std::string kv = argv[++i];
      auto eq = kv.find('=');
      if (eq == std::string::npos) return Usage();
      flags[kv.substr(0, eq)] = kv.substr(eq + 1);
    } else if (a.rfind("--", 0) == 0) {
      return Usage();
    } else {
      positional.push_back(a);
    }
  }
  auto fr = flags.find("xr_command_registry_v1");
  std::string flag_registry = (fr != flags.end()) ? fr->second : std::string("on");

  // Resolve (method, args-JSON-text).
  std::string method;
  std::string args_json = "{}";
  if (positional.empty()) {
    args_json = ReadAllStdin();
    auto p = ParseJson(args_json);
    if (!p.ok) {
      std::printf("{\"error\":\"kMalformedInput\",\"detail\":\"%s\"}\n", p.error.c_str());
      return 1;
    }
    const JsonValue* m = p.value.find("method");
    const JsonValue* a = p.value.find("args");
    method = (m && m->is_string()) ? m->as_string() : "";
    args_json = a ? a->Canonical() : std::string("{}");
  } else if (IsMethod(positional[0])) {
    method = positional[0];
    if (positional.size() >= 2) args_json = positional[1];
  } else {
    // A JSON request with a method field given positionally.
    args_json = positional[0];
    auto p = ParseJson(args_json);
    if (!p.ok) {
      std::printf("{\"error\":\"kMalformedInput\",\"detail\":\"%s\"}\n", p.error.c_str());
      return 1;
    }
    const JsonValue* m = p.value.find("method");
    const JsonValue* a = p.value.find("args");
    method = (m && m->is_string()) ? m->as_string() : "";
    args_json = a ? a->Canonical() : std::string("{}");
  }
  if (method.empty()) return Usage();

  JsonParseResult ap = ParseJson(args_json);
  if (!ap.ok || !ap.value.is_object()) {
    std::printf("{\"error\":\"kMalformedInput\",\"detail\":\"args must be a JSON object\"}\n");
    return 1;
  }

  Context ctx;
  ctx.flag_registry = flag_registry;
  std::string err;
  if (!LoadContext(&ctx, store_dir, roster, &err)) {
    std::printf("{\"error\":\"kStoreError\",\"detail\":\"%s\"}\n", err.c_str());
    return 1;
  }
  std::printf("%s\n", HandleMethod(method, ap.value, ctx).c_str());
  return 0;
}
