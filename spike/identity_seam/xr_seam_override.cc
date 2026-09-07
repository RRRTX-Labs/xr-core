// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// SPIKE KIT (P4) — not a product component. See README.md before use.

#include "chrome/browser/xr/xr_seam_override.h"

#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "url/gurl.h"

namespace xr {

namespace {

// Sites whose partition is decided by the site itself, independent of any
// identity. Kept in one place so a reviewer can see the whole policy.
// Anything not matched here falls through to the default partition.
bool IsSchemeScopedPartition(const GURL& site) {
  return site.SchemeIs("chrome-extension") ||
         site.SchemeIs("isolated-app") || site.SchemeIs("chrome");
}

}  // namespace

content::StoragePartitionConfig GetStoragePartitionConfigForSite(
    content::BrowserContext* browser_context,
    const GURL& site) {
  Profile* profile = Profile::FromBrowserContext(browser_context);

  // No opinion: return the profile default. This is the common case, and it
  // is deliberately NOT where identity is applied — see the header comment.
  // The identity partition is fixed at SiteInstance creation time instead
  // (xr::SiteInstanceForIdentity), which is the only point at which "which
  // tab is this?" is answerable.
  if (!IsSchemeScopedPartition(site)) {
    return content::StoragePartitionConfig::CreateDefault(profile);
  }

  // Scheme-scoped cases are left to upstream's own routing: returning the
  // default here preserves Chrome's behaviour for extensions / IWA / WebUI
  // rather than re-deriving it in a spike.
  return content::StoragePartitionConfig::CreateDefault(profile);
}

}  // namespace xr
