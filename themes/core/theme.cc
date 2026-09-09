// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Theme model implementation (see theme.h). Everything in this file is
// std-only C++20 + the local JSON model; no Chromium, no Skia.
#include "themes/core/theme.h"

#include <cctype>
#include <cstdint>

namespace xr::themes {
namespace {

bool IsHex(const std::string& v) {
  if (v.size() < 2 || v[0] != '#') return false;
  for (size_t i = 1; i < v.size(); ++i)
    if (!std::isxdigit(static_cast<unsigned char>(v[i]))) return false;
  return true;
}

// The reserved critical-red law: the value must be in the canonical alarming
// family — red-dominant, saturated, and not a pale pastel — or the doc is
// refused. Definition (recorded, identical in theme.cc / tokens_gen.py /
// fakes/themes.py):
//   r >= 0x60 AND r >= max(g,b) AND (r - min(g,b)) >= 0x60
// This admits dark maroons (#8f1a1a), alarm reds on light surfaces
// (#d1242f) and bright alarm reds on dark surfaces (#ff8a8a) while refusing
// calm colors (blues/greens/greys) and pale pinks (#ffaeae has r-min =
// 0x51 < 0x60).
bool IsAlarmingFamily(const std::string& hex) {
  if (hex.size() < 7) return false;
  auto nyb = [&](size_t i) -> int {
    char c = hex[i];
    if (c >= '0' && c <= '9') return c - '0';
    return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
  };
  const int r = nyb(1) * 16 + nyb(2);
  const int g = nyb(3) * 16 + nyb(4);
  const int b = nyb(5) * 16 + nyb(6);
  if (r < 0x60 || r < g || r < b) return false;
  const int lo = g < b ? g : b;
  return (r - lo) >= 0x60;
}

}  // namespace

const TokenDef* FindToken(const std::vector<TokenDef>& tokens,
                          const std::string& name) {
  for (const auto& t : tokens)
    if (t.name == name) return &t;
  return nullptr;
}

TokenSource LoadTokenSource(const std::string& json_text) {
  TokenSource src;
  JsonParseResult pr = ParseJson(json_text);
  if (!pr.ok) {
    src.error = "tokens source is not strict JSON: " + pr.error;
    return src;
  }
  const JsonValue& root = pr.value;
  if (!root.is_object()) {
    src.error = "tokens source must be a JSON object";
    return src;
  }
  const JsonValue* sv = root.find("schema_version");
  if (!sv || !sv->is_int() || sv->as_int() != 1) {
    src.error = "tokens source: schema_version must be 1 (unknown version "
                "refused — rollback law)";
    return src;
  }
  const JsonValue* meta = root.find("tokens");
  const JsonValue* themes = root.find("themes");
  if (!meta || !meta->is_object() || meta->as_object().empty()) {
    src.error = "tokens source: 'tokens' object required";
    return src;
  }
  if (!themes || !themes->is_object() || themes->as_object().empty()) {
    src.error = "tokens source: 'themes' object required";
    return src;
  }
  for (const auto& [name, defv] : meta->as_object()) {
    if (!defv.is_object()) {
      src.error = "token '" + name + "': meta must be an object";
      return src;
    }
    TokenDef d;
    d.name = name;
    const JsonValue* tv = defv.find("type");
    if (!tv || !tv->is_string() ||
        (tv->as_string() != "color" && tv->as_string() != "dimension" &&
         tv->as_string() != "font")) {
      src.error = "token '" + name + "': unknown/missing type";
      return src;
    }
    d.type = tv->as_string();
    const JsonValue* sc = defv.find("security_critical");
    d.security_critical = sc && sc->is_bool() && sc->as_bool();
    const JsonValue* rs = defv.find("reserved");
    d.reserved = rs && rs->is_bool() && rs->as_bool();
    if (d.name == "critical-red" && !d.reserved) {
      src.error = "token 'critical-red': reserved law — must carry reserved";
      return src;
    }
    if (d.name == "critical-red" && !d.security_critical) {
      src.error = "token 'critical-red': reserved law — must be "
                  "security_critical";
      return src;
    }
    if (const JsonValue* u = defv.find("usage"); u && u->is_string())
      d.usage = u->as_string();
    if (const JsonValue* pl = defv.find("pairing"); pl) {
      if (!pl->is_array()) {
        src.error = "token '" + name + "': pairing must be an array";
        return src;
      }
      for (const auto& p : pl->as_array()) {
        if (!p.is_string()) {
          src.error = "token '" + name + "': pairing entries must be strings";
          return src;
        }
        d.pairing.push_back(p.as_string());
      }
    }
    src.tokens.push_back(std::move(d));
  }
  // pairing references must resolve; security pairings symmetric is NOT
  // required (pairing is declared by the foreground token).
  for (const auto& t : src.tokens) {
    for (const auto& p : t.pairing)
      if (FindToken(src.tokens, p) == nullptr) {
        src.error = "token '" + t.name + "': pairing '" + p +
                    "' is not a token";
        return src;
      }
  }
  for (const auto& [tname, tvals] : themes->as_object()) {
    if (!tvals.is_object()) {
      src.error = "theme '" + tname + "': values must be an object";
      return src;
    }
    BuiltinTheme bt;
    bt.name = tname;
    for (const auto& [key, val] : tvals.as_object()) {
      if (key == "waivers") {
        if (!val.is_array()) {
          src.error = "theme '" + tname +
                      "': waivers must be an array of rows";
          return src;
        }
        for (const auto& w : val.as_array())
          if (w.is_object())
            bt.waivers.push_back(w.Canonical());
          else
            src.error = "theme '" + tname + "': waiver row must be an object";
        continue;
      }
      const TokenDef* t = FindToken(src.tokens, key);
      if (!t) {
        src.error = "theme '" + tname + "': value for unknown token '" + key +
                    "' (strict: unknown tokens are rejected)";
        return src;
      }
      bt.values[key] = val;
    }
    for (const auto& t : src.tokens) {
      if (bt.values.find(t.name) == bt.values.end()) {
        src.error = "theme '" + tname + "': missing value for token '" +
                    t.name + "'";
        return src;
      }
    }
    src.builtins.push_back(std::move(bt));
  }
  // system resolution data
  const JsonValue* sys = root.find("system_resolution");
  if (!sys || !sys->is_object()) {
    src.error = "tokens source: 'system_resolution' object required";
    return src;
  }
  const JsonValue* sd = sys->find("default");
  if (!sd || !sd->is_string()) {
    src.error = "system_resolution.default required";
    return src;
  }
  src.system_default = sd->as_string();
  const JsonValue* modes = sys->find("modes");
  if (!modes || !modes->is_object()) {
    src.error = "system_resolution.modes required";
    return src;
  }
  bool has_default = false;
  for (const auto& [mode, target] : modes->as_object()) {
    if (mode == "default" || mode == "light" || mode == "dark" ||
        mode == "high-contrast") {
      if (!target.is_string()) {
        src.error = "system_resolution.modes." + mode + ": target required";
        return src;
      }
      src.system_modes[mode] = target.as_string();
      if (mode == src.system_default) has_default = true;
    }
  }
  if (!has_default || src.system_modes.find(src.system_default) ==
                          src.system_modes.end()) {
    src.error = "system_resolution.default must be a declared mode";
    return src;
  }
  // The built-in set must cover what system resolution points at.
  for (const auto& [mode, target] : src.system_modes) {
    if (FindBuiltin(src.builtins, target) == nullptr) {
      src.error = "system_resolution.modes." + mode + " points at unknown "
                  "builtin '" + target + "'";
      return src;
    }
  }
  src.ok = true;
  return src;
}

const BuiltinTheme* FindBuiltin(const std::vector<BuiltinTheme>& builtins,
                                const std::string& name) {
  for (const auto& b : builtins)
    if (b.name == name) return &b;
  return nullptr;
}

std::string FindDuplicateKey(const std::string& raw) {
  // Cheap deterministic scanner for duplicate object keys at ANY depth in
  // the raw text (the JSON parser itself keeps-last; hostile import must
  // reject, not last-win). Line-oriented: report the first duplicate.
  // The scanner is conservative: only ASCII object syntax is tracked, and a
  // string VALUE that happens to contain '{'/'"' stays inside quotes.
  struct Frame {
    std::map<std::string, int> keys;
  };
  std::vector<Frame> stack;
  std::string key;
  bool in_string = false;
  bool in_escape = false;
  bool in_key = false;  // inside a string that is a key position
  bool expect_key = false;  // just saw '{' or ','
  std::string dup;
  for (size_t i = 0; i < raw.size() && dup.empty(); ++i) {
    char c = raw[i];
    if (in_string) {
      if (in_escape) {
        in_escape = false;
      } else if (c == '\\') {
        in_escape = true;
      } else if (c == '"') {
        in_string = false;
        if (expect_key && in_key) {
          if (stack.empty()) return key;  // malformed; parser will refuse
          Frame& fr = stack.back();
          if (++fr.keys[key] > 1) {
            dup = key;
            break;
          }
          in_key = false;
        }
      } else {
        key.push_back(c);
      }
      continue;
    }
    switch (c) {
      case '"':
        in_string = true;
        key.clear();
        in_key = true;
        break;
      case '{':
        stack.push_back(Frame{});
        expect_key = true;
        break;
      case '}':
        if (!stack.empty()) stack.pop_back();
        break;
      case ',':
        expect_key = true;
        break;
      case ':':
        expect_key = false;
        break;
      default:
        // any non-whitespace value token cancels a pending key expectation
        if (!std::isspace(static_cast<unsigned char>(c))) expect_key = false;
        break;
    }
  }
  return dup;
}

DocVerdict ValidateThemeDoc(const std::vector<TokenDef>& tokens,
                            const TokenMap& doc) {
  DocVerdict v;
  auto problem = [&](const std::string& where, const std::string& what) {
    v.problems.push_back(DocProblem{where, what});
  };
  for (const auto& [key, val] : doc) {
    const TokenDef* t = FindToken(tokens, key);
    if (t == nullptr) {
      problem("key '" + key + "'",
              "unknown token — the v1 token set is fixed; unknown tokens are "
              "rejected (contract), not ignored");
      continue;
    }
    const std::string& type = t->type;
    bool type_ok = false;
    if (type == "color") {
      type_ok = val.is_string() && IsHex(val.as_string()) &&
                (val.as_string().size() == 7 || val.as_string().size() == 9);
    } else if (type == "dimension") {
      type_ok = val.is_int() && val.as_int() >= 0 && val.as_int() <= 4096;
    } else if (type == "font") {
      if (val.is_string()) {
        const std::string& s = val.as_string();
        type_ok = !s.empty() && s.find("url(") == std::string::npos &&
                  s.find(';') == std::string::npos &&
                  s.find('{') == std::string::npos &&
                  s.find('}') == std::string::npos &&
                  s.find("http") == std::string::npos &&
                  s.find('\n') == std::string::npos;
        if (!type_ok) {
          // Keep the precise reason: the loader must say WHY.
          if (s.find("url(") != std::string::npos)
            problem("token '" + key + "'",
                    "font value contains url( — remote/embedded resources "
                    "are rejected (no code/no asset path in themes)");
          else if (s.find("http") != std::string::npos)
            problem("token '" + key + "'",
                    "font value references a remote resource — rejected");
          else
            problem("token '" + key + "'",
                    "font value must be a plain system-font stack");
          continue;
        }
      }
    }
    if (!type_ok) {
      problem("token '" + key + "'",
              "value does not match declared type '" + type + "'");
      continue;
    }
    // Reserved critical-red law (data + validator, both layers).
    if (t->name == "critical-red") {
      const std::string& hex = val.as_string();
      if (!IsAlarmingFamily(hex.substr(0, 7))) {
        problem("token 'critical-red'",
                "RESERVED: critical-red must stay in the canonical alarming "
                "family (red-dominant); mapping it to a non-alarming color "
                "is refused");
      }
    }
  }
  v.ok = v.problems.empty();
  return v;
}

}  // namespace xr::themes
