// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/page_load_metrics_observer_test_harness.h"

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/page_load_metrics/page_load_metrics_embedder_interface.h"
#include "chrome/browser/page_load_metrics/page_load_metrics_util.h"
#include "chrome/common/page_load_metrics/page_load_metrics_messages.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_data.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/web_contents_tester.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"

namespace page_load_metrics {

namespace {

class TestPageLoadMetricsEmbedderInterface
    : public PageLoadMetricsEmbedderInterface {
 public:
  explicit TestPageLoadMetricsEmbedderInterface(
      PageLoadMetricsObserverTestHarness* test)
      : test_(test) {}

  bool IsNewTabPageUrl(const GURL& url) override { return false; }

  // Forward the registration logic to the test class so that derived classes
  // can override the logic there without depending on the embedder interface.
  void RegisterObservers(PageLoadTracker* tracker) override {
    test_->RegisterObservers(tracker);
  }

 private:
  PageLoadMetricsObserverTestHarness* test_;

  DISALLOW_COPY_AND_ASSIGN(TestPageLoadMetricsEmbedderInterface);
};

}  // namespace

PageLoadMetricsObserverTestHarness::PageLoadMetricsObserverTestHarness()
    : ChromeRenderViewHostTestHarness() {}

PageLoadMetricsObserverTestHarness::~PageLoadMetricsObserverTestHarness() {}

// static
void PageLoadMetricsObserverTestHarness::PopulateRequiredTimingFields(
    mojom::PageLoadTiming* inout_timing) {
  if (inout_timing->paint_timing->first_meaningful_paint &&
      !inout_timing->paint_timing->first_contentful_paint) {
    inout_timing->paint_timing->first_contentful_paint =
        inout_timing->paint_timing->first_meaningful_paint;
  }
  if ((inout_timing->paint_timing->first_text_paint ||
       inout_timing->paint_timing->first_image_paint ||
       inout_timing->paint_timing->first_contentful_paint) &&
      !inout_timing->paint_timing->first_paint) {
    inout_timing->paint_timing->first_paint =
        OptionalMin(OptionalMin(inout_timing->paint_timing->first_text_paint,
                                inout_timing->paint_timing->first_image_paint),
                    inout_timing->paint_timing->first_contentful_paint);
  }
  if (inout_timing->paint_timing->first_paint &&
      !inout_timing->document_timing->first_layout) {
    inout_timing->document_timing->first_layout =
        inout_timing->paint_timing->first_paint;
  }
  if (inout_timing->document_timing->load_event_start &&
      !inout_timing->document_timing->dom_content_loaded_event_start) {
    inout_timing->document_timing->dom_content_loaded_event_start =
        inout_timing->document_timing->load_event_start;
  }
  if (inout_timing->document_timing->first_layout &&
      !inout_timing->parse_timing->parse_start) {
    inout_timing->parse_timing->parse_start =
        inout_timing->document_timing->first_layout;
  }
  if (inout_timing->document_timing->dom_content_loaded_event_start &&
      !inout_timing->parse_timing->parse_stop) {
    inout_timing->parse_timing->parse_stop =
        inout_timing->document_timing->dom_content_loaded_event_start;
  }
  if (inout_timing->parse_timing->parse_stop &&
      !inout_timing->parse_timing->parse_start) {
    inout_timing->parse_timing->parse_start =
        inout_timing->parse_timing->parse_stop;
  }
  if (inout_timing->parse_timing->parse_start &&
      !inout_timing->response_start) {
    inout_timing->response_start = inout_timing->parse_timing->parse_start;
  }
  if (inout_timing->parse_timing->parse_start) {
    if (!inout_timing->parse_timing->parse_blocked_on_script_load_duration)
      inout_timing->parse_timing->parse_blocked_on_script_load_duration =
          base::TimeDelta();
    if (!inout_timing->parse_timing
             ->parse_blocked_on_script_execution_duration) {
      inout_timing->parse_timing->parse_blocked_on_script_execution_duration =
          base::TimeDelta();
    }
    if (!inout_timing->parse_timing
             ->parse_blocked_on_script_load_from_document_write_duration) {
      inout_timing->parse_timing
          ->parse_blocked_on_script_load_from_document_write_duration =
          base::TimeDelta();
    }
    if (!inout_timing->parse_timing
             ->parse_blocked_on_script_execution_from_document_write_duration) {
      inout_timing->parse_timing
          ->parse_blocked_on_script_execution_from_document_write_duration =
          base::TimeDelta();
    }
  }
}

void PageLoadMetricsObserverTestHarness::SetUp() {
  ChromeRenderViewHostTestHarness::SetUp();
  SetContents(CreateTestWebContents());
  NavigateAndCommit(GURL("http://www.google.com"));
  observer_ = MetricsWebContentsObserver::CreateForWebContents(
      web_contents(),
      base::MakeUnique<TestPageLoadMetricsEmbedderInterface>(this));
  web_contents()->WasShown();
}

void PageLoadMetricsObserverTestHarness::StartNavigation(const GURL& gurl) {
  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->StartNavigation(gurl);
}

void PageLoadMetricsObserverTestHarness::SimulateTimingUpdate(
    const mojom::PageLoadTiming& timing) {
  SimulateTimingAndMetadataUpdate(timing, mojom::PageLoadMetadata());
}

void PageLoadMetricsObserverTestHarness::SimulateTimingAndMetadataUpdate(
    const mojom::PageLoadTiming& timing,
    const mojom::PageLoadMetadata& metadata) {
  observer_->OnTimingUpdated(web_contents()->GetMainFrame(), timing, metadata);
}

void PageLoadMetricsObserverTestHarness::SimulateStartedResource(
    const ExtraRequestStartInfo& info) {
  observer_->OnRequestStarted(content::GlobalRequestID(), info.resource_type,
                              base::TimeTicks::Now());
}

void PageLoadMetricsObserverTestHarness::SimulateLoadedResource(
    const ExtraRequestCompleteInfo& info) {
  observer_->OnRequestComplete(
      info.url, info.frame_tree_node_id, content::GlobalRequestID(),
      info.resource_type, info.was_cached,
      info.data_reduction_proxy_data
          ? info.data_reduction_proxy_data->DeepCopy()
          : nullptr,
      info.raw_body_bytes, info.original_network_content_length,
      base::TimeTicks::Now());
}

void PageLoadMetricsObserverTestHarness::SimulateInputEvent(
    const blink::WebInputEvent& event) {
  observer_->OnInputEvent(event);
}

void PageLoadMetricsObserverTestHarness::SimulateAppEnterBackground() {
  observer_->FlushMetricsOnAppEnterBackground();
}

void PageLoadMetricsObserverTestHarness::SimulateMediaPlayed() {
  content::WebContentsObserver::MediaPlayerInfo video_type(
      true /* in_has_video*/);
  content::RenderFrameHost* render_frame_host = web_contents()->GetMainFrame();
  observer_->MediaStartedPlaying(video_type,
                                 std::make_pair(render_frame_host, 0));
}

const base::HistogramTester&
PageLoadMetricsObserverTestHarness::histogram_tester() const {
  return histogram_tester_;
}

MetricsWebContentsObserver* PageLoadMetricsObserverTestHarness::observer()
    const {
  return observer_;
}

const PageLoadExtraInfo
PageLoadMetricsObserverTestHarness::GetPageLoadExtraInfoForCommittedLoad() {
  return observer_->GetPageLoadExtraInfoForCommittedLoad();
}

void PageLoadMetricsObserverTestHarness::NavigateWithPageTransitionAndCommit(
    const GURL& url,
    ui::PageTransition transition) {
  controller().LoadURL(url, content::Referrer(), transition, std::string());
  content::WebContentsTester::For(web_contents())->CommitPendingNavigation();
}

}  // namespace page_load_metrics
