// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// P6 policy-state bridge. See policy_state.h. std-only; writes the
// trust-bindings-v1 doc (validated by policy_core in the dial test) and
// serves a pinned, versioned availability snapshot.
#include "commands/core/policy_state.h"

#include <cstdio>
#include <unistd.h>

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

bool AtomicWrite(const std::string& path, const std::string& bytes,
                 std::string* error) {
  std::string tmp = path + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    if (error) *error = "cannot open tmp " + tmp;
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
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    if (error) *error = "atomic rename failed";
    return false;
  }
  return true;
}

bool IsTier(const std::string& t) {
  return t == "kStandard" || t == "kShield" || t == "kFortress";
}

JsonValue DefaultSnapshot() {
  // Pinned golden: dial writable, Tor NOT wired (so tor.open is a disabled
  // placeholder), one active identity. Versioned (pinned-at-version law).
  JsonValue::Object o = {
      {"version", JsonValue(static_cast<int64_t>(1))},
      {"dial_writable", JsonValue(true)},
      {"capabilities", JsonValue(JsonValue::Array{})},
      {"active_identity", JsonValue("xr:main-001")},
  };
  return JsonValue(o);
}

}  // namespace

JsonValue PolicyState::Snapshot() const {
  bool exists = false;
  std::string bytes = ReadAll(SnapshotPath(), &exists);
  if (exists && !bytes.empty()) {
    auto p = ParseJson(bytes);
    if (p.ok && p.value.is_object()) return p.value;  // pinned, versioned
  }
  return DefaultSnapshot();
}

JsonValue::Array PolicyState::LoadBindings() const {
  if (dir_.empty()) return mem_bindings_;  // ephemeral
  bool exists = false;
  std::string bytes = ReadAll(TrustBindingsPath(), &exists);
  if (!exists || bytes.empty()) return {};
  auto p = ParseJson(bytes);
  if (!p.ok || !p.value.is_object()) return {};  // deny-safe: empty
  const JsonValue* d = p.value.find("data");
  if (!d || !d->is_object()) return {};
  const JsonValue* bs = d->find("bindings");
  if (!bs || !bs->is_array()) return {};
  return bs->as_array();
}

bool PolicyState::SaveBindings(const JsonValue::Array& bindings,
                               std::string* error) {
  if (dir_.empty()) {
    mem_bindings_ = bindings;  // ephemeral: in-memory only
    return true;
  }
  JsonValue::Object data = {{"bindings", JsonValue(bindings)}};
  JsonValue::Object o = {
      {"schema", JsonValue("xr-trust-bindings")},
      {"schema_version", JsonValue(static_cast<int64_t>(1))},
      {"created_at", JsonValue(static_cast<int64_t>(0))},
      {"data", JsonValue(data)},
  };
  return AtomicWrite(TrustBindingsPath(), JsonValue(o).Canonical() + "\n", error);
}

std::string PolicyState::CurrentTrust(const std::string& domain,
                                      const std::string& identity) const {
  for (const auto& e : LoadBindings()) {
    const JsonValue* d = e.find("domain");
    const JsonValue* i = e.find("identity");
    const JsonValue* t = e.find("trust");
    if (d && d->is_string() && d->as_string() == domain &&
        ((i == nullptr) || (i->is_string() && i->as_string() == identity)) &&
        t && t->is_string()) {
      return t->as_string();
    }
  }
  return "";
}

std::string PolicyState::SetTrust(const std::string& domain,
                                  const std::string& identity,
                                  const std::string& trust, std::string* error) {
  if (!IsTier(trust)) {
    if (error) *error = "illegal trust tier '" + trust + "'";
    return "";
  }
  std::string prev = CurrentTrust(domain, identity);
  JsonValue::Array out;
  for (const auto& e : LoadBindings()) {
    const JsonValue* d = e.find("domain");
    const JsonValue* i = e.find("identity");
    bool match = d && d->is_string() && d->as_string() == domain &&
                 ((i == nullptr) || (i->is_string() && i->as_string() == identity));
    if (match) continue;  // replace existing
    out.push_back(e);
  }
  JsonValue::Object b = {
      {"domain", JsonValue(domain)},
      {"identity", JsonValue(identity)},
      {"trust", JsonValue(trust)},
  };
  out.push_back(JsonValue(b));
  std::string err;
  if (!SaveBindings(out, &err)) {
    if (error) *error = err;
    return "";
  }
  return prev;
}

std::string PolicyState::RemoveTrust(const std::string& domain,
                                     const std::string& identity,
                                     std::string* error) {
  std::string prev = CurrentTrust(domain, identity);
  JsonValue::Array out;
  for (const auto& e : LoadBindings()) {
    const JsonValue* d = e.find("domain");
    const JsonValue* i = e.find("identity");
    bool match = d && d->is_string() && d->as_string() == domain &&
                 ((i == nullptr) || (i->is_string() && i->as_string() == identity));
    if (!match) out.push_back(e);
  }
  std::string err;
  if (!SaveBindings(out, &err)) {
    if (error) *error = err;
    return "";
  }
  return prev;
}

}  // namespace xr::commands
