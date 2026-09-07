// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// SPIKE KIT (P4) — not a product component. See README.md before use.

#ifndef CHROME_BROWSER_XR_XR_IDENTITY_H_
#define CHROME_BROWSER_XR_XR_IDENTITY_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents_user_data.h"

class GURL;
class Profile;

namespace content {
class SiteInstance;
class WebContents;
}  // namespace content

namespace xr {

// The partition-domain prefix every XR identity lives under. Storage keys are
// `xr:<uuid>`; the prefix is what makes them sort outside any upstream domain
// and keeps them greppable in a StoragePartitionConfig dump.
inline constexpr char kPartitionDomainPrefix[] = "xr:";

// An identity: one storage partition domain inside one Profile.
struct Identity {
  // Stable, random UUIDv4. Never derived from a name, a URL or a user string:
  // a guessable partition domain is a cross-identity correlation primitive.
  std::string id;

  // Partition domain handed to StoragePartitionConfig::Create().
  std::string partition_domain() const;

  // Ephemeral identities resolve to an in-memory partition (no disk writes).
  bool ephemeral = false;

  // Partition name within the domain. Empty = the domain's default partition,
  // which is what a top-level identity tab wants.
  std::string partition_name;

  bool operator==(const Identity& other) const = default;
};

// The storage-partition configuration for `identity` under `profile`.
//
// NOTE (measured at pin d04cdb24…, see docs/spike-identity/
// measured-shared-state.md row S-02): when `profile` is off-the-record the
// returned config is in-memory regardless of `identity.ephemeral`, because
// content/browser/browser_context.cc:144-151 CHECKs it.
content::StoragePartitionConfig StoragePartitionConfigForIdentity(
    Profile* profile,
    const Identity& identity);

// The SiteInstance a new identity tab must start in.
//
// This is the seam: content/public/browser/site_instance.h:252-256
// `SiteInstance::CreateForFixedStoragePartition(browser_context, url,
// partition_config)` creates a SiteInstance in a new BrowsingInstance whose
// StoragePartition "is preserved across navigations". Every later SiteInstance
// in that BrowsingInstance inherits the config (browsing_instance.cc:264-267)
// and a mismatch is CHECK-failed (browsing_instance.cc:183).
//
// Returns nullptr when `identity` is not usable (bad domain, OTR mismatch) —
// callers must fall back to the default tab SiteInstance, never CHECK.
scoped_refptr<content::SiteInstance> SiteInstanceForIdentity(
    Profile* profile,
    const GURL& url,
    const Identity& identity);

// Which identity a new tab should open in, if any.
//
// SPIKE SCAFFOLD — clearly non-production. A real implementation reads the
// identity from the navigation's own context (a P5 `IdentityProvisioning`
// request), not from process-local state. This exists so the probes can ask
// "open this tab as identity X" without first designing that request surface;
// see ADR-0042 ("Contracts out") for what P5 inherits from this.
std::optional<Identity> IdentityForNewTab(Profile* profile, const GURL& url);

// Sets the identity the next new tab will request (and consumes it).
// Test-only: no production caller may depend on this.
void SetPendingIdentityForNewTabForTesting(std::optional<Identity> identity);

// Per-tab record of which identity a WebContents belongs to.
//
// Timing (the spike's #1 risk, resolved): the SiteInstance that fixes the
// partition is built BEFORE WebContents::Create, so the partition is already
// decided when the first navigation starts. This UserData is attached
// immediately after Create() and is therefore only a *record* for later
// lookups — it is not what establishes the partition. Depending on it for
// isolation would be a race; see README.md "What this spike disproves".
class XRIdentityTabData
    : public content::WebContentsUserData<XRIdentityTabData> {
 public:
  XRIdentityTabData(const XRIdentityTabData&) = delete;
  XRIdentityTabData& operator=(const XRIdentityTabData&) = delete;
  ~XRIdentityTabData() override;

  const Identity& identity() const { return identity_; }
  const content::StoragePartitionConfig& partition_config() const {
    return partition_config_;
  }

  // True when the tab's live SiteInstance still resolves to the recorded
  // partition. A false result is a seam break, not a benign drift: it means
  // the tab has moved to a different StoragePartition.
  bool PartitionMatchesLiveSiteInstance() const;

 private:
  friend class content::WebContentsUserData<XRIdentityTabData>;

  XRIdentityTabData(content::WebContents* contents,
                    Identity identity,
                    content::StoragePartitionConfig config);

  Identity identity_;
  content::StoragePartitionConfig partition_config_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

// Test/spike-only registry: the identities this browser instance knows about.
// Deliberately in-memory and process-local — a real implementation stores
// identity metadata encrypted in the Profile (P5/P12 work), and a spike must
// not invent that storage.
class IdentityRegistry {
 public:
  static IdentityRegistry& Get();

  // Registers `identity`. Returns false (and registers nothing) when the id is
  // malformed or already taken.
  bool Register(Identity identity);

  // Look up by id; nullopt when unknown.
  std::optional<Identity> Lookup(const std::string& id) const;

  std::vector<Identity> All() const;

  // Test seam: drops every identity. Not exposed to production callers.
  void ClearForTesting();

 private:
  IdentityRegistry();
  std::vector<Identity> identities_;
};

}  // namespace xr

#endif  // CHROME_BROWSER_XR_XR_IDENTITY_H_
