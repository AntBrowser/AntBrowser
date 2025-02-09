// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download.home.storage;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.TextView;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.download.home.filter.OfflineItemFilterSource;
import org.chromium.chrome.browser.modelutil.PropertyKey;
import org.chromium.chrome.browser.modelutil.PropertyModel;
import org.chromium.chrome.browser.modelutil.PropertyModel.ObjectPropertyKey;
import org.chromium.chrome.browser.modelutil.PropertyModelChangeProcessor;

/**
 * The coordinator responsible for creating the storage summary view in download home.
 */
public class StorageCoordinator {
    private final PropertyModel mModel = new PropertyModel(StorageProperties.ALL_KEYS);
    private final TextView mView;

    public StorageCoordinator(Context context, OfflineItemFilterSource filterSource) {
        mView = (TextView) LayoutInflater.from(context).inflate(
                R.layout.download_storage_summary, null);
        PropertyModelChangeProcessor.create(mModel, mView, this::bind);
        new StorageSummaryProvider(context, this::onStorageInfoUpdated, filterSource);
    }

    /** @return The {@link View} to be shown that contains the storage information. */
    public View getView() {
        return mView;
    }

    private void onStorageInfoUpdated(String storageInfoText) {
        mModel.setValue(StorageProperties.STORAGE_INFO_TEXT, storageInfoText);
    }

    private void bind(PropertyModel model, TextView view, PropertyKey propertyKey) {
        if (propertyKey == StorageProperties.STORAGE_INFO_TEXT) {
            view.setText(model.getValue(StorageProperties.STORAGE_INFO_TEXT));
        }
    }

    /** The properties needed to render the download home storage summary view. */
    private static class StorageProperties {
        /** The storage summary text to show in the content area. */
        public static final ObjectPropertyKey<String> STORAGE_INFO_TEXT = new ObjectPropertyKey<>();

        public static final PropertyKey[] ALL_KEYS = new PropertyKey[] {STORAGE_INFO_TEXT};
    }
}
