// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'crostini-subpage' is the settings subpage for managing Crostini.
 */

Polymer({
  is: 'settings-crostini-subpage',

  behaviors: [PrefsBehavior],

  properties: {
    /** Preferences state. */
    prefs: {
      type: Object,
      notify: true,
    },

    /**
     * Whether CrostiniUsbSupport flag is enabled.
     * @private {boolean}
     */
    enableCrostiniUsbDeviceSupport_: {
      type: Boolean,
      value: function() {
        return loadTimeData.getBoolean('enableCrostiniUsbDeviceSupport');
      },
    },

    /**
     * Whether export / import UI should be displayed.
     * @private {boolean}
     */
    showCrostiniExportImport_: {
      type: Boolean,
      value: function() {
        return loadTimeData.getBoolean('showCrostiniExportImport');
      },
    },

    /**
     * Whether the uninstall options should be displayed.
     * @private {boolean}
     */
    hideCrostiniUninstall_: {
      type: Boolean,
    },
  },

  observers: ['onCrostiniEnabledChanged_(prefs.crostini.enabled.value)'],

  created: function() {
    const callback = (status) => {
      this.hideCrostiniUninstall_ = status;
    };
    cr.addWebUIListener('crostini-installer-status-changed', callback);
    cr.sendWithPromise('requestCrostiniInstallerStatus').then(callback);
  },

  /** @private */
  onCrostiniEnabledChanged_: function(enabled) {
    if (!enabled &&
        settings.getCurrentRoute() == settings.routes.CROSTINI_DETAILS) {
      settings.navigateToPreviousRoute();
    }
  },

  /**
   * Shows a confirmation dialog when removing crostini.
   * @param {!Event} event
   * @private
   */
  onRemoveTap_: function(event) {
    settings.CrostiniBrowserProxyImpl.getInstance().requestRemoveCrostini();
  },

  /** @private */
  onSharedPathsTap_: function(event) {
    settings.navigateTo(settings.routes.CROSTINI_SHARED_PATHS);
  },

  /** @private */
  onExportClick_: function(event) {
    settings.CrostiniBrowserProxyImpl.getInstance().exportCrostiniContainer();
  },

  /** @private */
  onImportClick_: function(event) {
    settings.CrostiniBrowserProxyImpl.getInstance().importCrostiniContainer();
  },

  /** @private */
  onSharedUsbDevicesTap_: function(event) {
    settings.navigateTo(settings.routes.CROSTINI_SHARED_USB_DEVICES);
  },
});
