// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_LOCAL_CARD_MIGRATION_MANAGER_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_LOCAL_CARD_MIGRATION_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/strings/string16.h"
#include "components/autofill/core/browser/autofill_client.h"
#include "components/autofill/core/browser/payments/payments_client.h"

namespace autofill {

class CreditCard;
class PersonalDataManager;

// Server-side response can return SUCCESS, TEMPORARY_FAILURE, or
// PERMANENT_FAILURE (see SaveResult enum). Use these to extract migration
// result.
const char kMigrationResultPermanentFailure[] = "PERMANENT_FAILURE";
const char kMigrationResultTemporaryFailure[] = "TEMPORARY_FAILURE";
const char kMigrationResultSuccess[] = "SUCCESS";

// MigratableCreditCard class is used as a data structure to work as an
// intermediary between the UI side and the migration manager. Besides the basic
// credit card information, it also includes a boolean that represents whether
// the card was chosen for upload. We use each card's guid to distinguish each
// credit card for upload request/response.
// TODO(crbug.com/852904): Create one Enum to represent migration status such as
// whether the card is successfully uploaded or failure on uploading.
class MigratableCreditCard {
 public:
  // Possible states for the migratable local card.
  enum MigrationStatus {
    // Set if the migratable card have not been uploaded.
    UNKNOWN,
    // Set if the migratable card was successfully uploaded to the server.
    SUCCESS_ON_UPLOAD,
    // Set if the migratable card encountered a failure during upload.
    FAILURE_ON_UPLOAD,
  };

  MigratableCreditCard(const CreditCard& credit_card);
  ~MigratableCreditCard();

  CreditCard credit_card() const { return credit_card_; }

  bool is_chosen() const { return is_chosen_; }
  void ToggleChosen() { is_chosen_ = !is_chosen(); }

  MigrationStatus migration_status() const { return migration_status_; }
  void set_migration_status(MigrationStatus migration_status) {
    migration_status_ = migration_status;
  }

 private:
  // The main card information of the current migratable card.
  CreditCard credit_card_;

  // Whether the user has decided to migrate the this card; shown as a checkbox
  // in the UI.
  bool is_chosen_ = true;

  // Migration status for this card.
  MigrationStatus migration_status_ = MigrationStatus::UNKNOWN;
};

// Manages logic for determining whether migration of locally saved credit cards
// to Google Payments is available as well as multiple local card uploading.
// Owned by FormDataImporter.
class LocalCardMigrationManager {
 public:
  // The parameters should outlive the LocalCardMigrationManager.
  LocalCardMigrationManager(AutofillClient* client,
                            payments::PaymentsClient* payments_client,
                            const std::string& app_locale,
                            PersonalDataManager* personal_data_manager);
  virtual ~LocalCardMigrationManager();

  // Returns true if all of the conditions for allowing local credit card
  // migration are satisfied. Initializes the local card list for upload.
  bool ShouldOfferLocalCardMigration(int imported_credit_card_record_type);

  // Called from FormDataImporter or settings page when all migration
  // requirements are met. Fetches legal documents and triggers the
  // OnDidGetUploadDetails callback. |is_from_settings_page| to denote the user
  // triggers the migration from settings page. It will trigger the main prompt
  // directly if the get upload details call returns success.
  void AttemptToOfferLocalCardMigration(bool is_from_settings_page);

  // Callback function when user agrees to migration on the intermediate dialog.
  // Pops up a larger, modal dialog showing the local cards to be uploaded.
  // Exposed for testing.
  virtual void OnUserAcceptedIntermediateMigrationDialog();

  // Callback function when user confirms migration on the main migration
  // dialog. Sets |user_accepted_main_migration_dialog_| and sends the migration
  // request once risk data is available. Exposed for testing.
  virtual void OnUserAcceptedMainMigrationDialog();

  // Check that the user is signed in, syncing, and the proper experiment
  // flags are enabled. Override in the test class.
  virtual bool IsCreditCardMigrationEnabled();

  // Determines what detected_values metadata to send (generally, cardholder
  // name if it exists on all cards, and existence of Payments customer).
  int GetDetectedValues() const;

  // Fetch all migratable credit cards and store in |migratable_credit_cards_|.
  void GetMigratableCreditCards();

 protected:
  // Callback after successfully getting the legal documents. On success,
  // displays the offer-to-migrate dialog, which the user can accept or not.
  // When |is_from_settings_page| is true, it will trigger the main prompt
  // directly. If not, trigger the intermediate prompt. Exposed for testing.
  virtual void OnDidGetUploadDetails(
      bool is_from_settings_page,
      AutofillClient::PaymentsRpcResult result,
      const base::string16& context_token,
      std::unique_ptr<base::DictionaryValue> legal_message);

  // Callback after successfully getting the migration save results. Map
  // migration save result to each card depending on the |save_result|. Will
  // trigger a window showing the migration result together with display text to
  // the user.
  void OnDidMigrateLocalCards(
      AutofillClient::PaymentsRpcResult result,
      std::unique_ptr<std::unordered_map<std::string, std::string>> save_result,
      const std::string& display_text);

  AutofillClient* const client_;

  // Handles Payments service requests.
  // Owned by AutofillManager.
  payments::PaymentsClient* payments_client_;

 private:
  FRIEND_TEST_ALL_PREFIXES(LocalCardMigrationManagerTest,
                           MigrateCreditCard_MigrationPermanentFailure);
  FRIEND_TEST_ALL_PREFIXES(LocalCardMigrationManagerTest,
                           MigrateCreditCard_MigrationTemporaryFailure);
  FRIEND_TEST_ALL_PREFIXES(LocalCardMigrationManagerTest,
                           MigrateCreditCard_MigrationSuccess);

  // Pops up a larger, modal dialog showing the local cards to be uploaded.
  void ShowMainMigrationDialog();

  // Callback function when migration risk data is ready. Saves risk data in
  // |migration_risk_data_| and calls SendMigrateLocalCardsRequest if the user
  // has accepted the main migration dialog.
  void OnDidGetMigrationRiskData(const std::string& risk_data);

  // Finalizes the migration request and calls PaymentsClient.
  void SendMigrateLocalCardsRequest();

  std::unique_ptr<base::DictionaryValue> legal_message_;

  std::string app_locale_;

  // The personal data manager, used to save and load personal data to/from the
  // web database.  This is overridden by the AutofillManagerTest.
  // Weak reference.
  // May be NULL.  NULL indicates OTR.
  PersonalDataManager* personal_data_manager_;

  // Collected information about a pending migration request.
  payments::PaymentsClient::MigrationRequestDetails migration_request_;

  // The local credit cards to be uploaded.
  std::vector<MigratableCreditCard> migratable_credit_cards_;

  // |true| if the user has accepted migrating their local cards to Google Pay
  // on the main dialog.
  bool user_accepted_main_migration_dialog_ = false;

  base::WeakPtrFactory<LocalCardMigrationManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(LocalCardMigrationManager);
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_LOCAL_CARD_MIGRATION_MANAGER_H_
