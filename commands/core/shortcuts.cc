// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Shortcut store implementation. See shortcuts.h for the conflict + durability
// law. POSIX file APIs only (no external libs); rename(2) atomicity is the
// crash-durability guarantee.
#include "commands/core/shortcuts.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unistd.h>

#include "commands/core/json.h"

namespace xr::commands {
namespace {

std::string ReadAll(const std::string& path, bool* exists) {
  *exists = false;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return {};
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  std::fclose(f);
  *exists = true;
  return data;
}

}  // namespace

const char* ToString(ConflictKind k) {
  switch (k) {
    case ConflictKind::kDuplicate: return "duplicate";
    case ConflictKind::kBrowserReserved: return "browser-reserved";
    case ConflictKind::kSystemReserved: return "system-reserved";
    case ConflictKind::kNone:
    default:
      return "none";
  }
}

const std::vector<std::string>& ShortcutStore::BrowserReserved() {
  // Conservative deny-by-default: browser-owned accelerators a command must
  // not steal (mirrors the global_shortcut reservation model). Data, not code.
  static const std::vector<std::string> kRes = {"F11", "CTRL+SHIFT+I", "F12"};
  return kRes;
}
const std::vector<std::string>& ShortcutStore::SystemReserved() {
  // OS-level reserved combos (conservative; denied by default).
  static const std::vector<std::string> kRes = {"ALT+F4", "CTRL+ESC"};
  return kRes;
}

std::string ShortcutStore::Normalize(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    if (c == ' ' || c == '\t') continue;
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return out;
}

ShortcutStore::LoadResult ShortcutStore::Load() {
  LoadResult r;
  if (dir_.empty()) {
    r.ok = true;  // ephemeral: in-memory only
    return r;
  }
  bool exists = false;
  std::string bytes = ReadAll(Path(), &exists);
  if (!exists || bytes.empty()) {
    if (exists && bytes.empty()) {
      // A zero-byte file is a truncated write: deny-preserve.
      r.ok = false;
      r.preserved = true;
      r.error = "shortcuts.json is empty (truncated write?) — preserved as-is";
      return r;
    }
    r.ok = true;  // absent => empty store
    return r;
  }
  auto p = ParseJson(bytes);
  if (!p.ok) {
    r.ok = false;
    r.preserved = true;
    r.error = "shortcuts.json is not strict JSON: " + p.error + " — preserved";
    return r;
  }
  const JsonValue& d = p.value;
  const JsonValue* schema = d.find("schema");
  const JsonValue* sv = d.find("schema_version");
  if (!schema || !schema->is_string() || schema->as_string() != "xr-shortcuts" ||
      !sv || !sv->is_int() || sv->as_int() != 1) {
    r.ok = false;
    r.preserved = true;
    r.error = "shortcuts.json has an unrecognized schema — preserved";
    return r;
  }
  const JsonValue* arr = d.find("bindings");
  if (!arr || !arr->is_array()) {
    r.ok = false;
    r.preserved = true;
    r.error = "shortcuts.json missing bindings[] — preserved";
    return r;
  }
  for (const auto& b : arr->as_array()) {
    const JsonValue* cid = b.find("command_id");
    const JsonValue* acc = b.find("accelerator");
    if (!cid || !cid->is_string() || !acc || !acc->is_string()) continue;  // drop bad row
    bindings_.push_back(Binding{cid->as_string(), Normalize(acc->as_string())});
  }
  r.ok = true;
  return r;
}

BindResult ShortcutStore::Bind(const std::string& command_id,
                               const std::string& accelerator) {
  BindResult r;
  std::string acc = Normalize(accelerator);
  if (acc.empty()) {
    r.error = "empty accelerator";
    return r;
  }
  if (std::find(SystemReserved().begin(), SystemReserved().end(), acc) !=
      SystemReserved().end()) {
    r.conflict = ConflictKind::kSystemReserved;
    r.error = "accelerator " + acc + " is system-reserved (deny-by-default)";
    return r;
  }
  if (std::find(BrowserReserved().begin(), BrowserReserved().end(), acc) !=
      BrowserReserved().end()) {
    r.conflict = ConflictKind::kBrowserReserved;
    r.error = "accelerator " + acc + " is reserved by the browser (deny-by-default)";
    return r;
  }
  for (const auto& b : bindings_) {
    if (b.accelerator == acc && b.command_id != command_id) {
      r.conflict = ConflictKind::kDuplicate;
      r.conflicting_command = b.command_id;
      r.error = "accelerator " + acc + " is already bound to " + b.command_id;
      return r;
    }
  }
  // (Re)bind: drop any existing binding for this command, then add.
  bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                 [&](const Binding& b) {
                                   return b.command_id == command_id;
                                 }),
                  bindings_.end());
  bindings_.push_back(Binding{command_id, acc});
  r.ok = true;
  r.conflict = ConflictKind::kNone;
  return r;
}

bool ShortcutStore::Unbind(const std::string& command_id, std::string* error) {
  auto it = std::remove_if(bindings_.begin(), bindings_.end(),
                           [&](const Binding& b) {
                             return b.command_id == command_id;
                           });
  if (it == bindings_.end()) {
    if (error) *error = "no binding for " + command_id;
    return false;
  }
  bindings_.erase(it, bindings_.end());
  return true;
}

bool ShortcutStore::Save(std::string* error) const {
  if (dir_.empty()) return true;  // ephemeral: in-memory only, nothing to persist
  std::string target = Path();
  std::string tmp = target + ".tmp";
  JsonValue::Array arr;
  for (const auto& b : bindings_) {
    JsonValue::Object o = {{"accelerator", JsonValue(b.accelerator)},
                           {"command_id", JsonValue(b.command_id)}};
    arr.push_back(JsonValue(o));
  }
  JsonValue::Object doc = {
      {"schema", JsonValue("xr-shortcuts")},
      {"schema_version", JsonValue(static_cast<int64_t>(1))},
      {"bindings", JsonValue(arr)},
  };
  std::string bytes = JsonValue(doc).Canonical() + "\n";

  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    if (error) *error = "cannot open tmp file " + tmp;
    return false;
  }
  size_t w = std::fwrite(bytes.data(), 1, bytes.size(), f);
  int rc = fsync(fileno(f));
  bool closed = std::fclose(f) == 0;
  if (w != bytes.size() || rc != 0 || !closed) {
    if (error) *error = "tmp write/fsync failed";
    (void)std::remove(tmp.c_str());
    return false;
  }
  if (std::rename(tmp.c_str(), target.c_str()) != 0) {
    if (error) *error = "atomic rename failed";
    return false;
  }
  return true;
}

}  // namespace xr::commands
