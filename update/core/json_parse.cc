// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: S0 — strict RFC 8259 parser (depth-capped, UTF-8-validated,
// trailing-content rejected, duplicate keys keep-last). See json.h.
#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdio>
#include <cstring>

#include "update/core/json.h"

namespace xr::update {

constexpr int kMaxDepth = 64;



namespace {

// Decodes one UTF-8 sequence at s[pos]. Returns length (1-4) or 0 on invalid.
// (Twin of the serializer-side decoder in json.cc; both are covered by the
// round-trip and canonical-parity tests.)
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

class Parser {
 public:
  Parser(std::string_view text) : text_(text) {}

  JsonParseResult Run() {
    JsonParseResult r;
    SkipWs();
    if (!ParseValue(&root_, 0)) {
      r.error = error_;
      r.offset = pos_;
      return r;
    }
    SkipWs();
    if (pos_ != text_.size()) {
      r.error = "trailing content after JSON document";
      r.offset = pos_;
      return r;
    }
    r.ok = true;
    r.value = std::move(root_);
    return r;
  }

 private:
  void SkipWs() {
    while (pos_ < text_.size()) {
      char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
      else break;
    }
  }

  bool Fail(const std::string& msg, size_t at) {
    if (error_.empty()) error_ = msg + " at offset " + std::to_string(at);
    return false;
  }

  bool ParseValue(JsonValue* out, int depth) {
    if (depth > kMaxDepth) return Fail("nesting depth exceeds 64", pos_);
    if (pos_ >= text_.size()) return Fail("unexpected end of input", pos_);
    char c = text_[pos_];
    switch (c) {
      case '{': return ParseObject(out, depth);
      case '[': return ParseArray(out, depth);
      case '"': {
        std::string s;
        if (!ParseString(&s)) return false;
        *out = JsonValue(std::move(s));
        return true;
      }
      case 't':
        if (text_.substr(pos_, 4) == "true") { pos_ += 4; *out = JsonValue(true); return true; }
        return Fail("invalid literal", pos_);
      case 'f':
        if (text_.substr(pos_, 5) == "false") { pos_ += 5; *out = JsonValue(false); return true; }
        return Fail("invalid literal", pos_);
      case 'n':
        if (text_.substr(pos_, 4) == "null") { pos_ += 4; *out = JsonValue(nullptr); return true; }
        return Fail("invalid literal", pos_);
      default:
        return ParseNumber(out);
    }
  }

  bool ParseObject(JsonValue* out, int depth) {
    ++pos_;  // '{'
    JsonValue::Object obj;
    SkipWs();
    if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; *out = JsonValue(std::move(obj)); return true; }
    for (;;) {
      SkipWs();
      if (pos_ >= text_.size() || text_[pos_] != '"') return Fail("expected object key string", pos_);
      std::string key;
      if (!ParseString(&key)) return false;
      SkipWs();
      if (pos_ >= text_.size() || text_[pos_] != ':') return Fail("expected ':'", pos_);
      ++pos_;
      SkipWs();
      JsonValue val;
      if (!ParseValue(&val, depth + 1)) return false;
      obj.insert_or_assign(std::move(key), std::move(val));  // duplicate keys: keep last
      SkipWs();
      if (pos_ >= text_.size()) return Fail("unterminated object", pos_);
      if (text_[pos_] == ',') { ++pos_; continue; }
      if (text_[pos_] == '}') { ++pos_; break; }
      return Fail("expected ',' or '}'", pos_);
    }
    *out = JsonValue(std::move(obj));
    return true;
  }

  bool ParseArray(JsonValue* out, int depth) {
    ++pos_;  // '['
    JsonValue::Array arr;
    SkipWs();
    if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; *out = JsonValue(std::move(arr)); return true; }
    for (;;) {
      SkipWs();
      JsonValue val;
      if (!ParseValue(&val, depth + 1)) return false;
      arr.push_back(std::move(val));
      SkipWs();
      if (pos_ >= text_.size()) return Fail("unterminated array", pos_);
      if (text_[pos_] == ',') { ++pos_; continue; }
      if (text_[pos_] == ']') { ++pos_; break; }
      return Fail("expected ',' or ']'", pos_);
    }
    *out = JsonValue(std::move(arr));
    return true;
  }

