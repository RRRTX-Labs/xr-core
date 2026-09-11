// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
#include "update/core/seen.h"

#include <cstdio>
#include <filesystem>
#include <unistd.h>  // fsync (POSIX; the P6/P7/P8 store law)

namespace xr::update {
namespace {

constexpr const char* kFileName = "update-seen.json";
constexpr const char* kSchema = "xr-update-seen";

std::string ReadAll(const std::string& path, bool* exists) {
  *exists = false;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return {};
  std::string data;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  std::fclose(f);
  *exists = true;
  return data;
}

// The P6 store law, verbatim: write-tmp -> fflush -> fsync -> rename.
bool AtomicWrite(const std::string& path, const std::string& bytes,
                 std::string* error) {
  const std::string tmp = path + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) {
    if (error) *error = "cannot open temp file " + tmp;
    return false;
  }
  const bool wrote = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  const bool flushed = std::fflush(f) == 0;
  const int fd = fileno(f);
  const bool synced = fd >= 0 ? ::fsync(fd) == 0 : false;
  std::fclose(f);
  if (!wrote || !flushed || !synced) {
    if (error) *error = "tmp write/fsync failed for " + tmp;
    std::remove(tmp.c_str());
    return false;
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    if (error) *error = "rename failed for " + path;
    return false;
  }
  return true;
}

}  // namespace

std::string SeenSet::PathFor(const std::string& store_dir) {
  return store_dir.empty() ? std::string() : store_dir + "/" + kFileName;
}

SeenSet::SeenSet(std::string store_dir) : store_dir_(std::move(store_dir)) {}

bool SeenSet::Contains(const std::string& manifest_id) const {
  return ids_.count(manifest_id) != 0;
}

bool SeenSet::Insert(const std::string& manifest_id, std::string* error) {
  if (manifest_id.empty()) {
    if (error) *error = "empty manifest id";
    return false;
  }
  if (ids_.count(manifest_id)) return true;
  ids_.insert(manifest_id);
  order_.push_back(manifest_id);
  while (order_.size() > kMaxEntries) {
    ids_.erase(order_.front());
    order_.pop_front();
  }
  if (store_dir_.empty()) return true;  // disposable: memory only, zero bytes
  JsonValue::Array arr;
  for (const std::string& id : order_) arr.push_back(JsonValue(id));
  JsonValue doc(JsonValue::Object{
      {"ids", JsonValue(std::move(arr))},
      {"schema", JsonValue(kSchema)},
      {"schema_version", JsonValue(static_cast<int64_t>(1))},
  });
  return AtomicWrite(PathFor(store_dir_), doc.Canonical() + "\n", error);
}

bool SeenSet::Load(std::string* error) {
  const std::string path = PathFor(store_dir_);
  if (path.empty()) return true;  // disposable
  bool exists = false;
  const std::string bytes = ReadAll(path, &exists);
  if (!exists) return true;  // fresh store
  JsonParseResult pr = ParseJson(bytes);
  if (!pr.ok || !pr.value.is_object()) {
    if (error) *error = kFileName + std::string(" is corrupt — preserved as-is");
    return false;
  }
  const JsonValue* schema = pr.value.find("schema");
  const JsonValue* ids = pr.value.find("ids");
  if (!schema || !schema->is_string() || schema->as_string() != kSchema || !ids ||
      !ids->is_array()) {
    if (error) *error = kFileName + std::string(" has an unrecognized schema — preserved");
    return false;
  }
  for (const JsonValue& id : ids->as_array()) {
    if (!id.is_string() || id.as_string().empty()) {
      if (error) *error = kFileName + std::string(" has a malformed id — preserved");
      return false;
    }
    ids_.insert(id.as_string());
    order_.push_back(id.as_string());
  }
  return true;
}

}  // namespace xr::update
