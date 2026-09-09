// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Attention-Budget ledger implementation (see counters.h). POSIX file APIs
// only; rename(2) atomicity is the kill-durability guarantee. Rolling
// retention: on Save, days older than kRetentionDays are pruned.
#include "settings/core/counters.h"

#include <cstdio>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace xr::settings {
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

// day is guaranteed to be an object (created empty when absent).
JsonValue& DayRef(std::map<std::string, JsonValue>* days,
                  const std::string& day) {
  auto it = days->find(day);
  if (it == days->end()) {
    (*days)[day] = JsonValue(JsonValue::Object{});
    return days->at(day);
  }
  if (!it->second.is_object()) {
    it->second = JsonValue(JsonValue::Object{});
  }
  return it->second;
}

void IncInt(JsonValue* day, const std::string& key) {
  JsonValue::Object obj = day->as_object();
  auto it = obj.find(key);
  int64_t n = (it == obj.end() || !it->second.is_int()) ? 0 : it->second.as_int();
  obj[key] = JsonValue(n + 1);
  *day = JsonValue(std::move(obj));
}

void IncMapKey(JsonValue* day, const std::string& key, const std::string& sub) {
  JsonValue::Object obj = day->as_object();
  auto it = obj.find(key);
  JsonValue::Object map;
  if (it != obj.end() && it->second.is_object()) map = it->second.as_object();
  auto sit = map.find(sub);
  int64_t n = (sit == map.end() || !sit->second.is_int()) ? 0 : sit->second.as_int();
  map[sub] = JsonValue(n + 1);
  obj[key] = JsonValue(std::move(map));
  *day = JsonValue(std::move(obj));
}

}  // namespace

std::string CounterStore::UtcToday() {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&now, &tm);  // UTC (day granularity is the coarsest clock stored)
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                static_cast<int>(tm.tm_year) + 1900,
                static_cast<int>(tm.tm_mon) + 1,
                static_cast<int>(tm.tm_mday));
  return buf;
}

CounterStore::LoadResult CounterStore::Load() {
  LoadResult r;
  days_.clear();
  loaded_ = true;
  load_failed_ = false;
  if (dir_.empty()) return r;  // disposable: in-memory, zero bytes on disk
  bool exists = false;
  const std::string text = ReadAll(Path(), &exists);
  if (!exists) return r;
  JsonParseResult p = ParseJson(text);
  if (!p.ok || !p.value.is_object()) {
    r.preserved = true;
    load_failed_ = true;
    r.error = "ledger corrupt — kept as-is, nothing rewritten (deny-preserve)";
    return r;
  }
  const JsonValue& doc = p.value;
  const JsonValue* schema = doc.find("schema");
  const JsonValue* sv = doc.find("schema_version");
  if (schema == nullptr || !schema->is_string() || schema->as_string() != kSchema ||
      sv == nullptr || !sv->is_int() || sv->as_int() != kVersion) {
    r.preserved = true;
    load_failed_ = true;
    r.error = "ledger schema mismatch — kept as-is (downgrade no-op law)";
    return r;
  }
  const JsonValue* days = doc.find("days");
  if (days != nullptr && days->is_object()) {
    for (const auto& [day, row] : days->as_object()) {
      if (row.is_object()) days_[day] = row;
    }
  }
  r.ok = true;
  return r;
}

void CounterStore::OpenSection(const std::string& section) {
  if (section.empty() || !loaded_) return;
  JsonValue& day = DayRef(&days_, UtcToday());
  IncMapKey(&day, "opened", section);
}

void CounterStore::AcceptQuery() {
  if (!loaded_) return;
  JsonValue& day = DayRef(&days_, UtcToday());
  IncInt(&day, "queries");
}

void CounterStore::SettingChanged(const std::string& key) {
  if (key.empty() || !loaded_) return;
  JsonValue& day = DayRef(&days_, UtcToday());
  IncMapKey(&day, "changed", key);
}

bool CounterStore::Save(std::string* error) const {
  if (dir_.empty()) return true;  // disposable: nothing is ever written
  if (load_failed_) {
    // deny-preserve: a ledger we could not read is never silently rewritten
    std::error_code ec;
    if (std::filesystem::exists(Path(), ec) && !ec) {
      if (error)
        *error =
            "on-disk ledger failed to load and is preserved — refusing to "
            "overwrite (deny-preserve)";
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec) {
    if (error) *error = "cannot create store dir: " + ec.message();
    return false;
  }
  // Rolling retention enforced at persist time (C++20 chrono date math).
  const std::string today = UtcToday();
  int y = 0, m = 0, d = 0;
  if (std::sscanf(today.c_str(), "%04d-%02d-%02d", &y, &m, &d) != 3) {
    if (error) *error = "cannot compute retention cutoff";
    return false;
  }
  using namespace std::chrono;
  const sys_days today_d = sys_days{year(y) / month(m) / day(d)};
  const sys_days cutoff = today_d - days(kRetentionDays);
  const year_month_day cut(cutoff);
  char cutbuf[64];
  std::snprintf(cutbuf, sizeof(cutbuf), "%04d-%02d-%02d",
                static_cast<int>(cut.year()), static_cast<unsigned>(cut.month()),
                static_cast<unsigned>(cut.day()));
  JsonValue::Object days;
  for (const auto& [day_str, row] : days_) {
    if (day_str >= cutbuf) days[day_str] = row;  // older: dropped
  }
  JsonValue::Object doc;
  doc["schema"] = JsonValue(std::string(kSchema));
  doc["schema_version"] = JsonValue(kVersion);
  doc["days"] = JsonValue(std::move(days));
  const std::string json = JsonValue(std::move(doc)).Canonical() + "\n";
  const std::string tmp = Path() + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    if (error) *error = "cannot open tmp ledger for write";
    return false;
  }
  const bool write_ok =
      std::fwrite(json.data(), 1, json.size(), f) == json.size();
  const bool sync_ok = std::fflush(f) == 0 && fsync(fileno(f)) == 0;
  std::fclose(f);
  if (!write_ok || !sync_ok) {
    std::remove(tmp.c_str());
    if (error) *error = "write/fsync failed";
    return false;
  }
  if (std::rename(tmp.c_str(), Path().c_str()) != 0) {
    std::remove(tmp.c_str());
    if (error) *error = "rename failed";
    return false;
  }
  return true;
}

JsonValue CounterStore::Dump() const {
  JsonValue::Object days;
  for (const auto& [day, row] : days_) days[day] = row;
  JsonValue::Object doc;
  doc["schema"] = JsonValue(std::string(kSchema));
  doc["schema_version"] = JsonValue(kVersion);
  doc["days"] = JsonValue(std::move(days));
  doc["in_memory"] = JsonValue(dir_.empty());
  if (load_failed_) doc["load_error"] = JsonValue(std::string("preserved"));
  return JsonValue(std::move(doc));
}

size_t CounterStore::TotalQueries() const {
  size_t total = 0;
  for (const auto& [day, row] : days_) {
    const JsonValue* q = row.find("queries");
    if (q != nullptr && q->is_int()) total += static_cast<size_t>(q->as_int());
  }
  return total;
}

}  // namespace xr::settings
