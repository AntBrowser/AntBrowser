// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

suite('RuntimeHostsDialog', function() {
  /** @type {extensions.RuntimeHostsDialogElement} */ let dialog;
  /** @type {extensions.TestService} */ let delegate;
  const ITEM_ID = 'a'.repeat(32);

  setup(function() {
    PolymerTest.clearBody();
    dialog = document.createElement('extensions-runtime-hosts-dialog');

    delegate = new extensions.TestService();
    dialog.delegate = delegate;
    dialog.itemId = ITEM_ID;

    document.body.appendChild(dialog);
  });

  teardown(function() {
    dialog.remove();
  });

  test('valid input', function() {
    const input = dialog.$$('cr-input');
    const site = 'http://www.example.com';
    input.value = site;
    input.fire('input');
    assertFalse(input.invalid);

    const submit = dialog.$.submit;
    assertFalse(submit.disabled);
    submit.click();
    return delegate.whenCalled('addRuntimeHostPermission').then((args) => {
      let id = args[0];
      let input = args[1];
      assertEquals(ITEM_ID, id);
      assertEquals('http://www.example.com/*', input);
    });
  });

  test('invalid input', function() {
    // Initially the action button should be disabled, but the error warning
    // should not be shown for an empty input.
    const input = dialog.$$('cr-input');
    assertFalse(input.invalid);
    const submit = dialog.$.submit;
    assertTrue(submit.disabled);

    // Simulate user input of invalid text.
    const invalidSite = 'foobar';
    input.value = invalidSite;
    input.fire('input');
    assertTrue(input.invalid);
    assertTrue(submit.disabled);

    // Entering valid text should clear the error and enable the submit button.
    input.value = 'http://www.example.com';
    input.fire('input');
    assertFalse(input.invalid);
    assertFalse(submit.disabled);
  });

  test('delegate indicates invalid input', function() {
    delegate.acceptRuntimeHostPermission = false;

    const input = dialog.$$('cr-input');
    const site = 'http://....a';
    input.value = site;
    input.fire('input');
    assertFalse(input.invalid);

    const submit = dialog.$.submit;
    assertFalse(submit.disabled);
    submit.click();
    return delegate.whenCalled('addRuntimeHostPermission').then(() => {
      assertTrue(input.invalid);
      assertTrue(submit.disabled);
    });
  });

  test('editing current entry', function() {
    const oldPattern = 'http://example.com/*';
    const newPattern = 'http://chromium.org/*';

    dialog.currentSite = oldPattern;
    const input = dialog.$$('cr-input');
    input.value = newPattern;
    input.fire('input');
    const submit = dialog.$.submit;

    submit.click();
    return delegate.whenCalled('removeRuntimeHostPermission')
        .then((args) => {
          expectEquals(ITEM_ID, args[0] /* id */);
          expectEquals(oldPattern, args[1] /* pattern */);
          return delegate.whenCalled('addRuntimeHostPermission');
        })
        .then((args) => {
          expectEquals(ITEM_ID, args[0] /* id */);
          expectEquals(newPattern, args[1] /* pattern */);
        });
  });

  test('get pattern from url', function() {
    expectEquals(
        'https://example.com/*',
        extensions.getPatternFromSite('https://example.com/*'));
    expectEquals(
        'https://example.com/*',
        extensions.getPatternFromSite('https://example.com/'));
    expectEquals(
        'https://example.com/*',
        extensions.getPatternFromSite('https://example.com'));
    expectEquals(
        'https://*.example.com/*',
        extensions.getPatternFromSite('https://*.example.com/*'));
    expectEquals(
        '*://example.com/*', extensions.getPatternFromSite('example.com'));
    expectEquals(
        'https://example.com:80/*',
        extensions.getPatternFromSite('https://example.com:80/*'));
  });
});
