// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Multi-identity window fixture (P9-T1). Provides a second top-level window
// bound to a distinct identity so isolation-matrix browser rows can be
// scripted. Farm-executed; lint-only in this sandbox (no Chromium checkout).
//
// Attach points at pin d04cdb24d67b081f6cf80200ffc5233f44b61109:
//   content/public/test/browser_test_base.h:83  SetUpOnMainThread()
//   content/browser/web_contents/web_contents_impl.h  (WebContents host)
#ifndef XR_TEST_BROWSER_FIXTURES_MULTI_IDENTITY_WINDOW_H_
#define XR_TEST_BROWSER_FIXTURES_MULTI_IDENTITY_WINDOW_H_

#include <memory>
#include <string>

#include "test/browser/fixtures/xr_browser_test_base.h"

namespace content {
class WebContents;
}

namespace xr::test {

// Owns a second browser window opened for a named identity. The identity id
// is opaque to the fixture: the farm test supplies it and asserts the
// partition seam through chrome://process-internals parses (plan §11.4),
// never by reaching into the resolver's internals.
class MultiIdentityWindow {
 public:
  MultiIdentityWindow();
  ~MultiIdentityWindow();

  MultiIdentityWindow(const MultiIdentityWindow&) = delete;
  MultiIdentityWindow& operator=(const MultiIdentityWindow&) = delete;

  // Opens a new window for |identity_id| and returns its WebContents.
  content::WebContents* OpenForIdentity(const std::string& identity_id);

  // Closes the window, then blocks until the last renderer for it is gone.
  void CloseAndWait();

  // The identity this window was opened for (empty when not open).
  const std::string& identity_id() const { return identity_id_; }

  bool is_open() const { return web_contents_ != nullptr; }

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  std::string identity_id_;
  content::WebContents* web_contents_ = nullptr;
};

}  // namespace xr::test

#endif  // XR_TEST_BROWSER_FIXTURES_MULTI_IDENTITY_WINDOW_H_
