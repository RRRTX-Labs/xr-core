// Copyright 2026 RRRTX Labs
// test_descriptor.cc — frozen command-descriptor-v1 validation battery.
//
// Proves "implement to it, never redefine it": loads the FROZEN
// command-descriptor-v1.schema.json (xr-browser) and asserts this validator's
// required set + enums + additionalProperties:false match it, then runs the
// valid/invalid battery.
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "commands/core/descriptor.h"
#include "commands/core/json.h"
#include "commands/tests/harness.h"

using namespace xr::commands;

namespace {

JsonValue Obj(const char* json) {
  auto p = ParseJson(json);
  XR_EXPECT_MSG(p.ok, "test JSON parsed");
  return p.value;
}

void TestAgainstFrozenSchema() {
  const char* root = std::getenv("XR_BROWSER_ROOT");
  if (root == nullptr) {
    std::fprintf(stderr, "  (XR_BROWSER_ROOT unset; skipping frozen-schema cross-check)\n");
    return;
  }
  std::string path = std::string(root) + "/docs/contracts/command-descriptor-v1.schema.json";
  std::ifstream f(path);
  if (!f.good()) {
    std::fprintf(stderr, "  (cannot open frozen schema at %s; skipping cross-check)\n",
                 path.c_str());
    return;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  auto p = ParseJson(ss.str());
  XR_EXPECT_MSG(p.ok, "frozen schema parsed");
  const JsonValue& s = p.value;

  // required set == my kRequiredFields.
  std::set<std::string> schema_req;
  const JsonValue* req = s.find("required");
  if (req && req->is_array())
    for (const auto& r : req->as_array())
      if (r.is_string()) schema_req.insert(r.as_string());
  std::set<std::string> my_req(kRequiredFields.begin(), kRequiredFields.end());
  XR_EXPECT_MSG(schema_req == my_req,
                "frozen required set matches the validator's kRequiredFields");

  // enums match.
  const JsonValue* props = s.find("properties");
  auto enum_of = [&](const char* field) {
    std::set<std::string> out;
    if (props && props->is_object() && props->as_object().count(field)) {
      const JsonValue* fld = &props->as_object().at(field);
      const JsonValue* e = fld->find("enum");
      if (e && e->is_array())
        for (const auto& v : e->as_array())
          if (v.is_string()) out.insert(v.as_string());
    }
    return out;
  };
  std::set<std::string> my_tiers(kAttentionTiers.begin(), kAttentionTiers.end());
  std::set<std::string> my_dangers(kDangerClasses.begin(), kDangerClasses.end());
  XR_EXPECT_MSG(enum_of("attention_tier") == my_tiers,
                "frozen attention_tier enum matches");
  XR_EXPECT_MSG(enum_of("danger_class") == my_dangers,
                "frozen danger_class enum matches");

  // additionalProperties: false (strict).
  const JsonValue* ap = s.find("additionalProperties");
  XR_EXPECT_MSG(ap && ap->is_bool() && !ap->as_bool(),
                "frozen schema has additionalProperties:false");
}

void TestBattery() {
  // valid: exactly the six fields, legal enums.
  DescriptorResult ok = ValidateDescriptor(Obj(
      "{\"id\":\"a.b\",\"title\":\"T\",\"attention_tier\":\"tier1\","
      "\"danger_class\":\"safe\",\"surface\":\"command-palette\","
      "\"handler\":\"action.x\"}"));
  XR_EXPECT_MSG(ok.ok, "valid descriptor accepted");
  XR_EXPECT_STREQ(ok.fields.id.c_str(), "a.b");
  XR_EXPECT_STREQ(ok.fields.danger_class.c_str(), "safe");

  // unknown field => reject (additionalProperties:false).
  DescriptorResult unknown = ValidateDescriptor(Obj(
      "{\"id\":\"a\",\"title\":\"T\",\"attention_tier\":\"tier0\","
      "\"danger_class\":\"safe\",\"surface\":\"s\",\"handler\":\"h\","
      "\"keywords\":[\"x\"]}"));
  XR_EXPECT_MSG(!unknown.ok, "unknown field rejected");
  XR_EXPECT(unknown.error.find("additionalProperties") != std::string::npos);

  // missing field => reject.
  DescriptorResult missing = ValidateDescriptor(Obj(
      "{\"id\":\"a\",\"title\":\"T\",\"attention_tier\":\"tier0\","
      "\"danger_class\":\"safe\",\"surface\":\"s\"}"));  // no handler
  XR_EXPECT_MSG(!missing.ok && missing.error.find("handler") != std::string::npos,
                "missing required field rejected");

  // bad enum => reject.
  DescriptorResult bad_tier = ValidateDescriptor(Obj(
      "{\"id\":\"a\",\"title\":\"T\",\"attention_tier\":\"tier9\","
      "\"danger_class\":\"safe\",\"surface\":\"s\",\"handler\":\"h\"}"));
  XR_EXPECT_MSG(!bad_tier.ok && bad_tier.error.find("tier9") != std::string::npos,
                "out-of-enum attention_tier rejected");
  DescriptorResult bad_danger = ValidateDescriptor(Obj(
      "{\"id\":\"a\",\"title\":\"T\",\"attention_tier\":\"tier0\","
      "\"danger_class\":\"meh\",\"surface\":\"s\",\"handler\":\"h\"}"));
  XR_EXPECT_MSG(!bad_danger.ok, "out-of-enum danger_class rejected");

  // empty string field => reject.
  DescriptorResult empty = ValidateDescriptor(Obj(
      "{\"id\":\"\",\"title\":\"T\",\"attention_tier\":\"tier0\","
      "\"danger_class\":\"safe\",\"surface\":\"s\",\"handler\":\"h\"}"));
  XR_EXPECT_MSG(!empty.ok, "empty string field rejected");

  // not an object => reject.
  XR_EXPECT_MSG(!ValidateDescriptor(Obj("[1,2]")).ok, "non-object rejected");
}

}  // namespace

int main() {
  TestAgainstFrozenSchema();
  TestBattery();
  return xrtest::Report("test_descriptor");
}
