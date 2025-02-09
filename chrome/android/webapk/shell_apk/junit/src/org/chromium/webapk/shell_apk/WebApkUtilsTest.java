// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.webapk.shell_apk;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.os.Bundle;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.Shadows;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowPackageManager;

import org.chromium.testing.local.LocalRobolectricTestRunner;
import org.chromium.webapk.lib.common.WebApkMetaDataKeys;

/** Tests for WebApkUtils. */
@RunWith(LocalRobolectricTestRunner.class)
@Config(manifest = Config.NONE, packageName = WebApkUtilsTest.WEBAPK_PACKAGE_NAME)
public class WebApkUtilsTest {
    protected static final String WEBAPK_PACKAGE_NAME = "org.chromium.webapk.test_package";

    private Context mContext;
    private ShadowPackageManager mPackageManager;

    @Before
    public void setUp() {
        mContext = RuntimeEnvironment.application;
        mPackageManager = Shadows.shadowOf(mContext.getPackageManager());
    }

    /**
     * Test that MainActivity appends the start URL as a paramater if |loggedIntentUrlParam| in
     * WebAPK metadata is set and {@link intentStartUrl} is outside of the scope specified in the
     * manifest meta data.
     */
    @Test
    public void testLoggedIntentUrlParamWhenRewriteOutOfScope() {
        final String intentStartUrl = "https://maps.google.com/page?a=A";
        final String manifestStartUrl = "https://www.google.com/maps";
        final String manifestScope = "https://www.google.com";
        final String expectedRewrittenStartUrl =
                "https://www.google.com/maps?originalUrl=https%3A%2F%2Fmaps.google.com%2Fpage%3Fa%3DA";
        final String browserPackageName = "browser.support.webapks";

        Bundle bundle = new Bundle();
        bundle.putString(WebApkMetaDataKeys.START_URL, manifestStartUrl);
        bundle.putString(WebApkMetaDataKeys.SCOPE, manifestScope);
        bundle.putString(WebApkMetaDataKeys.RUNTIME_HOST, browserPackageName);
        bundle.putString(WebApkMetaDataKeys.LOGGED_INTENT_URL_PARAM, "originalUrl");

        Assert.assertEquals(expectedRewrittenStartUrl,
                WebApkUtils.rewriteIntentUrlIfNecessary(intentStartUrl, bundle));
    }

    /**
     * Test that MainActivity appends the start URL as a paramater if |loggedIntentUrlParam| in
     * WebAPK metadata is set and {@link intentStartUrl} is in the scope specified in the manifest
     * meta data.
     */
    @Test
    public void testLoggedIntentUrlParamWhenRewriteInScope() {
        final String intentStartUrl = "https://www.google.com/maps/search/A";
        final String manifestStartUrl = "https://www.google.com/maps?force=qVTs2FOxxTmHHo79-pwa";
        final String manifestScope = "https://www.google.com";
        final String expectedRewrittenStartUrl =
                "https://www.google.com/maps?force=qVTs2FOxxTmHHo79-pwa&intent="
                + "https%3A%2F%2Fwww.google.com%2Fmaps%2Fsearch%2FA";
        final String browserPackageName = "browser.support.webapks";

        Bundle bundle = new Bundle();
        bundle.putString(WebApkMetaDataKeys.START_URL, manifestStartUrl);
        bundle.putString(WebApkMetaDataKeys.SCOPE, manifestScope);
        bundle.putString(WebApkMetaDataKeys.RUNTIME_HOST, browserPackageName);
        bundle.putString(WebApkMetaDataKeys.LOGGED_INTENT_URL_PARAM, "intent");

        Assert.assertEquals(expectedRewrittenStartUrl,
                WebApkUtils.rewriteIntentUrlIfNecessary(intentStartUrl, bundle));
    }

    /**
     * Tests that a WebAPK should be launched as a tab if Chrome's version number is lower than
     * {@link WebApkUtils#MINIMUM_REQUIRED_CHROME_VERSION}.
     */
    @Test
    public void testShouldLaunchInTabWhenChromeVersionIsTooLow() {
        String versionName = "56.0.0000.0";
        Assert.assertTrue(WebApkUtils.shouldLaunchInTab(versionName));
    }

    /**
     * Tests that a WebAPK should not be launched as a tab if Chrome's version is higher or equal to
     * {@link WebApkUtils#MINIMUM_REQUIRED_CHROME_VERSION}.
     */
    @Test
    public void testShouldNotLaunchInTabWithNewVersionOfChrome() {
        String versionName = "57.0.0000.0";
        Assert.assertFalse(WebApkUtils.shouldLaunchInTab(versionName));
    }

    /** Tests that a WebAPK should not be launched as a tab in a developer build of Chrome. */
    @Test
    public void testShouldNotLaunchInTabWithDevBuild() {
        String versionName = "Developer Build";
        Assert.assertFalse(WebApkUtils.shouldLaunchInTab(versionName));
    }

    /**
     * Tests that {@link WebApkUtils#isInstalled} returns false for an installed but disabled app.
     */
    @Test
    public void testReturnFalseForInstalledButDisabledApp() {
        String packageName = "com.chrome.beta";
        PackageInfo info = new PackageInfo();
        info.packageName = packageName;
        info.applicationInfo = new ApplicationInfo();
        info.applicationInfo.enabled = false;
        mPackageManager.addPackage(info);

        Assert.assertFalse(WebApkUtils.isInstalled(mContext.getPackageManager(), packageName));
    }
}
