// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/chromeos/assistant_optin/assistant_optin_handler.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/consent_auditor/consent_auditor_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/ui/webui/chromeos/assistant_optin/assistant_optin_utils.h"
#include "chrome/browser/ui/webui/chromeos/user_image_source.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/services/assistant/public/mojom/constants.mojom.h"
#include "chromeos/services/assistant/public/proto/settings_ui.pb.h"
#include "components/arc/arc_prefs.h"
#include "components/consent_auditor/consent_auditor.h"
#include "components/login/localized_values_builder.h"
#include "components/prefs/pref_service.h"
#include "components/signin/core/browser/signin_manager_base.h"
#include "components/user_manager/user_manager.h"
#include "services/service_manager/public/cpp/connector.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/webui/web_ui_util.h"

using sync_pb::UserConsentTypes;

namespace chromeos {

namespace {

constexpr char kJsScreenPath[] = "assistantOptin";

// Construct SettingsUiSelector for the ConsentFlow UI.
assistant::SettingsUiSelector GetSettingsUiSelector() {
  assistant::SettingsUiSelector selector;
  assistant::ConsentFlowUiSelector* consent_flow_ui =
      selector.mutable_consent_flow_ui_selector();
  consent_flow_ui->set_flow_id(assistant::ActivityControlSettingsUiSelector::
                                   ASSISTANT_SUW_ONBOARDING_ON_CHROME_OS);
  selector.set_email_opt_in(true);
  return selector;
}

// Construct SettingsUiUpdate for user opt-in.
assistant::SettingsUiUpdate GetSettingsUiUpdate(
    const std::string& consent_token) {
  assistant::SettingsUiUpdate update;
  assistant::ConsentFlowUiUpdate* consent_flow_update =
      update.mutable_consent_flow_ui_update();
  consent_flow_update->set_flow_id(
      assistant::ActivityControlSettingsUiSelector::
          ASSISTANT_SUW_ONBOARDING_ON_CHROME_OS);
  consent_flow_update->set_consent_token(consent_token);

  return update;
}

// Construct SettingsUiUpdate for email opt-in.
assistant::SettingsUiUpdate GetEmailOptInUpdate(bool opted_in) {
  assistant::SettingsUiUpdate update;
  assistant::EmailOptInUpdate* email_optin_update =
      update.mutable_email_opt_in_update();
  email_optin_update->set_email_opt_in_update_state(
      opted_in ? assistant::EmailOptInUpdate::OPT_IN
               : assistant::EmailOptInUpdate::OPT_OUT);

  return update;
}

using SettingZippyList = google::protobuf::RepeatedPtrField<
    assistant::ClassicActivityControlUiTexts::SettingZippy>;
// Helper method to create zippy data.
base::Value CreateZippyData(const SettingZippyList& zippy_list) {
  base::Value zippy_data(base::Value::Type::LIST);
  for (auto& setting_zippy : zippy_list) {
    base::Value data(base::Value::Type::DICTIONARY);
    data.SetKey("title", base::Value(setting_zippy.title()));
    if (setting_zippy.description_paragraph_size()) {
      data.SetKey("description",
                  base::Value(setting_zippy.description_paragraph(0)));
    }
    if (setting_zippy.additional_info_paragraph_size()) {
      data.SetKey("additionalInfo",
                  base::Value(setting_zippy.additional_info_paragraph(0)));
    }
    data.SetKey("iconUri", base::Value(setting_zippy.icon_uri()));
    data.SetKey("popupLink", base::Value(l10n_util::GetStringUTF16(
                                 IDS_ASSISTANT_ACTIVITY_CONTROL_POPUP_LINK)));
    zippy_data.GetList().push_back(std::move(data));
  }
  return zippy_data;
}

// Helper method to create disclosure data.
base::Value CreateDisclosureData(const SettingZippyList& disclosure_list) {
  base::Value disclosure_data(base::Value::Type::LIST);
  for (auto& disclosure : disclosure_list) {
    base::Value data(base::Value::Type::DICTIONARY);
    data.SetKey("title", base::Value(disclosure.title()));
    if (disclosure.description_paragraph_size()) {
      data.SetKey("description",
                  base::Value(disclosure.description_paragraph(0)));
    }
    if (disclosure.additional_info_paragraph_size()) {
      data.SetKey("additionalInfo",
                  base::Value(disclosure.additional_info_paragraph(0)));
    }
    data.SetKey("iconUri", base::Value(disclosure.icon_uri()));
    disclosure_data.GetList().push_back(std::move(data));
  }
  return disclosure_data;
}

// Helper method to create get more screen data.
base::Value CreateGetMoreData(bool email_optin_needed,
                              const assistant::EmailOptInUi& email_optin_ui) {
  base::Value get_more_data(base::Value::Type::LIST);

  // Process hotword data.
  base::Value hotword_data(base::Value::Type::DICTIONARY);
  hotword_data.SetKey(
      "title",
      base::Value(l10n_util::GetStringUTF16(IDS_ASSISTANT_HOTWORD_TITLE)));
  hotword_data.SetKey(
      "description",
      base::Value(l10n_util::GetStringUTF16(IDS_ASSISTANT_HOTWORD_DESC)));
  hotword_data.SetKey("defaultEnabled", base::Value(true));
  hotword_data.SetKey(
      "iconUri",
      base::Value("https://www.gstatic.com/images/icons/material/system/"
                  "2x/mic_none_grey600_48dp.png"));
  get_more_data.GetList().push_back(std::move(hotword_data));

  // Process screen context data.
  base::Value context_data(base::Value::Type::DICTIONARY);
  context_data.SetKey("title", base::Value(l10n_util::GetStringUTF16(
                                   IDS_ASSISTANT_SCREEN_CONTEXT_TITLE)));
  context_data.SetKey("description", base::Value(l10n_util::GetStringUTF16(
                                         IDS_ASSISTANT_SCREEN_CONTEXT_DESC)));
  context_data.SetKey("defaultEnabled", base::Value(true));
  context_data.SetKey(
      "iconUri",
      base::Value("https://www.gstatic.com/images/icons/material/system/"
                  "2x/laptop_chromebook_grey600_24dp.png"));
  get_more_data.GetList().push_back(std::move(context_data));

  // Process email optin data.
  if (email_optin_needed) {
    base::Value data(base::Value::Type::DICTIONARY);
    data.SetKey("title", base::Value(email_optin_ui.title()));
    data.SetKey("description", base::Value(email_optin_ui.description()));
    data.SetKey("defaultEnabled",
                base::Value(email_optin_ui.default_enabled()));
    data.SetKey("iconUri", base::Value(email_optin_ui.icon_uri()));
    data.SetKey("legalText", base::Value(email_optin_ui.legal_text()));
    get_more_data.GetList().push_back(std::move(data));
  }

  return get_more_data;
}

// Get string constants for settings ui.
base::Value GetSettingsUiStrings(const assistant::SettingsUi& settings_ui,
                                 bool activity_control_needed) {
  auto consent_ui = settings_ui.consent_flow_ui().consent_ui();
  auto activity_control_ui = consent_ui.activity_control_ui();
  auto third_party_disclosure_ui = consent_ui.third_party_disclosure_ui();
  base::Value dictionary(base::Value::Type::DICTIONARY);

  // Add activity controll string constants.
  if (activity_control_needed) {
    scoped_refptr<base::RefCountedMemory> image =
        chromeos::UserImageSource::GetUserImage(
            user_manager::UserManager::Get()->GetActiveUser()->GetAccountId());
    std::string icon_url = webui::GetPngDataUrl(image->front(), image->size());
    dictionary.SetKey("valuePropUserImage", base::Value(icon_url));

    dictionary.SetKey("valuePropIdentity",
                      base::Value(activity_control_ui.identity()));
    dictionary.SetKey("valuePropTitle",
                      base::Value(activity_control_ui.title()));
    if (activity_control_ui.intro_text_paragraph_size()) {
      dictionary.SetKey(
          "valuePropIntro",
          base::Value(activity_control_ui.intro_text_paragraph(0)));
    }
    if (activity_control_ui.footer_paragraph_size()) {
      dictionary.SetKey("valuePropFooter",
                        base::Value(activity_control_ui.footer_paragraph(0)));
    }
    dictionary.SetKey("valuePropNextButton",
                      base::Value(consent_ui.accept_button_text()));
    dictionary.SetKey("valuePropSkipButton",
                      base::Value(consent_ui.reject_button_text()));
  }

  // Add third party string constants.
  dictionary.SetKey("thirdPartyTitle",
                    base::Value(third_party_disclosure_ui.title()));
  dictionary.SetKey("thirdPartyContinueButton",
                    base::Value(third_party_disclosure_ui.button_continue()));
  dictionary.SetKey("thirdPartyFooter", base::Value(consent_ui.tos_pp_links()));

  // Add get more screen string constants.
  dictionary.SetKey("getMoreTitle", base::Value(l10n_util::GetStringUTF16(
                                        IDS_ASSISTANT_GET_MORE_SCREEN_TITLE)));
  dictionary.SetKey("getMoreIntro", base::Value(l10n_util::GetStringUTF16(
                                        IDS_ASSISTANT_GET_MORE_SCREEN_INTRO)));
  dictionary.SetKey(
      "getMoreContinueButton",
      base::Value(l10n_util::GetStringUTF16(IDS_ASSISTANT_CONTINUE_BUTTON)));

  return dictionary;
}

void RecordActivityControlConsent(Profile* profile,
                                  std::string ui_audit_key,
                                  bool opted_in) {
  SigninManagerBase* signin_manager =
      SigninManagerFactory::GetForProfile(profile);
  DCHECK(signin_manager->IsAuthenticated());
  std::string account_id = signin_manager->GetAuthenticatedAccountId();

  UserConsentTypes::AssistantActivityControlConsent consent;
  consent.set_ui_audit_key(ui_audit_key);
  consent.set_status(opted_in ? UserConsentTypes::GIVEN
                              : UserConsentTypes::NOT_GIVEN);

  ConsentAuditorFactory::GetForProfile(profile)
      ->RecordAssistantActivityControlConsent(account_id, consent);
}

}  // namespace

AssistantOptInHandler::AssistantOptInHandler(
    JSCallsContainer* js_calls_container)
    : BaseWebUIHandler(js_calls_container), weak_factory_(this) {
  DCHECK(js_calls_container);
  set_call_js_prefix(kJsScreenPath);
}

AssistantOptInHandler::~AssistantOptInHandler() {
  arc::VoiceInteractionControllerClient::Get()->RemoveObserver(this);
}

void AssistantOptInHandler::DeclareLocalizedValues(
    ::login::LocalizedValuesBuilder* builder) {}

void AssistantOptInHandler::RegisterMessages() {
  AddCallback("initialized", &AssistantOptInHandler::HandleInitialized);
  AddCallback("hotwordResult", &AssistantOptInHandler::HandleHotwordResult);
}

void AssistantOptInHandler::Initialize() {
  if (arc::VoiceInteractionControllerClient::Get()->voice_interaction_state() ==
      ash::mojom::VoiceInteractionState::NOT_READY) {
    arc::VoiceInteractionControllerClient::Get()->AddObserver(this);
  } else {
    BindAssistantSettingsManager();
  }
}

void AssistantOptInHandler::ShowNextScreen() {
  CallJSOrDefer("showNextScreen");
}

void AssistantOptInHandler::OnActivityControlOptInResult(bool opted_in) {
  Profile* profile = Profile::FromWebUI(web_ui());
  if (opted_in) {
    RecordAssistantOptInStatus(ACTIVITY_CONTROL_ACCEPTED);
    settings_manager_->UpdateSettings(
        GetSettingsUiUpdate(consent_token_).SerializeAsString(),
        base::BindOnce(&AssistantOptInHandler::OnUpdateSettingsResponse,
                       weak_factory_.GetWeakPtr()));
  } else {
    RecordAssistantOptInStatus(ACTIVITY_CONTROL_SKIPPED);
    profile->GetPrefs()->SetBoolean(
        arc::prefs::kVoiceInteractionActivityControlAccepted, false);
    CallJSOrDefer("closeDialog");
  }

  RecordActivityControlConsent(profile, ui_audit_key_, opted_in);
}

void AssistantOptInHandler::OnEmailOptInResult(bool opted_in) {
  if (!email_optin_needed_) {
    DCHECK(!opted_in);
    ShowNextScreen();
    return;
  }

  RecordAssistantOptInStatus(opted_in ? EMAIL_OPTED_IN : EMAIL_OPTED_OUT);
  settings_manager_->UpdateSettings(
      GetEmailOptInUpdate(opted_in).SerializeAsString(),
      base::BindOnce(&AssistantOptInHandler::OnUpdateSettingsResponse,
                     weak_factory_.GetWeakPtr()));
}

void AssistantOptInHandler::OnStateChanged(
    ash::mojom::VoiceInteractionState state) {
  if (state != ash::mojom::VoiceInteractionState::NOT_READY) {
    BindAssistantSettingsManager();
    arc::VoiceInteractionControllerClient::Get()->RemoveObserver(this);
  }
}

void AssistantOptInHandler::BindAssistantSettingsManager() {
  if (settings_manager_.is_bound())
    return;

  // Set up settings mojom.
  service_manager::Connector* connector =
      content::BrowserContext::GetConnectorFor(Profile::FromWebUI(web_ui()));
  connector->BindInterface(assistant::mojom::kServiceName,
                           mojo::MakeRequest(&settings_manager_));

  SendGetSettingsRequest();
}

void AssistantOptInHandler::SendGetSettingsRequest() {
  assistant::SettingsUiSelector selector = GetSettingsUiSelector();
  settings_manager_->GetSettings(
      selector.SerializeAsString(),
      base::BindOnce(&AssistantOptInHandler::OnGetSettingsResponse,
                     weak_factory_.GetWeakPtr()));
}

void AssistantOptInHandler::ReloadContent(const base::Value& dict) {
  CallJSOrDefer("reloadContent", dict);
}

void AssistantOptInHandler::AddSettingZippy(const std::string& type,
                                            const base::Value& data) {
  CallJSOrDefer("addSettingZippy", type, data);
}

void AssistantOptInHandler::OnGetSettingsResponse(const std::string& settings) {
  assistant::SettingsUi settings_ui;
  settings_ui.ParseFromString(settings);

  DCHECK(settings_ui.has_consent_flow_ui());

  RecordAssistantOptInStatus(FLOW_STARTED);
  auto consent_ui = settings_ui.consent_flow_ui().consent_ui();
  auto activity_control_ui = consent_ui.activity_control_ui();
  auto third_party_disclosure_ui = consent_ui.third_party_disclosure_ui();

  consent_token_ = activity_control_ui.consent_token();
  ui_audit_key_ = activity_control_ui.ui_audit_key();

  // Process activity control data.
  if (!activity_control_ui.setting_zippy().size()) {
    // No need to consent. Move to the next screen.
    activity_control_needed_ = false;
    PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();
    prefs->SetBoolean(arc::prefs::kVoiceInteractionActivityControlAccepted,
                      true);
    ShowNextScreen();
  } else {
    AddSettingZippy("settings",
                    CreateZippyData(activity_control_ui.setting_zippy()));
  }

  // Process third party disclosure data.
  AddSettingZippy("disclosure", CreateDisclosureData(
                                    third_party_disclosure_ui.disclosures()));

  // Process get more data.
  email_optin_needed_ = settings_ui.has_email_opt_in_ui() &&
                        settings_ui.email_opt_in_ui().has_title();
  AddSettingZippy("get-more", CreateGetMoreData(email_optin_needed_,
                                                settings_ui.email_opt_in_ui()));

  // Pass string constants dictionary.
  ReloadContent(GetSettingsUiStrings(settings_ui, activity_control_needed_));
}

void AssistantOptInHandler::OnUpdateSettingsResponse(
    const std::string& result) {
  assistant::SettingsUiUpdateResult ui_result;
  ui_result.ParseFromString(result);

  if (ui_result.has_consent_flow_update_result()) {
    if (ui_result.consent_flow_update_result().update_status() !=
        assistant::ConsentFlowUiUpdateResult::SUCCESS) {
      // TODO(updowndta): Handle consent update failure.
      LOG(ERROR) << "Consent udpate error.";
    } else if (activity_control_needed_) {
      activity_control_needed_ = false;
      PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();
      prefs->SetBoolean(arc::prefs::kVoiceInteractionActivityControlAccepted,
                        true);
    }
  }

  if (ui_result.has_email_opt_in_update_result()) {
    if (ui_result.email_opt_in_update_result().update_status() !=
        assistant::EmailOptInUpdateResult::SUCCESS) {
      // TODO(updowndta): Handle email optin update failure.
      LOG(ERROR) << "Email OptIn udpate error.";
    }
    // Update hotword will cause Assistant restart. In order to make sure email
    // optin request is successfully sent to server, update the hotword after
    // email optin result has been received.
    PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();
    prefs->SetBoolean(arc::prefs::kVoiceInteractionHotwordEnabled,
                      enable_hotword_);
  }

  ShowNextScreen();
}

void AssistantOptInHandler::HandleInitialized() {
  ExecuteDeferredJSCalls();
}

void AssistantOptInHandler::HandleHotwordResult(bool enable_hotword) {
  enable_hotword_ = enable_hotword;

  if (!email_optin_needed_) {
    // No need to send email optin result. Safe to update hotword pref and
    // restart Assistant here.
    PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();
    prefs->SetBoolean(arc::prefs::kVoiceInteractionHotwordEnabled,
                      enable_hotword);
  }
}

}  // namespace chromeos
