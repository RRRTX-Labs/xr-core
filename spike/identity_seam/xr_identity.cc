// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// SPIKE KIT (P4) — not a product component. See README.md before use.

#include "chrome/browser/xr/xr_identity.h"

#include <utility>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/site_instance.h"
#include "url/gurl.h"

namespace xr {

namespace {

// A partition domain is `xr:` + a UUIDv4 (8-4-4-4-12 hex). Anything else is
// rejected: the domain is a storage-keying primitive and must not be
// attacker- or user-influenced text.
bool IsWellFormedDomain(const std::string& domain) {
  if (domain.compare(0, 3, kPartitionDomainPrefix) != 0) {
    return false;
  }
  const std::string uuid = domain.substr(3);
  if (uuid.size() != 36) {
    return false;
  }
  for (size_t i = 0; i < uuid.size(); ++i) {
    const size_t dash = (i == 8 || i == 13 || i == 18 || i == 23);
    const char c = uuid[i];
    if (dash) {
      if (c != '-') {
        return false;
      }
    } else if (!base::IsHexDigit(c)) {
      return false;
    }
  }
  return true;
}

bool IsWellFormedId(const std::string& id) {
  return IsWellFormedDomain(kPartitionDomainPrefix + id);
}

}  // namespace

std::string Identity::partition_domain() const {
  return kPartitionDomainPrefix + id;
}

content::StoragePartitionConfig StoragePartitionConfigForIdentity(
    Profile* profile,
    const Identity& identity) {
  CHECK(profile);
  // content/browser/browser_context.cc:144-151 CHECKs that an off-the-record
  // profile only ever gets in-memory partitions, so mirror that here instead
  // of letting the CHECK fire deeper in the stack.
  const bool in_memory = identity.ephemeral || profile->IsOffTheRecord();
  return content::StoragePartitionConfig::Create(
      profile, identity.partition_domain(), identity.partition_name,
      in_memory);
}

scoped_refptr<content::SiteInstance> SiteInstanceForIdentity(
    Profile* profile,
    const GURL& url,
    const Identity& identity) {
  if (!profile || !IsWellFormedId(identity.id)) {
    return nullptr;
  }
  const content::StoragePartitionConfig config =
      StoragePartitionConfigForIdentity(profile, identity);
  if (config.is_default()) {
    return nullptr;  // CreateForFixedStoragePartition CHECKs non-default.
  }
  return content::SiteInstance::CreateForFixedStoragePartition(profile, url,
                                                               config);
}

namespace {

// Spike-only, test-only: the identity the next new tab will request.
// A real implementation resolves this from the navigation request itself.
std::optional<Identity>& PendingIdentity() {
  static std::optional<Identity> pending;
  return pending;
}

}  // namespace

std::optional<Identity> IdentityForNewTab(Profile* profile, const GURL& url) {
  if (!profile) {
    return std::nullopt;
  }
  std::optional<Identity> pending = PendingIdentity();
  PendingIdentity().reset();  // consumed: one pending identity, one new tab.
  if (pending && !IsWellFormedId(pending->id)) {
    return std::nullopt;
  }
  return pending;
}

void SetPendingIdentityForNewTabForTesting(std::optional<Identity> identity) {
  PendingIdentity() = std::move(identity);
}

// --- XRIdentityTabData ------------------------------------------------------

XRIdentityTabData::XRIdentityTabData(
    content::WebContents* contents,
    Identity identity,
    content::StoragePartitionConfig config)
    : content::WebContentsUserData<XRIdentityTabData>(*contents),
      identity_(std::move(identity)),
      partition_config_(std::move(config)) {}

XRIdentityTabData::~XRIdentityTabData() = default;

bool XRIdentityTabData::PartitionMatchesLiveSiteInstance() const {
  content::SiteInstance* live = nullptr;
  if (auto* contents = GetWebContentsForTesting()) {
    live = contents->GetSiteInstance();
  }
  if (!live) {
    return false;
  }
  return live->GetStoragePartitionConfig() == partition_config_;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(XRIdentityTabData);

// --- IdentityRegistry -------------------------------------------------------

IdentityRegistry::IdentityRegistry() = default;

IdentityRegistry& IdentityRegistry::Get() {
  static base::NoDestructor<IdentityRegistry> instance;
  return *instance;
}

bool IdentityRegistry::Register(Identity identity) {
  if (!IsWellFormedId(identity.id)) {
    return false;
  }
  for (const Identity& existing : identities_) {
    if (existing.id == identity.id) {
      return false;
    }
  }
  identities_.push_back(std::move(identity));
  return true;
}

std::optional<Identity> IdentityRegistry::Lookup(const std::string& id) const {
  for (const Identity& identity : identities_) {
    if (identity.id == id) {
      return identity;
    }
  }
  return std::nullopt;
}

std::vector<Identity> IdentityRegistry::All() const {
  return identities_;
}

void IdentityRegistry::ClearForTesting() {
  identities_.clear();
}

}  // namespace xr
