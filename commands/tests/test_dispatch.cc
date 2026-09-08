// Copyright 2026 RRRTX Labs
// test_dispatch.cc — the dispatch security edge: source-tag allowlist, id
// whitelist, danger-class confirmation, and ledger rows for every reject (L6).
#include <string>

#include "commands/core/dispatch.h"
#include "commands/core/registry.h"
#include "commands/tests/harness.h"

using namespace xr::commands;

namespace {

Registry MakeRegistry() {
  Registry r;
  auto add = [&](const char* id, const char* danger) {
    Command c;
    c.id = id;
    c.title = id;
    c.attention_tier = "tier2";
    c.danger_class = danger;
    c.surface = "command-palette";
    c.handler = std::string("action.") + id;
    c.predicate_id = "always";
    r.Register(c);
  };
  add("safe.cmd", "safe");
  add("caution.cmd", "caution");
  add("destructive.cmd", "destructive");
  return r;
}

bool HasLedgerRow(const std::vector<std::string>& rows, const std::string& needle) {
  for (const auto& r : rows)
    if (r.find(needle) != std::string::npos) return true;
  return false;
}

}  // namespace

int main() {
  Registry reg = MakeRegistry();
  Dispatcher d(reg);

  // Source-tag allowlist: every whitelisted source invokes a safe known id.
  const char* allowed[] = {"ui-chrome", "palette", "menu", "shortcut", "test"};
  for (const char* s : allowed) {
    InvokeOutcome o = d.Invoke("safe.cmd", s, false);
    XR_EXPECT_MSG(o.authorized && o.status == "authorized",
                  (std::string("whitelisted source ") + s + " authorized").c_str());
    XR_EXPECT_STREQ(o.handler.c_str(), "action.safe.cmd");
  }

  // page-originated => rejected + ledger row (cross-process origin check).
  {
    InvokeOutcome o = d.Invoke("safe.cmd", "page", false);
    XR_EXPECT(!o.authorized);
    XR_EXPECT_STREQ(o.status.c_str(), "rejected");
    XR_EXPECT(o.reason.find("page-originated") != std::string::npos);
    XR_EXPECT(HasLedgerRow(o.ledger_rows, "invoke-reject"));
    XR_EXPECT(HasLedgerRow(o.ledger_rows, "page"));
    XR_EXPECT(!o.handler.empty() == false);  // no handler reaches
  }
  // Unknown / non-whitelisted source => rejected + ledger row.
  {
    InvokeOutcome o = d.Invoke("safe.cmd", "evil-agent", false);
    XR_EXPECT(!o.authorized);
    XR_EXPECT_STREQ(o.status.c_str(), "rejected");
    XR_EXPECT(o.reason.find("evil-agent") != std::string::npos);
    XR_EXPECT(HasLedgerRow(o.ledger_rows, "invoke-reject"));
  }
  // Unknown id => rejected + ledger row (whitelist).
  {
    InvokeOutcome o = d.Invoke("nope.nope", "palette", false);
    XR_EXPECT(!o.authorized);
    XR_EXPECT_STREQ(o.status.c_str(), "rejected");
    XR_EXPECT(o.reason.find("unknown command id") != std::string::npos);
    XR_EXPECT(HasLedgerRow(o.ledger_rows, "unknown"));
  }

  // Danger-class confirmation gate (L7 friction).
  {
    InvokeOutcome no_confirm = d.Invoke("destructive.cmd", "palette", false);
    XR_EXPECT(!no_confirm.authorized);
    XR_EXPECT_STREQ(no_confirm.status.c_str(), "confirmation-required");
    XR_EXPECT_STREQ(no_confirm.danger_class.c_str(), "destructive");
    XR_EXPECT(HasLedgerRow(no_confirm.ledger_rows, "invoke-confirm-required"));

    InvokeOutcome confirmed = d.Invoke("destructive.cmd", "palette", true);
    XR_EXPECT(confirmed.authorized);
    XR_EXPECT_STREQ(confirmed.status.c_str(), "authorized");
  }
  // caution / safe do NOT hard-block (no confirmation required).
  {
    XR_EXPECT(d.Invoke("caution.cmd", "palette", false).authorized);
    XR_EXPECT(d.Invoke("safe.cmd", "palette", false).authorized);
  }

  // Ledger accumulates (L6) with a monotonic seq (no clock => deterministic).
  {
    Registry r2 = MakeRegistry();
    Dispatcher d2(r2);
    d2.Invoke("safe.cmd", "page", false);
    d2.Invoke("nope", "palette", false);
    const auto& L = d2.ledger();
    XR_EXPECT_EQ(L.size(), 2);
    // seq 0 then 1, in canonical (sorted-key) JSON.
    XR_EXPECT(L[0].find("\"seq\":0") != std::string::npos);
    XR_EXPECT(L[1].find("\"seq\":1") != std::string::npos);
  }
  return xrtest::Report("test_dispatch");
}
