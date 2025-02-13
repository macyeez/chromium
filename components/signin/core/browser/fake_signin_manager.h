// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SIGNIN_CORE_BROWSER_FAKE_SIGNIN_MANAGER_H_
#define COMPONENTS_SIGNIN_CORE_BROWSER_FAKE_SIGNIN_MANAGER_H_

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "build/build_config.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/signin/core/browser/signin_metrics.h"

// SigninManager to use for testing.

#if !defined(OS_CHROMEOS)

// A signin manager that bypasses actual authentication routines with servers
// and accepts the credentials provided to StartSignIn.
class FakeSigninManager : public SigninManager {
 public:
  FakeSigninManager(SigninClient* client,
                    ProfileOAuth2TokenService* token_service,
                    AccountTrackerService* account_tracker_service,
                    GaiaCookieManagerService* cookie_manager_service);

  FakeSigninManager(SigninClient* client,
                    ProfileOAuth2TokenService* token_service,
                    AccountTrackerService* account_tracker_service,
                    GaiaCookieManagerService* cookie_manager_service,
                    signin::AccountConsistencyMethod account_consistency);

  ~FakeSigninManager() override;
};

#endif  // !defined (OS_CHROMEOS)

#endif  // COMPONENTS_SIGNIN_CORE_BROWSER_FAKE_SIGNIN_MANAGER_H_
