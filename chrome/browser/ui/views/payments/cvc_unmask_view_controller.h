// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PAYMENTS_CVC_UNMASK_VIEW_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_PAYMENTS_CVC_UNMASK_VIEW_CONTROLLER_H_

#include <string>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/observer_list.h"
#include "base/strings/string16.h"
#include "chrome/browser/ui/views/payments/payment_request_sheet_controller.h"
#include "components/autofill/core/browser/payments/full_card_request.h"
#include "components/autofill/core/browser/payments/payments_client.h"
#include "components/autofill/core/browser/risk_data_loader.h"

namespace autofill {
class AutofillClient;
}

namespace content {
class WebContents;
}

namespace views {
class Textfield;
}

namespace payments {

class PaymentRequestSpec;
class PaymentRequestState;
class PaymentRequestDialogView;

class CvcUnmaskViewController
    : public PaymentRequestSheetController,
      public autofill::RiskDataLoader,
      public autofill::payments::PaymentsClientDelegate,
      public autofill::payments::FullCardRequest::UIDelegate {
 public:
  CvcUnmaskViewController(
      PaymentRequestSpec* spec,
      PaymentRequestState* state,
      PaymentRequestDialogView* dialog,
      const autofill::CreditCard& credit_card,
      base::WeakPtr<autofill::payments::FullCardRequest::ResultDelegate>
          result_delegate,
      content::WebContents* web_contents);
  ~CvcUnmaskViewController() override;

  // autofill::payments::PaymentsClientDelegate:
  IdentityProvider* GetIdentityProvider() override;
  void OnDidGetRealPan(autofill::AutofillClient::PaymentsRpcResult result,
                       const std::string& real_pan) override;
  void OnDidGetUploadDetails(
      autofill::AutofillClient::PaymentsRpcResult result,
      const base::string16& context_token,
      std::unique_ptr<base::DictionaryValue> legal_message) override;
  void OnDidUploadCard(autofill::AutofillClient::PaymentsRpcResult result,
                       const std::string& server_id) override;

  // autofill::RiskDataLoader:
  void LoadRiskData(
      const base::Callback<void(const std::string&)>& callback) override;

  // autofill::payments::FullCardRequest::UIDelegate:
  void ShowUnmaskPrompt(
      const autofill::CreditCard& card,
      autofill::AutofillClient::UnmaskCardReason reason,
      base::WeakPtr<autofill::CardUnmaskDelegate> delegate) override;
  void OnUnmaskVerificationResult(
      autofill::AutofillClient::PaymentsRpcResult result) override;

 protected:
  // PaymentRequestSheetController:
  base::string16 GetSheetTitle() override;
  void FillContentView(views::View* content_view) override;
  std::unique_ptr<views::Button> CreatePrimaryButton() override;
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

 private:
  // Called when the user confirms their CVC. This will pass the value to the
  // active FullCardRequest.
  void CvcConfirmed();

  // Display a label with the text |error|
  void DisplayError(base::string16 error);

  bool GetSheetId(DialogViewID* sheet_id) override;
  views::View* GetFirstFocusedView() override;

  views::Textfield* cvc_field_;  // owned by the view hierarchy, outlives this.
  autofill::CreditCard credit_card_;
  content::WebContents* web_contents_;
  // The identity provider, used for Payments integration.
  std::unique_ptr<IdentityProvider> identity_provider_;
  autofill::payments::PaymentsClient payments_client_;
  autofill::payments::FullCardRequest full_card_request_;
  base::WeakPtr<autofill::CardUnmaskDelegate> unmask_delegate_;

  base::WeakPtrFactory<CvcUnmaskViewController> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(CvcUnmaskViewController);
};

}  // namespace payments

#endif  // CHROME_BROWSER_UI_VIEWS_PAYMENTS_CVC_UNMASK_VIEW_CONTROLLER_H_
