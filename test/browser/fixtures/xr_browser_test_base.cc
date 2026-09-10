// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// XR browser-test fixture base (P9-T1). Farm-executed; this file ships so the
// farm has a real, reviewed implementation — it is NOT compiled in this
// sandbox (no Chromium checkout), and the P9 gate records that as a visible
// SKIP rather than a simulated PASS.
#include "test/browser/fixtures/xr_browser_test_base.h"

#include "base/command_line.h"
#include "content/public/test/browser_test_utils.h"

namespace xr::test {

XrBrowserTestBase::XrBrowserTestBase() = default;
XrBrowserTestBase::~XrBrowserTestBase() = default;

void XrBrowserTestBase::SetUpCommandLine(base::CommandLine* command_line) {
  // Browser-test attach point (browser_test_base.h:90). Subclasses append
  // their own flags; this base forbids nothing by default.
  content::ContentBrowserTest::SetUpCommandLine(command_line);
}

void XrBrowserTestBase::SetUpInProcessBrowserTestFixture() {
  // browser_test_base.h:133 — fixtures install here, before the browser
  // process is spun up.
  content::ContentBrowserTest::SetUpInProcessBrowserTestFixture();
}

void XrBrowserTestBase::SetUpOnMainThread() {
  // browser_test_base.h:83.
  content::ContentBrowserTest::SetUpOnMainThread();
}

void XrBrowserTestBase::TearDownOnMainThread() {
  content::ContentBrowserTest::TearDownOnMainThread();
}

}  // namespace xr::test
