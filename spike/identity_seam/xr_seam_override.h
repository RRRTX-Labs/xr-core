// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// SPIKE KIT (P4) — not a product component. See README.md before use.

#ifndef CHROME_BROWSER_XR_XR_SEAM_OVERRIDE_H_
#define CHROME_BROWSER_XR_XR_SEAM_OVERRIDE_H_

#include "content/public/browser/storage_partition_config.h"

class GURL;

namespace content {
class BrowserContext;
}  // namespace content

namespace xr {

// The embedder consult for a storage partition, evaluated for a site.
//
// IMPORTANT — this is NOT the identity seam, and the spike exists partly to
// prove that it cannot be. Its signature is
// (BrowserContext*, const GURL& site): it has no tab, no SiteInstance and no
// identity in scope, so it must return the SAME config for every tab of a
// profile visiting the same site. Two identities browsing `example.com` in one
// profile are indistinguishable here (measured at pin d04cdb24…,
// content/public/browser/content_browser_client.h:1208; see
// docs/spike-identity/measured-shared-state.md row S-01).
//
// What it IS for: scheme-level partition decisions (extension / IWA / WebUI),
// exactly the cases upstream Chrome already handles. It is wired in the kit so
// the farm can measure the collision directly rather than take our word for it.
//
// Returns the default config when XR has no opinion, so callers keep upstream
// behaviour unless a decision is genuinely made.
content::StoragePartitionConfig GetStoragePartitionConfigForSite(
    content::BrowserContext* browser_context,
    const GURL& site);

}  // namespace xr

#endif  // CHROME_BROWSER_XR_XR_SEAM_OVERRIDE_H_
