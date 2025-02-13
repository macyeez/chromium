// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_FACTORY_H_
#define CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_FACTORY_H_

#include <memory>

#include "base/macros.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace base {
template <typename T>
struct DefaultSingletonTraits;
}  // namespace base

namespace browser_sync {
class ChromeSyncClient;
class ProfileSyncService;
}  // namespace browser_sync

namespace syncer {
class SyncService;
}  // namespace syncer

class ProfileSyncServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  using SyncClientFactory =
      base::RepeatingCallback<std::unique_ptr<browser_sync::ChromeSyncClient>(
          Profile*)>;

  // Returns the SyncService for the given profile.
  static syncer::SyncService* GetForProfile(Profile* profile);
  // TODO(crbug.com/924508): This is identical to GetForProfile; remove it.
  static syncer::SyncService* GetSyncServiceForProfile(Profile* profile);
  // Returns the ProfileSyncService for the given profile. DO NOT USE unless
  // absolutely necessary! Prefer GetForProfile instead.
  static browser_sync::ProfileSyncService* GetAsProfileSyncServiceForProfile(
      Profile* profile);
  static bool HasProfileSyncService(Profile* profile);

  static ProfileSyncServiceFactory* GetInstance();

  // Overrides how the SyncClient is created for testing purposes.
  static void SetSyncClientFactoryForTest(SyncClientFactory* client_factory);

 private:
  friend struct base::DefaultSingletonTraits<ProfileSyncServiceFactory>;

  ProfileSyncServiceFactory();
  ~ProfileSyncServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;

  // A factory function for overriding the way the SyncClient is created.
  // This is a raw pointer so it can be statically initialized.
  static SyncClientFactory* client_factory_;

  DISALLOW_COPY_AND_ASSIGN(ProfileSyncServiceFactory);
};

#endif  // CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_FACTORY_H_
