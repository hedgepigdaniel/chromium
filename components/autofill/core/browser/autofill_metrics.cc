// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/autofill_metrics.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "base/metrics/user_metrics.h"
#include "base/time/time.h"
#include "components/autofill/core/browser/autofill_experiments.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/common/form_data.h"
#include "components/ukm/ukm_entry_builder.h"

namespace internal {
const char kUKMCardUploadDecisionEntryName[] = "Autofill.CardUploadDecision";
const char kUKMCardUploadDecisionMetricName[] = "UploadDecision";
const char kUKMDeveloperEngagementEntryName[] = "Autofill.DeveloperEngagement";
const char kUKMDeveloperEngagementMetricName[] = "DeveloperEngagement";
const char kUKMMillisecondsSinceFormParsedMetricName[] =
    "MillisecondsSinceFormParsed";
const char kUKMInteractedWithFormEntryName[] = "Autofill.InteractedWithForm";
const char kUKMIsForCreditCardMetricName[] = "IsForCreditCard";
const char kUKMLocalRecordTypeCountMetricName[] = "LocalRecordTypeCount";
const char kUKMServerRecordTypeCountMetricName[] = "ServerRecordTypeCount";
const char kUKMSuggestionsShownEntryName[] = "Autofill.SuggestionsShown";
const char kUKMSelectedMaskedServerCardEntryName[] =
    "Autofill.SelectedMaskedServerCard";
const char kUKMSuggestionFilledEntryName[] = "Autofill.SuggestionFilled";
const char kUKMRecordTypeMetricName[] = "RecordType";
const char kUKMTextFieldDidChangeEntryName[] = "Autofill.TextFieldDidChange";
const char kUKMFieldTypeGroupMetricName[] = "FieldTypeGroup";
const char kUKMHeuristicTypeMetricName[] = "HeuristicType";
const char kUKMServerTypeMetricName[] = "ServerType";
const char kUKMHtmlFieldTypeMetricName[] = "HtmlFieldType";
const char kUKMHtmlFieldModeMetricName[] = "HtmlFieldMode";
const char kUKMIsAutofilledMetricName[] = "IsAutofilled";
const char kUKMIsEmptyMetricName[] = "IsEmpty";
const char kUKMFormSubmittedEntryName[] = "Autofill.AutofillFormSubmitted";
const char kUKMAutofillFormSubmittedStateMetricName[] =
    "AutofillFormSubmittedState";
}  // namespace internal

