// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — strict JSON value model + parser + canonical serializer, the
// SINGLE in-tree copy shared by every core (P11-T0-b; before this, five
// byte-identical per-core copies drifted only by namespace word).
// Byte-compatible with the Python reference canonical form
// (json.dumps(obj, sort_keys=True, separators=(",", ":")) — ensure_ascii
// semantics, lowercase \uXXXX escapes, surrogate pairs for astral code
// points). Parsing is RFC 8259-strict: depth-capped (DoS), UTF-8-validated,
// trailing content rejected, duplicate keys keep-last (matches the Python
// harness). No exceptions; typed parse result. Never guesses: a parse error
// is surfaced to the caller, which must deny.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace xr::common {

class JsonValue {
 public:
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue>;  // sorted: canonical by walk
  enum class Type { kNull, kBool, kInt, kDouble, kString, kArray, kObject };

  JsonValue() : type_(Type::kNull) {}
  JsonValue(std::nullptr_t) : type_(Type::kNull) {}
  JsonValue(bool b) : type_(Type::kBool) { bool_ = b; }
  JsonValue(int64_t i) : type_(Type::kInt) { int_ = i; }
  JsonValue(int i) : type_(Type::kInt) { int_ = i; }
  JsonValue(double d) : type_(Type::kDouble) { double_ = d; }
  JsonValue(std::string s) : type_(Type::kString) { str_ = std::move(s); }
  JsonValue(const char* s) : type_(Type::kString) { str_ = s; }
  JsonValue(Array a) : type_(Type::kArray) { arr_ = std::move(a); }
  JsonValue(Object o) : type_(Type::kObject) { obj_ = std::move(o); }

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::kNull; }
  bool is_bool() const { return type_ == Type::kBool; }
  bool is_int() const { return type_ == Type::kInt; }
  bool is_double() const { return type_ == Type::kDouble; }
  bool is_number() const { return is_int() || is_double(); }
  bool is_string() const { return type_ == Type::kString; }
  bool is_array() const { return type_ == Type::kArray; }
  bool is_object() const { return type_ == Type::kObject; }

  bool as_bool() const;      // assert-checked (validate type first)
  int64_t as_int() const;
  double as_double() const;
  const std::string& as_string() const;
  const Array& as_array() const;
  const Object& as_object() const;

  // Object convenience (returns nullptr when absent / not an object).
  const JsonValue* find(std::string_view key) const;

  bool operator==(const JsonValue& other) const;
  bool operator!=(const JsonValue& other) const { return !(*this == other); }

  // Canonical JSON: sorted keys, no whitespace, Python-compatible escaping.
  std::string Canonical() const;
  void AppendCanonical(std::string* out) const;

 private:
  void AppendCanonicalTo(std::string* out) const;
  Type type_;
  bool bool_ = false;
  int64_t int_ = 0;
  double double_ = 0.0;
  std::string str_;
  Array arr_;
  Object obj_;
};

struct JsonParseResult {
  bool ok = false;
  JsonValue value;
  std::string error;   // human-readable, includes byte offset
  size_t offset = 0;   // error position (0 when ok)
};

// Strict parse of a complete JSON document (trailing garbage rejected).
JsonParseResult ParseJson(std::string_view text);

// Escapes `s` into a canonical JSON string literal (including quotes).
std::string CanonicalJsonString(std::string_view s);

}  // namespace xr::common
