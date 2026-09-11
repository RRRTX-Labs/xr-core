// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: strict envelope + frozen-3.1-subset validation (update/manifest.h).
// Deny-on-unknown at every object level; canonical bytes are the signed
// message. std-only.
#include "update/core/manifest.h"

#include "update/core/sha256.h"

namespace xr::update {
namespace {

constexpr size_t kMaxDocBytes = 64 * 1024;

bool AllHex64(const std::string& s) {
  if (s.size() != 64) return false;
  for (char c : s) {
    bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!hex) return false;
  }
  return true;
}

bool HasHttpsScheme(const std::string& url) {
  const std::string kHttps = "https://";
  if (url.size() < kHttps.size()) return false;
  for (size_t i = 0; i < kHttps.size(); ++i) {
    char a = url[i], b = kHttps[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

// Object field-set check: every key must be in `allowed`, else false.
bool ClosedObject(const JsonValue& v, const char* const* allowed, size_t n) {
  if (!v.is_object()) return false;
  for (const auto& [k, _] : v.as_object()) {
    bool ok = false;
    for (size_t i = 0; i < n; ++i) ok = ok || (k == allowed[i]);
    if (!ok) return false;
  }
  return true;
}

}  // namespace

// ---- Version ---------------------------------------------------------------

bool Version::operator<(const Version& o) const {
  for (int i = 0; i < 4; ++i) {
    if (part[i] != o.part[i]) return part[i] < o.part[i];
  }
  return false;
}
bool Version::operator==(const Version& o) const {
  for (int i = 0; i < 4; ++i) {
    if (part[i] != o.part[i]) return false;
  }
  return true;
}
bool Version::operator>(const Version& o) const { return o < *this; }

bool Version::Parse(const std::string& text, Version* out) {
  if (text.empty()) return false;
  int part_index = 0;
  size_t i = 0;
  while (part_index < 4) {
    if (i >= text.size() || text[i] < '0' || text[i] > '9') return false;
    if (text[i] == '0' && (i + 1 < text.size() && text[i + 1] >= '0' &&
                           text[i + 1] <= '9')) {
      return false;  // leading zero
    }
    uint64_t acc = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      acc = acc * 10 + static_cast<uint64_t>(text[i] - '0');
      if (acc > 0xFFFFFFFFull) return false;
      ++i;
    }
    out->part[part_index++] = static_cast<uint32_t>(acc);
    if (part_index < 4) {
      if (i >= text.size() || text[i] != '.') return false;
      ++i;
    }
  }
  return i == text.size();  // exactly four parts, nothing trailing
}

std::string Version::ToString() const {
  std::string s;
  for (int i = 0; i < 4; ++i) {
    if (i > 0) s += '.';
    s += std::to_string(part[i]);
  }
  return s;
}

const char* ToString(ParseResult r) {
  switch (r) {
    case ParseResult::kOk: return "ok";
    case ParseResult::kMalformed: return "malformed-manifest";
    case ParseResult::kUnknownField: return "unknown-field";
    case ParseResult::kWrongProtocol: return "wrong-protocol";
    case ParseResult::kBadVersion: return "non-canonical-version";
    case ParseResult::kOversize: return "oversize";
    case ParseResult::kInsecureUrl: return "insecure-url";
    case ParseResult::kBadDigest: return "bad-digest";
  }
  return "unknown";
}

// ---- ParseEnvelope ----------------------------------------------------------

ParseResult ParseEnvelope(const std::string& raw, UpdateEnvelope* out) {
  if (raw.size() > kMaxDocBytes) return ParseResult::kOversize;
  // Safe JSON prefix (protocol_3_1.md:194-197, 541-544 @ d04cdb24): the
  // response may carry the `)]}'\n` anti-XSSI prefix; strip exactly that
  // prefix, nothing else, before parsing.
  std::string body = raw;
  const std::string kPrefix = ")]}'\n";
  if (body.compare(0, kPrefix.size(), kPrefix) == 0) body.erase(0, kPrefix.size());
  JsonParseResult pr = ParseJson(body);
  if (!pr.ok) return ParseResult::kMalformed;
  const JsonValue& doc = pr.value;
  if (!doc.is_object()) return ParseResult::kMalformed;

  static const char* kEnvelopeKeys[] = {
      "schema", "schema_version", "response", "epoch", "signature"};
  if (!ClosedObject(doc, kEnvelopeKeys, 5)) return ParseResult::kUnknownField;
  const JsonValue* schema = doc.find("schema");
  const JsonValue* sv = doc.find("schema_version");
  const JsonValue* response = doc.find("response");
  const JsonValue* epoch = doc.find("epoch");
  const JsonValue* sig = doc.find("signature");
  if (!schema || !schema->is_string() || schema->as_string() != "xr-update-envelope")
    return ParseResult::kMalformed;
  if (!sv || !sv->is_int() || sv->as_int() != 1) return ParseResult::kMalformed;
  if (!response || !response->is_object()) return ParseResult::kMalformed;
  if (!epoch || !epoch->is_object() || !sig || !sig->is_object())
    return ParseResult::kMalformed;

  // frozen 3.1 subset: response {protocol, server?, daystart?, app}
  static const char* kResponseKeys[] = {"protocol", "server", "daystart", "app"};
  if (!ClosedObject(*response, kResponseKeys, 4)) return ParseResult::kUnknownField;
  const JsonValue* protocol = response->find("protocol");
  if (!protocol || !protocol->is_string()) return ParseResult::kMalformed;
  if (protocol->as_string() != "3.1") return ParseResult::kWrongProtocol;
  const JsonValue* server = response->find("server");
  if (server && !server->is_string()) return ParseResult::kMalformed;
  const JsonValue* daystart = response->find("daystart");
  if (daystart && !daystart->is_object()) return ParseResult::kMalformed;
  if (daystart) {
    static const char* kDaystartKeys[] = {"elapsed_days", "elapsed_seconds"};
    if (!ClosedObject(*daystart, kDaystartKeys, 2)) return ParseResult::kUnknownField;
  }
  const JsonValue* app = response->find("app");
  if (!app || !app->is_array() || app->as_array().size() != 1)
    return ParseResult::kMalformed;  // profile: single-app response
  const JsonValue& app0 = app->as_array()[0];
  static const char* kAppKeys[] = {"appid", "status", "updatecheck"};
  if (!ClosedObject(app0, kAppKeys, 3)) return ParseResult::kUnknownField;
  const JsonValue* appid = app0.find("appid");
  const JsonValue* status = app0.find("status");
  const JsonValue* updatecheck = app0.find("updatecheck");
  if (!appid || !appid->is_string() || appid->as_string().empty() ||
      appid->as_string().size() > 128)
    return ParseResult::kMalformed;
  if (!status || !status->is_string()) return ParseResult::kMalformed;
  if (!updatecheck || !updatecheck->is_object()) return ParseResult::kMalformed;

  static const char* kUcKeys[] = {"status", "urls", "manifest"};
  if (!ClosedObject(*updatecheck, kUcKeys, 3)) return ParseResult::kUnknownField;
  const JsonValue* uc_status = updatecheck->find("status");
  if (!uc_status || !uc_status->is_string()) return ParseResult::kMalformed;
  const std::string ucs = uc_status->as_string();
  if (ucs != "ok" && ucs != "noupdate" && ucs.rfind("error-", 0) != 0)
    return ParseResult::kMalformed;

  out->schema = schema->as_string();
  out->schema_version = 1;
  out->protocol = protocol->as_string();
  out->app_id = appid->as_string();
  out->app_status = status->as_string();
  out->updatecheck_status = ucs;
  out->canonical_response = response->Canonical();

  // epoch block
  static const char* kEpochKeys[] = {"epoch_id", "key_id", "seq"};
  if (!ClosedObject(*epoch, kEpochKeys, 3)) return ParseResult::kUnknownField;
  const JsonValue* eid = epoch->find("epoch_id");
  const JsonValue* ekid = epoch->find("key_id");
  const JsonValue* eseq = epoch->find("seq");
  if (!eid || !eid->is_string() || eid->as_string().empty() || !ekid ||
      !ekid->is_string() || ekid->as_string().empty() || !eseq || !eseq->is_int() ||
      eseq->as_int() < 0)
    return ParseResult::kMalformed;
  out->epoch_id = eid->as_string();
  out->epoch_key_id = ekid->as_string();
  out->epoch_seq = eseq->as_int();

  // signature block (format work: fields name a standard scheme; the
  // primitive behind it is injected, never implemented here)
  static const char* kSigKeys[] = {"alg", "key_id", "sig"};
  if (!ClosedObject(*sig, kSigKeys, 3)) return ParseResult::kUnknownField;
  const JsonValue* alg = sig->find("alg");
  const JsonValue* skid = sig->find("key_id");
  const JsonValue* sval = sig->find("sig");
  if (!alg || !alg->is_string() || alg->as_string() != "minisign-ed25519")
    return ParseResult::kMalformed;
  if (!skid || !skid->is_string() || skid->as_string().empty() || !sval ||
      !sval->is_string() || sval->as_string().empty())
    return ParseResult::kMalformed;
  out->sig_alg = alg->as_string();
  out->sig_key_id = skid->as_string();
  out->sig_value = sval->as_string();

  // offer details are only required/validated when an update is offered
  if (ucs == "ok") {
    const JsonValue* manifest = updatecheck->find("manifest");
    const JsonValue* urls = updatecheck->find("urls");
    if (!manifest || !manifest->is_object() || !urls || !urls->is_object())
      return ParseResult::kMalformed;
    static const char* kManifestKeys[] = {"version", "packages", "run"};
    if (!ClosedObject(*manifest, kManifestKeys, 3)) return ParseResult::kUnknownField;
    const JsonValue* version = manifest->find("version");
    if (!version || !version->is_string()) return ParseResult::kMalformed;
    if (!Version::Parse(version->as_string(), &out->version))
      return ParseResult::kBadVersion;
    const JsonValue* packages = manifest->find("packages");
    if (!packages || !packages->is_object()) return ParseResult::kMalformed;
    static const char* kPackagesKeys[] = {"package"};
    if (!ClosedObject(*packages, kPackagesKeys, 1)) return ParseResult::kUnknownField;
    const JsonValue* package = packages->find("package");
    if (!package || !package->is_array() || package->as_array().size() != 1)
      return ParseResult::kMalformed;  // profile: single package
    const JsonValue& pkg = package->as_array()[0];
    static const char* kPkgKeys[] = {"name", "size", "hash_sha256", "fp"};
    if (!ClosedObject(pkg, kPkgKeys, 4)) return ParseResult::kUnknownField;
    const JsonValue* name = pkg.find("name");
    const JsonValue* size = pkg.find("size");
    const JsonValue* hash = pkg.find("hash_sha256");
    const JsonValue* fp = pkg.find("fp");
    if (!name || !name->is_string() || name->as_string().empty() || !size ||
        !size->is_int() || size->as_int() < 0 || !fp || !fp->is_string())
      return ParseResult::kMalformed;  // a REQUIRED field is absent
    if (!hash || !hash->is_string())
      return ParseResult::kMalformed;  // a REQUIRED field is absent
    if (!AllHex64(hash->as_string()))
      return ParseResult::kBadDigest;  // present but not a sha256 hex
    out->package_name = name->as_string();
    out->package_size = size->as_int();
    out->package_hash_sha256 = hash->as_string();
    out->package_fp = fp->as_string();
    static const char* kUrlsKeys[] = {"url"};
    if (!ClosedObject(*urls, kUrlsKeys, 1)) return ParseResult::kUnknownField;
    const JsonValue* url = urls->find("url");
    if (!url || !url->is_array() || url->as_array().empty())
      return ParseResult::kMalformed;
    for (const JsonValue& u : url->as_array()) {
      static const char* kUrlKeys[] = {"codebase"};
      if (!ClosedObject(u, kUrlKeys, 1)) return ParseResult::kUnknownField;
      const JsonValue* cb = u.find("codebase");
      if (!cb || !cb->is_string()) return ParseResult::kMalformed;
      if (!HasHttpsScheme(cb->as_string())) return ParseResult::kInsecureUrl;
    }
    const JsonValue* cb0 = url->as_array()[0].find("codebase");
    out->codebase = cb0->as_string();
  }

  // manifest_id is RECOMPUTED client-side over the canonical response bytes
  out->manifest_id = Sha256Hex(out->canonical_response);
  return ParseResult::kOk;
}

}  // namespace xr::update