namespace autofill {

namespace {

// Note: if adding an enum value here, update the corresponding description for
// AutofillTypeQualityByFieldType in histograms.xml.
enum FieldTypeGroupForMetrics {
  GROUP_AMBIGUOUS = 0,
  GROUP_NAME,
  GROUP_COMPANY,
  GROUP_ADDRESS_LINE_1,
  GROUP_ADDRESS_LINE_2,
  GROUP_ADDRESS_CITY,
  GROUP_ADDRESS_STATE,
  GROUP_ADDRESS_ZIP,
  GROUP_ADDRESS_COUNTRY,
  GROUP_PHONE,
  GROUP_FAX,  // Deprecated.
  GROUP_EMAIL,
  GROUP_CREDIT_CARD_NAME,
  GROUP_CREDIT_CARD_NUMBER,
  GROUP_CREDIT_CARD_DATE,
  GROUP_CREDIT_CARD_TYPE,
  GROUP_PASSWORD,
  GROUP_ADDRESS_LINE_3,
  GROUP_USERNAME,
  GROUP_STREET_ADDRESS,
  GROUP_CREDIT_CARD_VERIFICATION,
  NUM_FIELD_TYPE_GROUPS_FOR_METRICS
};

std::string PreviousSaveCreditCardPromptUserDecisionToString(
    int previous_save_credit_card_prompt_user_decision) {
  DCHECK_LT(previous_save_credit_card_prompt_user_decision,
            prefs::NUM_PREVIOUS_SAVE_CREDIT_CARD_PROMPT_USER_DECISIONS);
  std::string previous_response;
  if (previous_save_credit_card_prompt_user_decision ==
      prefs::PREVIOUS_SAVE_CREDIT_CARD_PROMPT_USER_DECISION_ACCEPTED)
    previous_response = ".PreviouslyAccepted";
  else if (previous_save_credit_card_prompt_user_decision ==
           prefs::PREVIOUS_SAVE_CREDIT_CARD_PROMPT_USER_DECISION_DENIED)
    previous_response = ".PreviouslyDenied";
  else
    DCHECK_EQ(previous_save_credit_card_prompt_user_decision,
              prefs::PREVIOUS_SAVE_CREDIT_CARD_PROMPT_USER_DECISION_NONE);
  return previous_response;
}

}  // namespace

// First, translates |field_type| to the corresponding logical |group| from
// |FieldTypeGroupForMetrics|.  Then, interpolates this with the given |metric|,
// which should be in the range [0, |num_possible_metrics|).
// Returns the interpolated index.
//
// The interpolation maps the pair (|group|, |metric|) to a single index, so
// that all the indicies for a given group are adjacent.  In particular, with
// the groups {AMBIGUOUS, NAME, ...} combining with the metrics {UNKNOWN, MATCH,
// MISMATCH}, we create this set of mapped indices:
// {
//   AMBIGUOUS+UNKNOWN,
//   AMBIGUOUS+MATCH,
//   AMBIGUOUS+MISMATCH,
//   NAME+UNKNOWN,
//   NAME+MATCH,
//   NAME+MISMATCH,
//   ...
// }.
//
// Clients must ensure that |field_type| is one of the types Chrome supports
// natively, e.g. |field_type| must not be a billng address.
// NOTE: This is defined outside of the anonymous namespace so that it can be
// accessed from the unit test file. It is not exposed in the header file,
// however, because it is not intended for consumption outside of the metrics
// implementation.
int GetFieldTypeGroupMetric(ServerFieldType field_type,
                            AutofillMetrics::FieldTypeQualityMetric metric) {
  DCHECK_LT(metric, AutofillMetrics::NUM_FIELD_TYPE_QUALITY_METRICS);

  FieldTypeGroupForMetrics group = GROUP_AMBIGUOUS;
  switch (AutofillType(field_type).group()) {
    case NO_GROUP:
      group = GROUP_AMBIGUOUS;
      break;

    case NAME:
    case NAME_BILLING:
      group = GROUP_NAME;
      break;

    case COMPANY:
      group = GROUP_COMPANY;
      break;

    case ADDRESS_HOME:
    case ADDRESS_BILLING:
      switch (AutofillType(field_type).GetStorableType()) {
        case ADDRESS_HOME_LINE1:
          group = GROUP_ADDRESS_LINE_1;
          break;
        case ADDRESS_HOME_LINE2:
          group = GROUP_ADDRESS_LINE_2;
          break;
        case ADDRESS_HOME_LINE3:
          group = GROUP_ADDRESS_LINE_3;
          break;
        case ADDRESS_HOME_STREET_ADDRESS:
          group = GROUP_STREET_ADDRESS;
        case ADDRESS_HOME_CITY:
          group = GROUP_ADDRESS_CITY;
          break;
        case ADDRESS_HOME_STATE:
          group = GROUP_ADDRESS_STATE;
          break;
        case ADDRESS_HOME_ZIP:
          group = GROUP_ADDRESS_ZIP;
          break;
        case ADDRESS_HOME_COUNTRY:
          group = GROUP_ADDRESS_COUNTRY;
          break;
        default:
          NOTREACHED() << field_type << " has no group assigned (ambiguous)";
          group = GROUP_AMBIGUOUS;
          break;
      }
      break;

    case EMAIL:
      group = GROUP_EMAIL;
      break;

    case PHONE_HOME:
    case PHONE_BILLING:
      group = GROUP_PHONE;
      break;

    case CREDIT_CARD:
      switch (field_type) {
        case CREDIT_CARD_NAME_FULL:
        case CREDIT_CARD_NAME_FIRST:
        case CREDIT_CARD_NAME_LAST:
          group = GROUP_CREDIT_CARD_NAME;
          break;
        case CREDIT_CARD_NUMBER:
          group = GROUP_CREDIT_CARD_NUMBER;
          break;
        case CREDIT_CARD_TYPE:
          group = GROUP_CREDIT_CARD_TYPE;
          break;
        case CREDIT_CARD_EXP_MONTH:
        case CREDIT_CARD_EXP_2_DIGIT_YEAR:
        case CREDIT_CARD_EXP_4_DIGIT_YEAR:
        case CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR:
        case CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR:
          group = GROUP_CREDIT_CARD_DATE;
          break;
        case CREDIT_CARD_VERIFICATION_CODE:
          group = GROUP_CREDIT_CARD_VERIFICATION;
          break;
        default:
          NOTREACHED() << field_type << " has no group assigned (ambiguous)";
          group = GROUP_AMBIGUOUS;
          break;
      }
      break;

    case PASSWORD_FIELD:
      group = GROUP_PASSWORD;
      break;

    case USERNAME_FIELD:
      group = GROUP_USERNAME;
      break;

    case TRANSACTION:
      NOTREACHED();
      break;
  }

  // Interpolate the |metric| with the |group|, so that all metrics for a given
  // |group| are adjacent.
  return (group * AutofillMetrics::NUM_FIELD_TYPE_QUALITY_METRICS) + metric;
}

namespace {

// A version of the UMA_HISTOGRAM_ENUMERATION macro that allows the |name|
// to vary over the program's runtime.
void LogUMAHistogramEnumeration(const std::string& name,
                                int sample,
                                int boundary_value) {
  DCHECK_LT(sample, boundary_value);

  // Note: This leaks memory, which is expected behavior.
  base::HistogramBase* histogram =
      base::LinearHistogram::FactoryGet(
          name,
          1,
          boundary_value,
          boundary_value + 1,
          base::HistogramBase::kUmaTargetedHistogramFlag);
  histogram->Add(sample);
}

// A version of the UMA_HISTOGRAM_LONG_TIMES macro that allows the |name|
// to vary over the program's runtime.
void LogUMAHistogramLongTimes(const std::string& name,
                              const base::TimeDelta& duration) {
  // Note: This leaks memory, which is expected behavior.
  base::HistogramBase* histogram =
      base::Histogram::FactoryTimeGet(
          name,
          base::TimeDelta::FromMilliseconds(1),
          base::TimeDelta::FromHours(1),
          50,
          base::HistogramBase::kUmaTargetedHistogramFlag);
  histogram->AddTime(duration);
}

// Logs a type quality metric.  The primary histogram name is constructed based
// on |base_name|.  The field-specific histogram name also factors in the
// |field_type|.  Logs a sample of |metric|, which should be in the range
// [0, |num_possible_metrics|). May log a suffixed version of the metric
// depending on |metric_type|.
void LogTypeQualityMetric(const std::string& base_name,
                          AutofillMetrics::FieldTypeQualityMetric metric,
                          ServerFieldType field_type,
                          AutofillMetrics::QualityMetricType metric_type) {
  DCHECK_LT(metric, AutofillMetrics::NUM_FIELD_TYPE_QUALITY_METRICS);

  std::string suffix;
  switch (metric_type) {
    case AutofillMetrics::TYPE_SUBMISSION:
      break;
    case AutofillMetrics::TYPE_NO_SUBMISSION:
      suffix = ".NoSubmission";
      break;
    case AutofillMetrics::TYPE_AUTOCOMPLETE_BASED:
      suffix = ".BasedOnAutocomplete";
      break;
    default:
      NOTREACHED();
  }
  LogUMAHistogramEnumeration(base_name + suffix, metric,
                             AutofillMetrics::NUM_FIELD_TYPE_QUALITY_METRICS);

  int field_type_group_metric = GetFieldTypeGroupMetric(field_type, metric);
  int num_field_type_group_metrics =
      AutofillMetrics::NUM_FIELD_TYPE_QUALITY_METRICS *
      NUM_FIELD_TYPE_GROUPS_FOR_METRICS;
  LogUMAHistogramEnumeration(base_name + ".ByFieldType" + suffix,
                             field_type_group_metric,
                             num_field_type_group_metrics);
}

}  // namespace

// static
void AutofillMetrics::LogCardUploadDecisionMetrics(
    int upload_decision_metrics) {
  DCHECK(upload_decision_metrics);
  DCHECK_LT(upload_decision_metrics, 1 << kNumCardUploadDecisionMetrics);

  for (int metric = 0; metric < kNumCardUploadDecisionMetrics; ++metric)
    if (upload_decision_metrics & (1 << metric))
      UMA_HISTOGRAM_ENUMERATION("Autofill.CardUploadDecisionMetric", metric,
                                kNumCardUploadDecisionMetrics);
}

// static
void AutofillMetrics::LogCreditCardInfoBarMetric(
    InfoBarMetric metric,
    bool is_uploading,
    int previous_save_credit_card_prompt_user_decision) {
  DCHECK_LT(metric, NUM_INFO_BAR_METRICS);

  std::string destination = is_uploading ? ".Server" : ".Local";
  LogUMAHistogramEnumeration(
      "Autofill.CreditCardInfoBar" + destination +
          PreviousSaveCreditCardPromptUserDecisionToString(
              previous_save_credit_card_prompt_user_decision),
      metric, NUM_INFO_BAR_METRICS);
}

// static
void AutofillMetrics::LogCreditCardFillingInfoBarMetric(InfoBarMetric metric) {
  DCHECK_LT(metric, NUM_INFO_BAR_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Autofill.CreditCardFillingInfoBar", metric,
                            NUM_INFO_BAR_METRICS);
}

// static
void AutofillMetrics::LogSaveCardPromptMetric(
    SaveCardPromptMetric metric,
    bool is_uploading,
    bool is_reshow,
    int previous_save_credit_card_prompt_user_decision) {
  DCHECK_LT(metric, NUM_SAVE_CARD_PROMPT_METRICS);
  std::string destination = is_uploading ? ".Upload" : ".Local";
  std::string show = is_reshow ? ".Reshows" : ".FirstShow";
  LogUMAHistogramEnumeration(
      "Autofill.SaveCreditCardPrompt" + destination + show +
          PreviousSaveCreditCardPromptUserDecisionToString(
              previous_save_credit_card_prompt_user_decision),
      metric, NUM_SAVE_CARD_PROMPT_METRICS);
}

// static
void AutofillMetrics::LogScanCreditCardPromptMetric(
    ScanCreditCardPromptMetric metric) {
  DCHECK_LT(metric, NUM_SCAN_CREDIT_CARD_PROMPT_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Autofill.ScanCreditCardPrompt", metric,
                            NUM_SCAN_CREDIT_CARD_PROMPT_METRICS);
}

// static
void AutofillMetrics::LogScanCreditCardCompleted(
    const base::TimeDelta& duration,
    bool completed) {
  std::string suffix = completed ? "Completed" : "Cancelled";
  LogUMAHistogramLongTimes("Autofill.ScanCreditCard.Duration_" + suffix,
                           duration);
  UMA_HISTOGRAM_BOOLEAN("Autofill.ScanCreditCard.Completed", completed);
}

// static
void AutofillMetrics::LogUnmaskPromptEvent(UnmaskPromptEvent event) {
  UMA_HISTOGRAM_ENUMERATION("Autofill.UnmaskPrompt.Events", event,
                            NUM_UNMASK_PROMPT_EVENTS);
}

// static
void AutofillMetrics::LogUnmaskPromptEventDuration(
    const base::TimeDelta& duration,
    UnmaskPromptEvent close_event) {
  std::string suffix;
  switch (close_event) {
    case UNMASK_PROMPT_CLOSED_NO_ATTEMPTS:
      suffix = "NoAttempts";
      break;
    case UNMASK_PROMPT_CLOSED_FAILED_TO_UNMASK_RETRIABLE_FAILURE:
    case UNMASK_PROMPT_CLOSED_FAILED_TO_UNMASK_NON_RETRIABLE_FAILURE:
      suffix = "Failure";
      break;
    case UNMASK_PROMPT_CLOSED_ABANDON_UNMASKING:
      suffix = "AbandonUnmasking";
      break;
    case UNMASK_PROMPT_UNMASKED_CARD_FIRST_ATTEMPT:
    case UNMASK_PROMPT_UNMASKED_CARD_AFTER_FAILED_ATTEMPTS:
      suffix = "Success";
      break;
    default:
      NOTREACHED();
      return;
  }
  LogUMAHistogramLongTimes("Autofill.UnmaskPrompt.Duration", duration);
  LogUMAHistogramLongTimes("Autofill.UnmaskPrompt.Duration." + suffix,
                           duration);
}

// static
void AutofillMetrics::LogTimeBeforeAbandonUnmasking(
    const base::TimeDelta& duration) {
  UMA_HISTOGRAM_LONG_TIMES("Autofill.UnmaskPrompt.TimeBeforeAbandonUnmasking",
                           duration);
}

// static
void AutofillMetrics::LogRealPanResult(
    AutofillClient::PaymentsRpcResult result) {
  PaymentsRpcResult metric_result;
  switch (result) {
    case AutofillClient::SUCCESS:
      metric_result = PAYMENTS_RESULT_SUCCESS;
      break;
    case AutofillClient::TRY_AGAIN_FAILURE:
      metric_result = PAYMENTS_RESULT_TRY_AGAIN_FAILURE;
      break;
    case AutofillClient::PERMANENT_FAILURE:
      metric_result = PAYMENTS_RESULT_PERMANENT_FAILURE;
      break;
    case AutofillClient::NETWORK_ERROR:
      metric_result = PAYMENTS_RESULT_NETWORK_ERROR;
      break;
    default:
      NOTREACHED();
      return;
  }
  UMA_HISTOGRAM_ENUMERATION("Autofill.UnmaskPrompt.GetRealPanResult",
                            metric_result, NUM_PAYMENTS_RESULTS);
}

// static
void AutofillMetrics::LogRealPanDuration(
    const base::TimeDelta& duration,
    AutofillClient::PaymentsRpcResult result) {
  std::string suffix;
  switch (result) {
    case AutofillClient::SUCCESS:
      suffix = "Success";
      break;
    case AutofillClient::TRY_AGAIN_FAILURE:
    case AutofillClient::PERMANENT_FAILURE:
      suffix = "Failure";
      break;
    case AutofillClient::NETWORK_ERROR:
      suffix = "NetworkError";
      break;
    default:
      NOTREACHED();
      return;
  }
  LogUMAHistogramLongTimes("Autofill.UnmaskPrompt.GetRealPanDuration",
                           duration);
  LogUMAHistogramLongTimes("Autofill.UnmaskPrompt.GetRealPanDuration." + suffix,
                           duration);
}

// static
void AutofillMetrics::LogUnmaskingDuration(
    const base::TimeDelta& duration,
    AutofillClient::PaymentsRpcResult result) {
  std::string suffix;
  switch (result) {
    case AutofillClient::SUCCESS:
      suffix = "Success";
      break;
    case AutofillClient::TRY_AGAIN_FAILURE:
    case AutofillClient::PERMANENT_FAILURE:
      suffix = "Failure";
      break;
    case AutofillClient::NETWORK_ERROR:
      suffix = "NetworkError";
      break;
    default:
      NOTREACHED();
      return;
  }
  LogUMAHistogramLongTimes("Autofill.UnmaskPrompt.UnmaskingDuration", duration);
  LogUMAHistogramLongTimes("Autofill.UnmaskPrompt.UnmaskingDuration." + suffix,
                           duration);
}

// static
void AutofillMetrics::LogDeveloperEngagementMetric(
    DeveloperEngagementMetric metric) {
  DCHECK_LT(metric, NUM_DEVELOPER_ENGAGEMENT_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Autofill.DeveloperEngagement", metric,
                            NUM_DEVELOPER_ENGAGEMENT_METRICS);
}

// static
void AutofillMetrics::LogHeuristicTypePrediction(
    FieldTypeQualityMetric metric,
    ServerFieldType field_type,
    QualityMetricType metric_type) {
  LogTypeQualityMetric("Autofill.Quality.HeuristicType", metric, field_type,
                       metric_type);
}

// static
void AutofillMetrics::LogOverallTypePrediction(FieldTypeQualityMetric metric,
                                               ServerFieldType field_type,
                                               QualityMetricType metric_type) {
  LogTypeQualityMetric("Autofill.Quality.PredictedType", metric, field_type,
                       metric_type);
}

// static
void AutofillMetrics::LogServerTypePrediction(FieldTypeQualityMetric metric,
                                              ServerFieldType field_type,
                                              QualityMetricType metric_type) {
  LogTypeQualityMetric("Autofill.Quality.ServerType", metric, field_type,
                       metric_type);
}

// static
void AutofillMetrics::LogServerQueryMetric(ServerQueryMetric metric) {
  DCHECK_LT(metric, NUM_SERVER_QUERY_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Autofill.ServerQueryResponse", metric,
                            NUM_SERVER_QUERY_METRICS);
}

// static
void AutofillMetrics::LogUserHappinessMetric(UserHappinessMetric metric) {
  DCHECK_LT(metric, NUM_USER_HAPPINESS_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Autofill.UserHappiness", metric,
                            NUM_USER_HAPPINESS_METRICS);
}

// static
void AutofillMetrics::LogFormFillDurationFromLoadWithAutofill(
    const base::TimeDelta& duration) {
  UMA_HISTOGRAM_CUSTOM_TIMES("Autofill.FillDuration.FromLoad.WithAutofill",
                             duration,
                             base::TimeDelta::FromMilliseconds(100),
                             base::TimeDelta::FromMinutes(10),
                             50);
}

// static
void AutofillMetrics::LogFormFillDurationFromLoadWithoutAutofill(
    const base::TimeDelta& duration) {
  UMA_HISTOGRAM_CUSTOM_TIMES("Autofill.FillDuration.FromLoad.WithoutAutofill",
                             duration,
                             base::TimeDelta::FromMilliseconds(100),
                             base::TimeDelta::FromMinutes(10),
                             50);
}

// static
void AutofillMetrics::LogFormFillDurationFromInteractionWithAutofill(
    const base::TimeDelta& duration) {
  UMA_HISTOGRAM_CUSTOM_TIMES(
      "Autofill.FillDuration.FromInteraction.WithAutofill",
      duration,
      base::TimeDelta::FromMilliseconds(100),
      base::TimeDelta::FromMinutes(10),
      50);
}

// static
void AutofillMetrics::LogFormFillDurationFromInteractionWithoutAutofill(
    const base::TimeDelta& duration) {
  UMA_HISTOGRAM_CUSTOM_TIMES(
       "Autofill.FillDuration.FromInteraction.WithoutAutofill",
       duration,
       base::TimeDelta::FromMilliseconds(100),
       base::TimeDelta::FromMinutes(10),
       50);
}

// static
void AutofillMetrics::LogIsAutofillEnabledAtStartup(bool enabled) {
  UMA_HISTOGRAM_BOOLEAN("Autofill.IsEnabled.Startup", enabled);
}

// static
void AutofillMetrics::LogIsAutofillEnabledAtPageLoad(bool enabled) {
  UMA_HISTOGRAM_BOOLEAN("Autofill.IsEnabled.PageLoad", enabled);
}

// static
void AutofillMetrics::LogStoredProfileCount(size_t num_profiles) {
  UMA_HISTOGRAM_COUNTS("Autofill.StoredProfileCount", num_profiles);
}

// static
void AutofillMetrics::LogStoredLocalCreditCardCount(size_t num_local_cards) {
  UMA_HISTOGRAM_COUNTS("Autofill.StoredLocalCreditCardCount", num_local_cards);
}

// static
void AutofillMetrics::LogStoredServerCreditCardCounts(
    size_t num_masked_cards,
    size_t num_unmasked_cards) {
  UMA_HISTOGRAM_COUNTS_1000("Autofill.StoredServerCreditCardCount.Masked",
                            num_masked_cards);
  UMA_HISTOGRAM_COUNTS_1000("Autofill.StoredServerCreditCardCount.Unmasked",
                            num_unmasked_cards);
}

// static
void AutofillMetrics::LogNumberOfProfilesAtAutofillableFormSubmission(
    size_t num_profiles) {
  UMA_HISTOGRAM_COUNTS(
      "Autofill.StoredProfileCountAtAutofillableFormSubmission", num_profiles);
}

// static
void AutofillMetrics::LogHasModifiedProfileOnCreditCardFormSubmission(
    bool has_modified_profile) {
  UMA_HISTOGRAM_BOOLEAN("Autofill.HasModifiedProfile.CreditCardFormSubmission",
                        has_modified_profile);
}

// static
void AutofillMetrics::LogAddressSuggestionsCount(size_t num_suggestions) {
  UMA_HISTOGRAM_COUNTS("Autofill.AddressSuggestionsCount", num_suggestions);
}

// static
void AutofillMetrics::LogAutofillSuggestionAcceptedIndex(int index) {
  // A maximum of 50 is enforced to minimize the number of buckets generated.
  UMA_HISTOGRAM_SPARSE_SLOWLY("Autofill.SuggestionAcceptedIndex",
                              std::min(index, 50));

  base::RecordAction(base::UserMetricsAction("Autofill_SelectedSuggestion"));
}

// static
void AutofillMetrics::LogAutocompleteSuggestionAcceptedIndex(int index) {
  // A maximum of 50 is enforced to minimize the number of buckets generated.
  UMA_HISTOGRAM_SPARSE_SLOWLY("Autofill.SuggestionAcceptedIndex.Autocomplete",
                              std::min(index, 50));
}

// static
void AutofillMetrics::LogNumberOfEditedAutofilledFields(
    size_t num_edited_autofilled_fields,
    bool observed_submission) {
  if (observed_submission) {
    UMA_HISTOGRAM_COUNTS_1000(
        "Autofill.NumberOfEditedAutofilledFieldsAtSubmission",
        num_edited_autofilled_fields);
  } else {
    UMA_HISTOGRAM_COUNTS_1000(
        "Autofill.NumberOfEditedAutofilledFieldsAtSubmission.NoSubmission",
        num_edited_autofilled_fields);
  }
}

// static
void AutofillMetrics::LogServerResponseHasDataForForm(bool has_data) {
  UMA_HISTOGRAM_BOOLEAN("Autofill.ServerResponseHasDataForForm", has_data);
}

// static
void AutofillMetrics::LogProfileActionOnFormSubmitted(
    AutofillProfileAction action) {
  UMA_HISTOGRAM_ENUMERATION("Autofill.ProfileActionOnFormSubmitted", action,
                            AUTOFILL_PROFILE_ACTION_ENUM_SIZE);
}

// static
void AutofillMetrics::LogAutofillFormSubmittedState(
    AutofillFormSubmittedState state,
    AutofillMetrics::FormInteractionsUkmLogger* form_interactions_ukm_logger) {
  UMA_HISTOGRAM_ENUMERATION("Autofill.FormSubmittedState", state,
                            AUTOFILL_FORM_SUBMITTED_STATE_ENUM_SIZE);

  switch (state) {
    case NON_FILLABLE_FORM_OR_NEW_DATA:
      base::RecordAction(
          base::UserMetricsAction("Autofill_FormSubmitted_NonFillable"));
      break;

    case FILLABLE_FORM_AUTOFILLED_ALL:
      base::RecordAction(
          base::UserMetricsAction("Autofill_FormSubmitted_FilledAll"));
      break;

    case FILLABLE_FORM_AUTOFILLED_SOME:
      base::RecordAction(
          base::UserMetricsAction("Autofill_FormSubmitted_FilledSome"));
      break;

    case FILLABLE_FORM_AUTOFILLED_NONE_DID_SHOW_SUGGESTIONS:
      base::RecordAction(base::UserMetricsAction(
          "Autofill_FormSubmitted_FilledNone_SuggestionsShown"));
      break;

    case FILLABLE_FORM_AUTOFILLED_NONE_DID_NOT_SHOW_SUGGESTIONS:
      base::RecordAction(base::UserMetricsAction(
          "Autofill_FormSubmitted_FilledNone_SuggestionsNotShown"));
      break;

    default:
      NOTREACHED();
      break;
  }
  form_interactions_ukm_logger->LogFormSubmitted(state);
}

// static
void AutofillMetrics::LogDetermineHeuristicTypesTiming(
    const base::TimeDelta& duration) {
  UMA_HISTOGRAM_TIMES("Autofill.Timing.DetermineHeuristicTypes", duration);
}

// static
void AutofillMetrics::LogParseFormTiming(const base::TimeDelta& duration) {
  UMA_HISTOGRAM_TIMES("Autofill.Timing.ParseForm", duration);
}

// static
void AutofillMetrics::LogNumberOfProfilesConsideredForDedupe(
    size_t num_considered) {
  // A maximum of 50 is enforced to reduce the number of generated buckets.
  UMA_HISTOGRAM_COUNTS_1000("Autofill.NumberOfProfilesConsideredForDedupe",
                            std::min(int(num_considered), 50));
}

// static
void AutofillMetrics::LogNumberOfProfilesRemovedDuringDedupe(
    size_t num_removed) {
  // A maximum of 50 is enforced to reduce the number of generated buckets.
  UMA_HISTOGRAM_COUNTS_1000("Autofill.NumberOfProfilesRemovedDuringDedupe",
                            std::min(int(num_removed), 50));
}

// static
void AutofillMetrics::LogIsQueriedCreditCardFormSecure(bool is_secure) {
  UMA_HISTOGRAM_BOOLEAN("Autofill.QueriedCreditCardFormIsSecure", is_secure);
}

// static
void AutofillMetrics::LogWalletAddressConversionType(
    WalletAddressConversionType type) {
  DCHECK_LT(type, NUM_CONVERTED_ADDRESS_CONVERSION_TYPES);
  UMA_HISTOGRAM_ENUMERATION("Autofill.WalletAddressConversionType", type,
                            NUM_CONVERTED_ADDRESS_CONVERSION_TYPES);
}

// static
void AutofillMetrics::LogShowedHttpNotSecureExplanation() {
  base::RecordAction(
      base::UserMetricsAction("Autofill_ShowedHttpNotSecureExplanation"));
}

// static
void AutofillMetrics::LogCardUploadDecisionsUkm(ukm::UkmService* ukm_service,
                                                const GURL& url,
                                                int upload_decision_metrics) {
  DCHECK(upload_decision_metrics);
  DCHECK_LT(upload_decision_metrics, 1 << kNumCardUploadDecisionMetrics);

  const std::vector<std::pair<const char*, int>> metrics = {
      {internal::kUKMCardUploadDecisionMetricName, upload_decision_metrics}};
  LogUkm(ukm_service, url, internal::kUKMCardUploadDecisionEntryName, metrics);
}

// static
void AutofillMetrics::LogDeveloperEngagementUkm(
    ukm::UkmService* ukm_service,
    const GURL& url,
    int developer_engagement_metrics) {
  DCHECK(developer_engagement_metrics);
  DCHECK_LT(developer_engagement_metrics,
            1 << NUM_DEVELOPER_ENGAGEMENT_METRICS);

  const std::vector<std::pair<const char*, int>> metrics = {
      {internal::kUKMDeveloperEngagementMetricName,
       developer_engagement_metrics}};

  LogUkm(ukm_service, url, internal::kUKMDeveloperEngagementEntryName, metrics);
}

// static
bool AutofillMetrics::LogUkm(
    ukm::UkmService* ukm_service,
    const GURL& url,
    const std::string& ukm_entry_name,
    const std::vector<std::pair<const char*, int>>& metrics) {
  if (!IsUkmLoggingEnabled() || !ukm_service || !url.is_valid() ||
      metrics.empty()) {
    return false;
  }

  int32_t source_id = ukm_service->GetNewSourceID();
  ukm_service->UpdateSourceURL(source_id, url);
  std::unique_ptr<ukm::UkmEntryBuilder> builder =
      ukm_service->GetEntryBuilder(source_id, ukm_entry_name.c_str());

  for (auto it = metrics.begin(); it != metrics.end(); ++it) {
    builder->AddMetric(it->first, it->second);
  }

  return true;
}

AutofillMetrics::FormEventLogger::FormEventLogger(
    bool is_for_credit_card,
    AutofillMetrics::FormInteractionsUkmLogger* form_interactions_ukm_logger)
    : is_for_credit_card_(is_for_credit_card),
      server_record_type_count_(0),
      local_record_type_count_(0),
      is_context_secure_(false),
      has_logged_interacted_(false),
      has_logged_suggestions_shown_(false),
      has_logged_masked_server_card_suggestion_selected_(false),
      has_logged_suggestion_filled_(false),
      has_logged_will_submit_(false),
      has_logged_submitted_(false),
      logged_suggestion_filled_was_server_data_(false),
      logged_suggestion_filled_was_masked_server_card_(false),
      form_interactions_ukm_logger_(form_interactions_ukm_logger) {}

void AutofillMetrics::FormEventLogger::OnDidInteractWithAutofillableForm() {
  if (!has_logged_interacted_) {
    has_logged_interacted_ = true;
    form_interactions_ukm_logger_->LogInteractedWithForm(
        is_for_credit_card_, local_record_type_count_,
        server_record_type_count_);
    Log(AutofillMetrics::FORM_EVENT_INTERACTED_ONCE);
  }
}

void AutofillMetrics::FormEventLogger::OnDidPollSuggestions(
    const FormFieldData& field) {
  // Record only one poll user action for consecutive polls of the same field.
  // This is to avoid recording too many poll actions (for example when a user
  // types in a field, triggering multiple queries) to make the analysis more
  // simple.
  if (!field.SameFieldAs(last_polled_field_)) {
    if (is_for_credit_card_) {
      base::RecordAction(
          base::UserMetricsAction("Autofill_PolledCreditCardSuggestions"));
    } else {
      base::RecordAction(
          base::UserMetricsAction("Autofill_PolledProfileSuggestions"));
    }

    last_polled_field_ = field;
  }
}

void AutofillMetrics::FormEventLogger::OnDidShowSuggestions() {
  form_interactions_ukm_logger_->LogSuggestionsShown();

  Log(AutofillMetrics::FORM_EVENT_SUGGESTIONS_SHOWN);
  if (!has_logged_suggestions_shown_) {
    has_logged_suggestions_shown_ = true;
    Log(AutofillMetrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE);
  }

  if (is_for_credit_card_) {
    base::RecordAction(
        base::UserMetricsAction("Autofill_ShowedCreditCardSuggestions"));
  } else {
    base::RecordAction(
        base::UserMetricsAction("Autofill_ShowedProfileSuggestions"));
  }
}

void AutofillMetrics::FormEventLogger::OnDidSelectMaskedServerCardSuggestion() {
  DCHECK(is_for_credit_card_);
  form_interactions_ukm_logger_->LogSelectedMaskedServerCard();

  Log(AutofillMetrics::FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SELECTED);
  if (!has_logged_masked_server_card_suggestion_selected_) {
    has_logged_masked_server_card_suggestion_selected_ = true;
    Log(AutofillMetrics::
            FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SELECTED_ONCE);
  }
}

void AutofillMetrics::FormEventLogger::OnDidFillSuggestion(
    const CreditCard& credit_card) {
  DCHECK(is_for_credit_card_);
  form_interactions_ukm_logger_->LogDidFillSuggestion(
      static_cast<int>(credit_card.record_type()));

  if (credit_card.record_type() == CreditCard::MASKED_SERVER_CARD)
    Log(AutofillMetrics::FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED);
  else if (credit_card.record_type() == CreditCard::FULL_SERVER_CARD)
    Log(AutofillMetrics::FORM_EVENT_SERVER_SUGGESTION_FILLED);
  else
    Log(AutofillMetrics::FORM_EVENT_LOCAL_SUGGESTION_FILLED);

  if (!has_logged_suggestion_filled_) {
    has_logged_suggestion_filled_ = true;
    logged_suggestion_filled_was_server_data_ =
        credit_card.record_type() == CreditCard::MASKED_SERVER_CARD ||
        credit_card.record_type() == CreditCard::FULL_SERVER_CARD;
    logged_suggestion_filled_was_masked_server_card_ =
        credit_card.record_type() == CreditCard::MASKED_SERVER_CARD;
    if (credit_card.record_type() == CreditCard::MASKED_SERVER_CARD) {
      Log(AutofillMetrics::
              FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED_ONCE);
    } else if (credit_card.record_type() == CreditCard::FULL_SERVER_CARD) {
      Log(AutofillMetrics::FORM_EVENT_SERVER_SUGGESTION_FILLED_ONCE);
    } else {
      Log(AutofillMetrics::FORM_EVENT_LOCAL_SUGGESTION_FILLED_ONCE);
    }
  }

  base::RecordAction(
      base::UserMetricsAction("Autofill_FilledCreditCardSuggestion"));
}

void AutofillMetrics::FormEventLogger::OnDidFillSuggestion(
    const AutofillProfile& profile) {
  DCHECK(!is_for_credit_card_);
  form_interactions_ukm_logger_->LogDidFillSuggestion(
      static_cast<int>(profile.record_type()));

  if (profile.record_type() == AutofillProfile::SERVER_PROFILE)
    Log(AutofillMetrics::FORM_EVENT_SERVER_SUGGESTION_FILLED);
  else
    Log(AutofillMetrics::FORM_EVENT_LOCAL_SUGGESTION_FILLED);

  if (!has_logged_suggestion_filled_) {
    has_logged_suggestion_filled_ = true;
    logged_suggestion_filled_was_server_data_ =
        profile.record_type() == AutofillProfile::SERVER_PROFILE;
    Log(profile.record_type() == AutofillProfile::SERVER_PROFILE
            ? AutofillMetrics::FORM_EVENT_SERVER_SUGGESTION_FILLED_ONCE
            : AutofillMetrics::FORM_EVENT_LOCAL_SUGGESTION_FILLED_ONCE);
  }

  base::RecordAction(
      base::UserMetricsAction("Autofill_FilledProfileSuggestion"));
}

void AutofillMetrics::FormEventLogger::OnWillSubmitForm() {
  // Not logging this kind of form if we haven't logged a user interaction.
  if (!has_logged_interacted_)
    return;

  // Not logging twice.
  if (has_logged_will_submit_)
    return;
  has_logged_will_submit_ = true;

  if (!has_logged_suggestion_filled_) {
    Log(AutofillMetrics::FORM_EVENT_NO_SUGGESTION_WILL_SUBMIT_ONCE);
  } else if (logged_suggestion_filled_was_masked_server_card_) {
    Log(AutofillMetrics::
            FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_WILL_SUBMIT_ONCE);
  } else if (logged_suggestion_filled_was_server_data_) {
    Log(AutofillMetrics::FORM_EVENT_SERVER_SUGGESTION_WILL_SUBMIT_ONCE);
  } else {
    Log(AutofillMetrics::FORM_EVENT_LOCAL_SUGGESTION_WILL_SUBMIT_ONCE);
  }

  if (has_logged_suggestions_shown_) {
    Log(AutofillMetrics::FORM_EVENT_SUGGESTION_SHOWN_WILL_SUBMIT_ONCE);
  }

  base::RecordAction(base::UserMetricsAction("Autofill_OnWillSubmitForm"));
}

void AutofillMetrics::FormEventLogger::OnFormSubmitted() {
  // Not logging this kind of form if we haven't logged a user interaction.
  if (!has_logged_interacted_)
    return;

  // Not logging twice.
  if (has_logged_submitted_)
    return;
  has_logged_submitted_ = true;

  if (!has_logged_suggestion_filled_) {
    Log(AutofillMetrics::FORM_EVENT_NO_SUGGESTION_SUBMITTED_ONCE);
  } else if (logged_suggestion_filled_was_masked_server_card_) {
    Log(AutofillMetrics::
            FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SUBMITTED_ONCE);
  } else if (logged_suggestion_filled_was_server_data_) {
    Log(AutofillMetrics::FORM_EVENT_SERVER_SUGGESTION_SUBMITTED_ONCE);
  } else {
    Log(AutofillMetrics::FORM_EVENT_LOCAL_SUGGESTION_SUBMITTED_ONCE);
  }

  if (has_logged_suggestions_shown_) {
    Log(AutofillMetrics::FORM_EVENT_SUGGESTION_SHOWN_SUBMITTED_ONCE);
  }
}

void AutofillMetrics::FormEventLogger::Log(FormEvent event) const {
  DCHECK_LT(event, NUM_FORM_EVENTS);
  std::string name("Autofill.FormEvents.");
  if (is_for_credit_card_)
    name += "CreditCard";
  else
    name += "Address";
  LogUMAHistogramEnumeration(name, event, NUM_FORM_EVENTS);

  // Log again in a different histogram for credit card forms on nonsecure
  // pages, so that form interactions on nonsecure pages can be analyzed on
  // their own.
  if (is_for_credit_card_ && !is_context_secure_) {
    LogUMAHistogramEnumeration(name + ".OnNonsecurePage", event,
                               NUM_FORM_EVENTS);
  }

  // Logging again in a different histogram for segmentation purposes.
  // TODO(waltercacau): Re-evaluate if we still need such fine grained
  // segmentation. http://crbug.com/454018
  if (server_record_type_count_ == 0 && local_record_type_count_ == 0)
    name += ".WithNoData";
  else if (server_record_type_count_ > 0 && local_record_type_count_ == 0)
    name += ".WithOnlyServerData";
  else if (server_record_type_count_ == 0 && local_record_type_count_ > 0)
    name += ".WithOnlyLocalData";
  else
    name += ".WithBothServerAndLocalData";
  LogUMAHistogramEnumeration(name, event, NUM_FORM_EVENTS);
}

AutofillMetrics::FormInteractionsUkmLogger::FormInteractionsUkmLogger(
    ukm::UkmService* ukm_service)
    : ukm_service_(ukm_service) {}

void AutofillMetrics::FormInteractionsUkmLogger::OnFormsParsed(
    const GURL& url) {
  if (!IsUkmLoggingEnabled() || ukm_service_ == nullptr)
    return;

  url_ = url;
  form_parsed_timestamp_ = base::TimeTicks::Now();
}

void AutofillMetrics::FormInteractionsUkmLogger::LogInteractedWithForm(
    bool is_for_credit_card,
    size_t local_record_type_count,
    size_t server_record_type_count) {
  if (!CanLog())
    return;

  if (source_id_ == -1)
    GetNewSourceID();

  std::unique_ptr<ukm::UkmEntryBuilder> builder = ukm_service_->GetEntryBuilder(
      source_id_, internal::kUKMInteractedWithFormEntryName);
  builder->AddMetric(internal::kUKMIsForCreditCardMetricName,
                     is_for_credit_card);
  builder->AddMetric(internal::kUKMLocalRecordTypeCountMetricName,
                     local_record_type_count);
  builder->AddMetric(internal::kUKMServerRecordTypeCountMetricName,
                     server_record_type_count);
}

void AutofillMetrics::FormInteractionsUkmLogger::LogSuggestionsShown() {
  if (!CanLog())
    return;

  if (source_id_ == -1)
    GetNewSourceID();

  std::unique_ptr<ukm::UkmEntryBuilder> builder = ukm_service_->GetEntryBuilder(
      source_id_, internal::kUKMSuggestionsShownEntryName);
  builder->AddMetric(internal::kUKMMillisecondsSinceFormParsedMetricName,
                     MillisecondsSinceFormParsed());
}

void AutofillMetrics::FormInteractionsUkmLogger::LogSelectedMaskedServerCard() {
  if (!CanLog())
    return;

  if (source_id_ == -1)
    GetNewSourceID();

  std::unique_ptr<ukm::UkmEntryBuilder> builder = ukm_service_->GetEntryBuilder(
      source_id_, internal::kUKMSelectedMaskedServerCardEntryName);
  builder->AddMetric(internal::kUKMMillisecondsSinceFormParsedMetricName,
                     MillisecondsSinceFormParsed());
}

void AutofillMetrics::FormInteractionsUkmLogger::LogDidFillSuggestion(
    int record_type) {
  if (!CanLog())
    return;

  if (source_id_ == -1)
    GetNewSourceID();

  std::unique_ptr<ukm::UkmEntryBuilder> builder = ukm_service_->GetEntryBuilder(
      source_id_, internal::kUKMSuggestionFilledEntryName);
  builder->AddMetric(internal::kUKMRecordTypeMetricName, record_type);
  builder->AddMetric(internal::kUKMMillisecondsSinceFormParsedMetricName,
                     MillisecondsSinceFormParsed());
}

void AutofillMetrics::FormInteractionsUkmLogger::LogTextFieldDidChange(
    const AutofillField& field) {
  if (!CanLog())
    return;

  if (source_id_ == -1)
    GetNewSourceID();

  std::unique_ptr<ukm::UkmEntryBuilder> builder = ukm_service_->GetEntryBuilder(
      source_id_, internal::kUKMTextFieldDidChangeEntryName);
  builder->AddMetric(internal::kUKMFieldTypeGroupMetricName,
                     static_cast<int>(field.Type().group()));
  builder->AddMetric(internal::kUKMHeuristicTypeMetricName,
                     static_cast<int>(field.heuristic_type()));
  builder->AddMetric(internal::kUKMServerTypeMetricName,
                     static_cast<int>(field.server_type()));
  builder->AddMetric(internal::kUKMHtmlFieldTypeMetricName,
                     static_cast<int>(field.html_type()));
  builder->AddMetric(internal::kUKMHtmlFieldModeMetricName,
                     static_cast<int>(field.html_mode()));
  builder->AddMetric(internal::kUKMIsAutofilledMetricName, field.is_autofilled);
  builder->AddMetric(internal::kUKMIsEmptyMetricName, field.IsEmpty());
  builder->AddMetric(internal::kUKMMillisecondsSinceFormParsedMetricName,
                     MillisecondsSinceFormParsed());
}

void AutofillMetrics::FormInteractionsUkmLogger::LogFormSubmitted(
    AutofillFormSubmittedState state) {
  if (!CanLog())
    return;

  if (source_id_ == -1)
    GetNewSourceID();

  std::unique_ptr<ukm::UkmEntryBuilder> builder = ukm_service_->GetEntryBuilder(
      source_id_, internal::kUKMFormSubmittedEntryName);
  builder->AddMetric(internal::kUKMAutofillFormSubmittedStateMetricName,
                     static_cast<int>(state));
  if (form_parsed_timestamp_.is_null())
    DCHECK_EQ(state, NON_FILLABLE_FORM_OR_NEW_DATA);
  else
    builder->AddMetric(internal::kUKMMillisecondsSinceFormParsedMetricName,
                       MillisecondsSinceFormParsed());
}

void AutofillMetrics::FormInteractionsUkmLogger::UpdateSourceURL(
    const GURL& url) {
  url_ = url;
  if (CanLog())
    ukm_service_->UpdateSourceURL(source_id_, url_);
}

bool AutofillMetrics::FormInteractionsUkmLogger::CanLog() const {
  return IsUkmLoggingEnabled() && ukm_service_ && url_.is_valid();
}

int64_t
AutofillMetrics::FormInteractionsUkmLogger::MillisecondsSinceFormParsed()
    const {
  DCHECK(!form_parsed_timestamp_.is_null());
  return (base::TimeTicks::Now() - form_parsed_timestamp_).InMilliseconds();
}

void AutofillMetrics::FormInteractionsUkmLogger::GetNewSourceID() {
  source_id_ = ukm_service_->GetNewSourceID();
  ukm_service_->UpdateSourceURL(source_id_, url_);
}

}  // namespace autofill
