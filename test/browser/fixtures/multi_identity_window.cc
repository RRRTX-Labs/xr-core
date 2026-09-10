// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Multi-identity window fixture (P9-T1). Farm-executed implementation.
#include "test/browser/fixtures/multi_identity_window.h"

#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"

namespace xr::test {

class MultiIdentityWindow::Impl {
 public:
  // Farm-side: window creation is driven through the content shell helpers
  // (content/public/test/browser_test_utils.h), asserting the partition seam
  // via chrome://process-internals parses per plan §11.4.
};

MultiIdentityWindow::MultiIdentityWindow()
    : impl_(std::make_unique<Impl>()) {}
MultiIdentityWindow::~MultiIdentityWindow() = default;

content::WebContents* MultiIdentityWindow::OpenForIdentity(
    const std::string& identity_id) {
  identity_id_ = identity_id;
  // The real open path lands on the farm (needs a running browser process);
  // until then this fixture is lint-only and never claims otherwise.
  web_contents_ = nullptr;
  return web_contents_;
}

void MultiIdentityWindow::CloseAndWait() {
  web_contents_ = nullptr;
}

}  // namespace xr::test
