// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_AUTOFILL_ASSISTANT_UI_CONTROLLER_ANDROID_H_
#define CHROME_BROWSER_ANDROID_AUTOFILL_ASSISTANT_UI_CONTROLLER_ANDROID_H_

#include "components/autofill_assistant/browser/ui_controller.h"

#include <string>

#include "base/android/scoped_java_ref.h"
#include "base/macros.h"

namespace autofill_assistant {
// Class implements UiController and starts the Controller.
class UiControllerAndroid : public UiController {
 public:
  UiControllerAndroid(JNIEnv* env,
                      jobject jcaller,
                      const base::android::JavaParamRef<jobject>& webContents);
  ~UiControllerAndroid() override;

  // Overrides UiController:
  void SetUiDelegate(UiDelegate* ui_delegate) override;
  void ShowStatusMessage(const std::string& message) override;
  void ShowOverlay() override;
  void HideOverlay() override;
  void ChooseAddress(
      base::OnceCallback<void(const std::string&)> callback) override;
  void ChooseCard(
      base::OnceCallback<void(const std::string&)> callback) override;

  // Called by Java.
  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);

 private:
  // Java-side AutofillAssistantUiController object.
  base::android::ScopedJavaGlobalRef<jobject>
      java_autofill_assistant_ui_controller_;

  UiDelegate* ui_delegate_;

  DISALLOW_COPY_AND_ASSIGN(UiControllerAndroid);
};

}  // namespace autofill_assistant.
#endif  // CHROME_BROWSER_ANDROID_AUTOFILL_ASSISTANT_UI_CONTROLLER_ANDROID_H_