  bool ParseHex4(uint32_t* out) {
    if (pos_ + 4 > text_.size()) return Fail("truncated \\u escape", pos_);
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = text_[pos_ + i];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
      else return Fail("invalid \\u escape hex digit", pos_ + static_cast<size_t>(i));
    }
    pos_ += 4;
    *out = v;
    return true;
  }

  void AppendUtf8(std::string* s, uint32_t cp) {
    if (cp <= 0x7F) {
      s->push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      s->push_back(static_cast<char>(0xC0 | (cp >> 6)));
      s->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      s->push_back(static_cast<char>(0xE0 | (cp >> 12)));
      s->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      s->push_back(static_cast<char>(0xF0 | (cp >> 18)));
      s->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      s->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool ParseString(std::string* out) {
    ++pos_;  // opening quote
    std::string s;
    for (;;) {
      if (pos_ >= text_.size()) return Fail("unterminated string", pos_);
      unsigned char c = static_cast<unsigned char>(text_[pos_]);
      if (c == '"') { ++pos_; *out = std::move(s); return true; }
      if (c == '\\') {
        ++pos_;
        if (pos_ >= text_.size()) return Fail("truncated escape", pos_);
        char e = text_[pos_];
        switch (e) {
          case '"': s.push_back('"'); ++pos_; break;
          case '\\': s.push_back('\\'); ++pos_; break;
          case '/': s.push_back('/'); ++pos_; break;
          case 'b': s.push_back('\b'); ++pos_; break;
          case 'f': s.push_back('\f'); ++pos_; break;
          case 'n': s.push_back('\n'); ++pos_; break;
          case 'r': s.push_back('\r'); ++pos_; break;
          case 't': s.push_back('\t'); ++pos_; break;
          case 'u': {
            ++pos_;
            uint32_t cp = 0;
            if (!ParseHex4(&cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              // high surrogate: require a following \uDC00-\uDFFF
              if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                size_t save = pos_;
                pos_ += 2;
                uint32_t lo = 0;
                if (!ParseHex4(&lo)) return false;
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                  AppendUtf8(&s, 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00));
                } else {
                  return Fail("invalid low surrogate", save);
                }
              } else {
                return Fail("lone high surrogate", pos_);
              }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              return Fail("lone low surrogate", pos_);
            } else {
              AppendUtf8(&s, cp);
            }
            break;
          }
          default: return Fail("invalid escape character", pos_);
        }
        continue;
      }
      if (c < 0x20) return Fail("raw control character in string", pos_);
      if (c < 0x80) { s.push_back(static_cast<char>(c)); ++pos_; continue; }
      // multi-byte UTF-8: validate
      uint32_t cp = 0;
      int len = DecodeUtf8(text_, pos_, &cp);
      if (len == 0) return Fail("invalid UTF-8 sequence", pos_);
      s.append(text_.substr(pos_, static_cast<size_t>(len)));
      pos_ += static_cast<size_t>(len);
    }
  }

  bool ParseNumber(JsonValue* out) {
    size_t start = pos_;
    bool is_double = false;
    if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
    if (pos_ >= text_.size()) return Fail("invalid number", start);
    if (text_[pos_] == '0') {
      ++pos_;
      if (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
        return Fail("leading zero in number", pos_);
    } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
    } else {
      return Fail("invalid number", start);
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      is_double = true;
      ++pos_;
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9')
        return Fail("missing fraction digits", pos_);
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      is_double = true;
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9')
        return Fail("missing exponent digits", pos_);
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
    }
    std::string num(text_.substr(start, pos_ - start));
    if (!is_double) {
      int64_t v = 0;
      auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), v);
      if (ec == std::errc()) { *out = JsonValue(v); return true; }
      // overflow: fall through to double (validators reject doubles where
      // integers are expected, so this stays deny-safe).
    }
    double d = 0.0;
    auto [ptr2, ec2] = std::from_chars(num.data(), num.data() + num.size(), d);
    if (ec2 != std::errc()) return Fail("unparseable number", start);
    *out = JsonValue(d);
    return true;
  }

  std::string_view text_;
  size_t pos_ = 0;
  std::string error_;
  JsonValue root_;
};

}  // namespace

JsonParseResult ParseJson(std::string_view text) {
  return Parser(text).Run();
}

}  // namespace xr::update
