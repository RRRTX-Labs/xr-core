// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// XR browser-test fixture base (P9-T1). These fixtures are the in-repo
// extension points the farm's browser_tests attach to; they are
// runner-ready here (lint + syntax-check only) because this sandbox has no
// Chromium checkout and no gn/ninja — nothing in this directory claims a
// compile result it did not produce.
//
// Upstream attach points, verified at pin d04cdb24d67b081f6cf80200ffc5233f44b61109:
//   content/public/test/browser_test_base.h:64     class BrowserTestBase
//   content/public/test/content_browser_test.h:51  class ContentBrowserTest
//   content/public/test/browser_test_base.h:83     SetUpOnMainThread()
//   content/public/test/browser_test_base.h:90     SetUpCommandLine()
//   content/public/test/browser_test_base.h:133    SetUpInProcessBrowserTestFixture()
#ifndef XR_TEST_BROWSER_FIXTURES_XR_BROWSER_TEST_BASE_H_
#define XR_TEST_BROWSER_FIXTURES_XR_BROWSER_TEST_BASE_H_

#include "content/public/test/content_browser_test.h"

namespace xr::test {

// Base for every XR browser test. Centralizes the identity-partition seam so
// no test hand-rolls its own StoragePartitionConfig domain (the same reason
// mode checks live in one resolver — one place, one law).
class XrBrowserTestBase : public content::ContentBrowserTest {
 public:
  XrBrowserTestBase();
  ~XrBrowserTestBase() override;

  // content::ContentBrowserTest:
  void SetUpCommandLine(base::CommandLine* command_line) override;
  void SetUpInProcessBrowserTestFixture() override;
  void SetUpOnMainThread() override;
  void TearDownOnMainThread() override;
};

}  // namespace xr::test

#endif  // XR_TEST_BROWSER_FIXTURES_XR_BROWSER_TEST_BASE_H_
