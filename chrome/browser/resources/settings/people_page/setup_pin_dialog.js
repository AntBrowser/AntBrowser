// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-setup-pin-dialog' is the settings page for choosing a PIN.
 *
 * Example:
 * * <settings-setup-pin-dialog set-modes="[[quickUnlockSetModes]]">
 * </settings-setup-pin-dialog>
 */

(function() {
'use strict';

Polymer({
  is: 'settings-setup-pin-dialog',

  behaviors: [I18nBehavior],

  properties: {
    /**
     * Reflects property set in password_prompt_dialog.js.
     * @type {?Object}
     */
    setModes: {
      type: Object,
      notify: true,
    },

    /**
     * Should the step-specific submit button be displayed?
     * @private
     */
    enableSubmit_: Boolean,

    /**
     * The current step/subpage we are on.
     * @private
     */
    isConfirmStep_: {type: Boolean, value: false},
  },

  /** @override */
  attached: function() {
    this.$.dialog.showModal();
    this.$.pinKeyboard.focus();
  },

  close: function() {
    if (this.$.dialog.open)
      this.$.dialog.close();

    this.$.pinKeyboard.resetState();
  },


  /** @private */
  onCancelTap_: function() {
    this.$.pinKeyboard.resetState();
    this.$.dialog.close();
  },

  /** @private */
  onPinSubmit_: function() {
    this.$.pinKeyboard.doSubmit();
  },


  /** @private */
  onSetPinDone_: function() {
    if (this.$.dialog.open)
      this.$.dialog.close();
  },

  /**
   * @private
   * @param {boolean} isConfirmStep
   * @return {string}
   */
  getTitleMessage_: function(isConfirmStep) {
    return this.i18n(
        isConfirmStep ? 'configurePinConfirmPinTitle' :
                        'configurePinChoosePinTitle');
  },

  /**
   * @private
   * @param {boolean} isConfirmStep
   * @return {string}
   */
  getContinueMessage_: function(isConfirmStep) {
    return this.i18n(isConfirmStep ? 'confirm' : 'configurePinContinueButton');
  },
});

})();
