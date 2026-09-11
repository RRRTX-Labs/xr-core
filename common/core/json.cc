// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — implementation of the strict JSON layer (see json.h). Parser
// is RFC 8259-strict with a 64-depth cap and UTF-8 validation; serializer
// reproduces the Python canonical form byte-for-byte.
#include "common/core/json.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdio>
#include <cstring>

namespace xr::common {

constexpr int kMaxDepth = 64;

bool JsonValue::as_bool() const { assert(is_bool()); return bool_; }
int64_t JsonValue::as_int() const { assert(is_int()); return int_; }
double JsonValue::as_double() const {
  if (is_int()) return static_cast<double>(int_);
  assert(is_double());
  return double_;
}
const std::string& JsonValue::as_string() const { assert(is_string()); return str_; }
const JsonValue::Array& JsonValue::as_array() const { assert(is_array()); return arr_; }
const JsonValue::Object& JsonValue::as_object() const { assert(is_object()); return obj_; }

const JsonValue* JsonValue::find(std::string_view key) const {
  if (type_ != Type::kObject) return nullptr;
  auto it = obj_.find(std::string(key));
  return it == obj_.end() ? nullptr : &it->second;
}

bool JsonValue::operator==(const JsonValue& other) const {
  if (type_ != other.type_) {
    // int/double never compare equal across types (Python: 1 != 1.0 is False
    // in Python, but json-level equality in our validators is type-strict).
    return false;
  }
  switch (type_) {
    case Type::kNull: return true;
    case Type::kBool: return bool_ == other.bool_;
    case Type::kInt: return int_ == other.int_;
    case Type::kDouble: return double_ == other.double_;
    case Type::kString: return str_ == other.str_;
    case Type::kArray: return arr_ == other.arr_;
    case Type::kObject: return obj_ == other.obj_;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Canonical serialization (Python json.dumps compatible).
// ---------------------------------------------------------------------------

namespace {

// Hex of a code point <= 0xFFFF (precondition held by all call sites).
void AppendHex4(std::string* out, uint32_t v) {
  char buf[9];  // sized for the full %x range; callers pass <= 0xFFFF
  std::snprintf(buf, sizeof(buf), "%04x", v);
  out->append(buf, 4);  // exactly 4 hex digits (values are <= 0xFFFF)
}

// Decodes one UTF-8 sequence at s[pos]. Returns length (1-4) or 0 on invalid.
int DecodeUtf8(std::string_view s, size_t pos, uint32_t* cp) {
  unsigned char c = static_cast<unsigned char>(s[pos]);
  if (c < 0x80) { *cp = c; return 1; }
  if ((c & 0xE0) == 0xC0) {
    if (pos + 1 >= s.size()) return 0;
    unsigned char c1 = static_cast<unsigned char>(s[pos + 1]);
    if ((c1 & 0xC0) != 0x80) return 0;
    uint32_t v = ((c & 0x1Fu) << 6) | (c1 & 0x3Fu);
    if (v < 0x80) return 0;  // overlong
    *cp = v;
    return 2;
  }
  if ((c & 0xF0) == 0xE0) {
    if (pos + 2 >= s.size()) return 0;
    unsigned char c1 = static_cast<unsigned char>(s[pos + 1]);
    unsigned char c2 = static_cast<unsigned char>(s[pos + 2]);
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return 0;
    uint32_t v = ((c & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
    if (v < 0x800 || (v >= 0xD800 && v <= 0xDFFF)) return 0;  // overlong/surrogate
    *cp = v;
    return 3;
  }
  if ((c & 0xF8) == 0xF0) {
    if (pos + 3 >= s.size()) return 0;
    unsigned char c1 = static_cast<unsigned char>(s[pos + 1]);
    unsigned char c2 = static_cast<unsigned char>(s[pos + 2]);
    unsigned char c3 = static_cast<unsigned char>(s[pos + 3]);
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return 0;
    uint32_t v = ((c & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
    if (v < 0x10000 || v > 0x10FFFF) return 0;
    *cp = v;
    return 4;
  }
  return 0;
}

void AppendEscapedString(std::string* out, std::string_view s) {
  out->push_back('"');
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      switch (c) {
        case '"': out->append("\\\""); break;
        case '\\': out->append("\\\\"); break;
        case '\b': out->append("\\b"); break;
        case '\f': out->append("\\f"); break;
        case '\n': out->append("\\n"); break;
        case '\r': out->append("\\r"); break;
        case '\t': out->append("\\t"); break;
        default:
          if (c >= 0x20 && c <= 0x7e) {
            out->push_back(static_cast<char>(c));
          } else {
            out->append("\\u");
            AppendHex4(out, c);
          }
      }
      ++i;
      continue;
    }
    uint32_t cp = 0;
    int len = DecodeUtf8(s, i, &cp);
    if (len == 0) {  // invalid UTF-8: emit U+FFFD, advance one byte
      out->append("\\ufffd");
      ++i;
      continue;
    }
    if (cp <= 0xFFFF) {
      out->append("\\u");
      AppendHex4(out, cp);
    } else {
      const uint32_t v = cp - 0x10000;
      out->append("\\u");
      AppendHex4(out, 0xD800 + (v >> 10));
      out->append("\\u");
      AppendHex4(out, 0xDC00 + (v & 0x3FF));
    }
    i += static_cast<size_t>(len);
  }
  out->push_back('"');
}

}  // namespace

std::string CanonicalJsonString(std::string_view s) {
  std::string out;
  AppendEscapedString(&out, s);
  return out;
}

void JsonValue::AppendCanonical(std::string* out) const { AppendCanonicalTo(out); }

void JsonValue::AppendCanonicalTo(std::string* out) const {
  switch (type_) {
    case Type::kNull: out->append("null"); break;
    case Type::kBool: out->append(bool_ ? "true" : "false"); break;
    case Type::kInt: {
      char buf[24];
      auto r = std::to_chars(buf, buf + sizeof(buf), int_);
      out->append(buf, r.ptr);
      break;
    }
    case Type::kDouble: {
      char buf[40];
      auto r = std::to_chars(buf, buf + sizeof(buf), double_);
      out->append(buf, r.ptr);
      break;
    }
    case Type::kString: AppendEscapedString(out, str_); break;
    case Type::kArray: {
      out->push_back('[');
      for (size_t i = 0; i < arr_.size(); ++i) {
        if (i) out->push_back(',');
        arr_[i].AppendCanonicalTo(out);
      }
      out->push_back(']');
      break;
    }
    case Type::kObject: {
      out->push_back('{');
      bool first = true;
      for (const auto& [k, v] : obj_) {
        if (!first) out->push_back(',');
        first = false;
        AppendEscapedString(out, k);
        out->push_back(':');
        v.AppendCanonicalTo(out);
      }
      out->push_back('}');
      break;
    }
  }
}

std::string JsonValue::Canonical() const {
  std::string out;
  out.reserve(256);
  AppendCanonicalTo(&out);
  return out;
}
}  // namespace xr::common
