// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file contains base classes for fileManagerPrivate API.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_BASE_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_BASE_H_

#include "base/time/time.h"
#include "extensions/browser/extension_function.h"

namespace extensions {

// This class adds a logging feature to UIThreadExtensionFunction. Logging is
// done when sending the response to JavaScript, using drive::util::Log().
// API functions of fileManagerPrivate should inherit this class.
//
// By default, logging is turned off, hence sub classes should call
// set_log_on_completion(true) to enable it, if they want. However, even if
// the logging is turned off, a warning is emitted when a function call is
// very slow. See the implementation of OnResponded() for details.
class LoggedUIThreadExtensionFunction : public UIThreadExtensionFunction {
 public:
  LoggedUIThreadExtensionFunction();

 protected:
  ~LoggedUIThreadExtensionFunction() override;

  // UIThreadExtensionFunction overrides.
  void OnResponded() override;

  // Sets the logging on completion flag. By default, logging is turned off.
  void set_log_on_completion(bool log_on_completion) {
    log_on_completion_ = log_on_completion;
  }

 private:
  base::Time start_time_;
  bool log_on_completion_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_BASE_H_
