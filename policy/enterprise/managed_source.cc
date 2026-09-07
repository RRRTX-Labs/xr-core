// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — managed source implementation (see managed_source.h).
#include "policy/enterprise/managed_source.h"

#include <cstdio>
#include <cstdlib>

#include "policy/core/json.h"

namespace xr::policy {

namespace {

constexpr const char* kManagedSchema = "xr-managed-policy";
constexpr int kManagedSchemaVersion = 1;

bool ReadFileToString(const std::string& path, std::string* out) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
  std::fclose(f);
  return true;
}

// Runs `minisign -Vm <doc> -x <sig> -p <pub>`; paths containing shell
// metacharacters are rejected up front (fail closed to skip). Tool
// availability is checked EXPLICITLY (a missing binary yields shell 127,
// which must never be conflated with a bad signature).
int RunMinisignVerify(const std::string& doc, const std::string& sig, const std::string& pub) {
  for (const std::string* s : {&doc, &sig, &pub}) {
    for (char c : *s) {
      if (c == '\'' || c == '"' || c == ';' || c == '|' || c == '&' || c == '$' ||
          c == '`' || c == '\\' || c == '\n' || c == ' ') {
        return -1;  // unsafe path => cannot verify => ignore
      }
    }
  }
  if (std::system("command -v minisign >/dev/null 2>&1") != 0) return 127;  // tool absent
  std::string cmd = "minisign -Vm '" + doc + "' -x '" + sig + "' -p '" + pub + "' 2>/dev/null";
  return std::system(cmd.c_str());
}

}  // namespace

const char* ToString(ManagedStatus s) {
  switch (s) {
    case ManagedStatus::kAbsent: return "kAbsent";
    case ManagedStatus::kVerified: return "kVerified";
    case ManagedStatus::kIgnoredUnsigned: return "kIgnoredUnsigned";
    case ManagedStatus::kIgnoredBadSignature: return "kIgnoredBadSignature";
    case ManagedStatus::kIgnoredInvalid: return "kIgnoredInvalid";
    case ManagedStatus::kIgnoredFutureVersion: return "kIgnoredFutureVersion";
    case ManagedStatus::kSkippedNoTool: return "kSkippedNoTool";
  }
  return "kAbsent";
}

bool ValidateManagedEnvelope(const JsonValue& v, EnterprisePolicy* out, std::string* error) {
  if (!v.is_object()) { *error = "managed doc: root not an object"; return false; }
  if (v.as_object().size() != 4) {
    *error = "managed doc: envelope must have exactly schema/schema_version/created_at/data";
    return false;
  }
  const JsonValue* schema = v.find("schema");
  if (schema == nullptr || !schema->is_string() || schema->as_string() != kManagedSchema) {
    *error = "managed doc: unknown schema";
    return false;
  }
  const JsonValue* sv = v.find("schema_version");
  if (sv == nullptr || !sv->is_int()) {
    *error = "managed doc: schema_version must be an integer";
    return false;
  }
  if (sv->as_int() > kManagedSchemaVersion) {
    *error = "managed doc: future schema_version (never guess)";
    return false;
  }
  if (sv->as_int() < kManagedSchemaVersion) {
    *error = "managed doc: obsolete schema_version";
    return false;
  }
  const JsonValue* ca = v.find("created_at");
  if (ca == nullptr || !ca->is_int()) {
    *error = "managed doc: created_at must be an integer";
    return false;
  }
  const JsonValue* data = v.find("data");
  if (data == nullptr || !data->is_object()) {
    *error = "managed doc: data must be an object";
    return false;
  }
  EnterprisePolicy e;
  if (!ParseEnterpriseFields(*data, &e)) {
    *error = "managed doc: invalid enterprise fields";
    return false;
  }
  *out = e;
  return true;
}

ManagedResult LoadManagedPolicy(const std::string& doc_path, const std::string& pubpath) {
  ManagedResult r;
  std::string raw;
  if (!ReadFileToString(doc_path, &raw)) {
    r.status = ManagedStatus::kAbsent;
    r.detail = "no managed doc at " + doc_path;
    return r;
  }
  auto ledger = [&r](const std::string& row) { r.ledger_rows.push_back(row); };

  auto parsed = ParseJson(raw);
  if (!parsed.ok) {
    r.status = ManagedStatus::kIgnoredInvalid;
    r.detail = "JSON parse: " + parsed.error;
    ledger("managed-policy: ignored (invalid JSON) path=" + doc_path);
    return r;
  }
  EnterprisePolicy e;
  std::string err;
  if (!ValidateManagedEnvelope(parsed.value, &e, &err)) {
    // Distinguish future-version for the ledger row.
    const JsonValue* sv = parsed.value.is_object() ? parsed.value.find("schema_version") : nullptr;
    if (parsed.value.is_object() && parsed.value.find("schema") != nullptr &&
        parsed.value.find("schema")->is_string() &&
        parsed.value.find("schema")->as_string() == kManagedSchema && sv != nullptr &&
        sv->is_int() && sv->as_int() > kManagedSchemaVersion) {
      r.status = ManagedStatus::kIgnoredFutureVersion;
      r.detail = err;
      ledger("managed-policy: ignored (future schema_version) path=" + doc_path);
      return r;
    }
    r.status = ManagedStatus::kIgnoredInvalid;
    r.detail = err;
    ledger("managed-policy: ignored (invalid envelope) path=" + doc_path + " reason=" + err);
    return r;
  }

  // Signature gate: signed-or-ignored, fail closed on any doubt.
  const std::string sig_path = doc_path + ".minisign";
  std::string sig;
  if (!ReadFileToString(sig_path, &sig)) {
    r.status = ManagedStatus::kIgnoredUnsigned;
    r.detail = "detached signature missing: " + sig_path;
    ledger("managed-policy: IGNORED (unsigned) path=" + doc_path);
    return r;
  }
  int rc = RunMinisignVerify(doc_path, sig_path, pubpath);
  if (rc == -1 || rc == 127) {
    r.status = ManagedStatus::kSkippedNoTool;
    r.detail = "verification not safely possible (tool absent or unsafe path)";
    ledger("managed-policy: SKIP-visibility (minisign absent/unusable) => IGNORED path=" + doc_path);
    return r;
  }
  if (rc != 0) {
    r.status = ManagedStatus::kIgnoredBadSignature;
    r.detail = "minisign rejected the signature";
    ledger("managed-policy: ignored (bad signature) path=" + doc_path);
    return r;
  }
  r.status = ManagedStatus::kVerified;
  r.enforced = true;
  r.policy = e;
  ledger("managed-policy: verified+applied path=" + doc_path);
  return r;
}

}  // namespace xr::policy
