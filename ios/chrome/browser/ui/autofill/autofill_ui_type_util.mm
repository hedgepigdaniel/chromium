// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/autofill/autofill_ui_type_util.h"

#include "base/logging.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

AutofillUIType AutofillUITypeFromAutofillType(autofill::ServerFieldType type) {
  switch (type) {
    case autofill::UNKNOWN_TYPE:
      return AutofillUITypeUnknown;
    case autofill::CREDIT_CARD_NUMBER:
      return AutofillUITypeCreditCardNumber;
    case autofill::CREDIT_CARD_NAME_FULL:
      return AutofillUITypeCreditCardHolderFullName;
    case autofill::CREDIT_CARD_EXP_MONTH:
      return AutofillUITypeCreditCardExpMonth;
    case autofill::CREDIT_CARD_EXP_4_DIGIT_YEAR:
      return AutofillUITypeCreditCardExpYear;
    case autofill::NAME_FULL:
      return AutofillUITypeProfileFullName;
    case autofill::COMPANY_NAME:
      return AutofillUITypeProfileCompanyName;
    case autofill::ADDRESS_HOME_LINE1:
      return AutofillUITypeProfileHomeAddressLine1;
    case autofill::ADDRESS_HOME_LINE2:
      return AutofillUITypeProfileHomeAddressLine2;
    case autofill::ADDRESS_HOME_CITY:
      return AutofillUITypeProfileHomeAddressCity;
    case autofill::ADDRESS_HOME_STATE:
      return AutofillUITypeProfileHomeAddressState;
    case autofill::ADDRESS_HOME_ZIP:
      return AutofillUITypeProfileHomeAddressZip;
    case autofill::ADDRESS_HOME_COUNTRY:
      return AutofillUITypeProfileHomeAddressCountry;
    case autofill::PHONE_HOME_WHOLE_NUMBER:
      return AutofillUITypeProfileHomePhoneWholeNumber;
    case autofill::EMAIL_ADDRESS:
      return AutofillUITypeProfileEmailAddress;
    default:
      NOTREACHED();
      return AutofillUITypeUnknown;
  }
}

autofill::ServerFieldType AutofillTypeFromAutofillUIType(AutofillUIType type) {
  switch (type) {
    case AutofillUITypeUnknown:
      return autofill::UNKNOWN_TYPE;
    case AutofillUITypeCreditCardNumber:
      return autofill::CREDIT_CARD_NUMBER;
    case AutofillUITypeCreditCardHolderFullName:
      return autofill::CREDIT_CARD_NAME_FULL;
    case AutofillUITypeCreditCardExpMonth:
      return autofill::CREDIT_CARD_EXP_MONTH;
    case AutofillUITypeCreditCardExpYear:
      return autofill::CREDIT_CARD_EXP_4_DIGIT_YEAR;
    case AutofillUITypeProfileFullName:
      return autofill::NAME_FULL;
    case AutofillUITypeProfileCompanyName:
      return autofill::COMPANY_NAME;
    case AutofillUITypeProfileHomeAddressLine1:
      return autofill::ADDRESS_HOME_LINE1;
    case AutofillUITypeProfileHomeAddressLine2:
      return autofill::ADDRESS_HOME_LINE2;
    case AutofillUITypeProfileHomeAddressCity:
      return autofill::ADDRESS_HOME_CITY;
    case AutofillUITypeProfileHomeAddressState:
      return autofill::ADDRESS_HOME_STATE;
    case AutofillUITypeProfileHomeAddressZip:
      return autofill::ADDRESS_HOME_ZIP;
    case AutofillUITypeProfileHomeAddressCountry:
      return autofill::ADDRESS_HOME_COUNTRY;
    case AutofillUITypeProfileHomePhoneWholeNumber:
      return autofill::PHONE_HOME_WHOLE_NUMBER;
    case AutofillUITypeProfileEmailAddress:
      return autofill::EMAIL_ADDRESS;
    case AutofillUITypeCreditCardBillingAddress:
    default:
      NOTREACHED();
      return autofill::UNKNOWN_TYPE;
  }
}
