// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_PUBLIC_TEST_FAKES_CRW_TEST_WEB_STATE_OBSERVER_H_
#define IOS_WEB_PUBLIC_TEST_FAKES_CRW_TEST_WEB_STATE_OBSERVER_H_

#include "ios/web/public/test/fakes/test_web_state_observer_util.h"
#import "ios/web/public/web_state/web_state_observer_bridge.h"

// Test implementation of CRWWebStateObserver protocol.
@interface CRWTestWebStateObserver : NSObject<CRWWebStateObserver>

// Arguments passed to |webState:didStartProvisionalNavigationForURL:|.
@property(nonatomic, readonly)
    web::TestStartProvisionalNavigationInfo* startProvisionalNavigationInfo;
// Arguments passed to |webState:didFinishNavigation:|.
@property(nonatomic, readonly)
    web::TestDidFinishNavigationInfo* didFinishNavigationInfo;
// Arguments passed to |webState:didCommitNavigationWithDetails:|.
@property(nonatomic, readonly)
    web::TestCommitNavigationInfo* commitNavigationInfo;
// Arguments passed to |webState:didLoadPageWithSuccess:|.
@property(nonatomic, readonly) web::TestLoadPageInfo* loadPageInfo;
// Arguments passed to |webStateDidDismissInterstitial:|.
@property(nonatomic, readonly)
    web::TestDismissInterstitialInfo* dismissInterstitialInfo;
// Arguments passed to |webState:didChangeLoadingProgress:|.
@property(nonatomic, readonly)
    web::TestChangeLoadingProgressInfo* changeLoadingProgressInfo;
// Arguments passed to |webStateDidChangeTitle:|.
@property(nonatomic, readonly) web::TestTitleWasSetInfo* titleWasSetInfo;
// Arguments passed to |webStateDidChangeVisibleSecurityState:|.
@property(nonatomic, readonly) web::TestDidChangeVisibleSecurityStateInfo*
    didChangeVisibleSecurityStateInfo;
// Arguments passed to |webStateDidSuppressDialog:|.
@property(nonatomic, readonly)
    web::TestDidSuppressDialogInfo* didSuppressDialogInfo;
// Arguments passed to |webState:didSubmitDocumentWithFormNamed:userInitiated:|.
@property(nonatomic, readonly) web::TestSubmitDocumentInfo* submitDocumentInfo;
// Arguments passed to
// |webState:didRegisterFormActivityWithFormNamed:fieldName:type:value:|.
@property(nonatomic, readonly) web::TestFormActivityInfo* formActivityInfo;
// Arguments passed to |webState:didUpdateFaviconURLCandidates|.
@property(nonatomic, readonly)
    web::TestUpdateFaviconUrlCandidatesInfo* updateFaviconUrlCandidatesInfo;
// Arguments passed to |webState:renderProcessGoneForWebState:|.
@property(nonatomic, readonly)
    web::TestRenderProcessGoneInfo* renderProcessGoneInfo;
// Arguments passed to |webStateDestroyed:|.
@property(nonatomic, readonly)
    web::TestWebStateDestroyedInfo* webStateDestroyedInfo;
// Arguments passed to |webStateDidStopLoading:|.
@property(nonatomic, readonly) web::TestStopLoadingInfo* stopLoadingInfo;
// Arguments passed to |webStateDidStartLoading:|.
@property(nonatomic, readonly) web::TestStartLoadingInfo* startLoadingInfo;

@end

#endif  // IOS_WEB_PUBLIC_TEST_FAKES_CRW_TEST_WEB_STATE_OBSERVER_H_
