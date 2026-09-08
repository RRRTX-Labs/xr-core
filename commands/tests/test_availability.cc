// Copyright 2026 RRRTX Labs
// test_availability.cc — predicate flips, pinned-snapshot TOCTOU semantics,
// and unknown-predicate deny-default.
#include <string>
#include <vector>

#include "commands/core/availability.h"
#include "commands/core/json.h"
#include "commands/tests/harness.h"

using namespace xr::commands;

namespace {

JsonValue Snap(bool dial_writable, const std::vector<std::string>& caps,
               const std::string& active) {
  JsonValue::Array ca;
  for (const auto& c : caps) ca.push_back(JsonValue(c));
  JsonValue::Object o = {
      {"version", JsonValue(static_cast<int64_t>(1))},
      {"dial_writable", JsonValue(dial_writable)},
      {"capabilities", JsonValue(ca)},
      {"active_identity", JsonValue(active)},
  };
  return JsonValue(o);
}

}  // namespace

int main() {
  Availability a;

  // Registered set + known-check.
  XR_EXPECT(a.KnownPredicate("always"));
  XR_EXPECT(a.KnownPredicate("tor.engine-ready"));
  XR_EXPECT(!a.KnownPredicate("no.such.predicate"));

  // Predicate flips.
  {
    JsonValue s = Snap(true, {}, "xr:1");
    XR_EXPECT(a.Evaluate("policy.trust-dial-writable", s).available);
    s = Snap(false, {}, "xr:1");
    AvailabilityVerdict v = a.Evaluate("policy.trust-dial-writable", s);
    XR_EXPECT(!v.available);
    XR_EXPECT(!v.reason.empty());  // disabled WITH a reason (never absent)
  }
  {
    JsonValue s = Snap(true, {"tor.engine"}, "");
    XR_EXPECT(a.Evaluate("tor.engine-ready", s).available);
    s = Snap(true, {}, "");  // no tor.engine capability
    AvailabilityVerdict v = a.Evaluate("tor.engine-ready", s);
    XR_EXPECT(!v.available);
    XR_EXPECT(v.reason.find("P31") != std::string::npos);  // placeholder reason
  }
  {
    JsonValue s = Snap(true, {}, "xr:main");
    XR_EXPECT(a.Evaluate("identity.active", s).available);
    s = Snap(true, {}, "");
    XR_EXPECT(!a.Evaluate("identity.active", s).available);
  }
  {
    JsonValue s = Snap(false, {}, "");
    XR_EXPECT(a.Evaluate("always", s).available);  // always, regardless
  }
  // Unknown predicate => deny-default (never a guess).
  {
    JsonValue s = Snap(true, {"tor.engine"}, "xr:1");
    AvailabilityVerdict v = a.Evaluate("bogus.predicate", s);
    XR_EXPECT(!v.available);
    XR_EXPECT(v.reason.find("unknown predicate") != std::string::npos);
  }

  // Pinned-snapshot TOCTOU: an in-flight evaluation keeps the pinned version's
  // verdict even when the live cache is invalidated mid-flight.
  {
    JsonValue live = Snap(true, {}, "xr:1");        // version V1
    JsonValue pinned = live;                         // PIN at V1 (by value)
    AvailabilityVerdict in_flight = a.Evaluate("policy.trust-dial-writable", pinned);
    // A newer version arrives and invalidates the live cache:
    live = Snap(false, {}, "xr:1");                  // version V2
    // The pinned V1 verdict is stable for the in-flight evaluation:
    AvailabilityVerdict recheck = a.Evaluate("policy.trust-dial-writable", pinned);
    XR_EXPECT_MSG(in_flight.available && recheck.available,
                  "pinned V1 verdict stable under mid-flight invalidation");
    // A FRESH evaluation against the live V2 snapshot reflects the new state:
    XR_EXPECT(!a.Evaluate("policy.trust-dial-writable", live).available);
  }
  return xrtest::Report("test_availability");
}
