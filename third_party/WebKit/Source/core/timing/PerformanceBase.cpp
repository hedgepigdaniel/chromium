/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2012 Intel Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/timing/PerformanceBase.h"

#include <algorithm>
#include "core/dom/Document.h"
#include "core/dom/DocumentTiming.h"
#include "core/events/Event.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/UseCounter.h"
#include "core/loader/DocumentLoadTiming.h"
#include "core/loader/DocumentLoader.h"
#include "core/timing/PerformanceLongTaskTiming.h"
#include "core/timing/PerformanceObserver.h"
#include "core/timing/PerformanceResourceTiming.h"
#include "core/timing/PerformanceServerTiming.h"
#include "core/timing/PerformanceUserTiming.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/loader/fetch/ResourceResponse.h"
#include "platform/loader/fetch/ResourceTimingInfo.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "platform/wtf/CurrentTime.h"

namespace blink {

namespace {

SecurityOrigin* GetSecurityOrigin(ExecutionContext* context) {
  if (context)
    return context->GetSecurityOrigin();
  return nullptr;
}

}  // namespace

using PerformanceObserverVector = HeapVector<Member<PerformanceObserver>>;

static const size_t kDefaultResourceTimingBufferSize = 150;
static const size_t kDefaultFrameTimingBufferSize = 150;
static const size_t kServerTimingBufferSize = 150;

PerformanceBase::PerformanceBase(double time_origin,
                                 RefPtr<WebTaskRunner> task_runner)
    : frame_timing_buffer_size_(kDefaultFrameTimingBufferSize),
      resource_timing_buffer_size_(kDefaultResourceTimingBufferSize),
      user_timing_(nullptr),
      time_origin_(time_origin),
      observer_filter_options_(PerformanceEntry::kInvalid),
      deliver_observations_timer_(
          std::move(task_runner),
          this,
          &PerformanceBase::DeliverObservationsTimerFired) {}

PerformanceBase::~PerformanceBase() {}

const AtomicString& PerformanceBase::InterfaceName() const {
  return EventTargetNames::Performance;
}

PerformanceTiming* PerformanceBase::timing() const {
  return nullptr;
}

PerformanceEntryVector PerformanceBase::getEntries() {
  PerformanceEntryVector entries;

  entries.AppendVector(resource_timing_buffer_);
  if (!navigation_timing_)
    navigation_timing_ = CreateNavigationTimingInstance();
  // This extra checking is needed when WorkerPerformance
  // calls this method.
  if (navigation_timing_)
    entries.push_back(navigation_timing_);
  entries.AppendVector(frame_timing_buffer_);

  if (user_timing_) {
    entries.AppendVector(user_timing_->GetMarks());
    entries.AppendVector(user_timing_->GetMeasures());
  }

  entries.AppendVector(server_timing_buffer_);

  std::sort(entries.begin(), entries.end(),
            PerformanceEntry::StartTimeCompareLessThan);
  return entries;
}

PerformanceEntryVector PerformanceBase::getEntriesByType(
    const String& entry_type) {
  PerformanceEntryVector entries;
  PerformanceEntry::EntryType type =
      PerformanceEntry::ToEntryTypeEnum(entry_type);

  switch (type) {
    case PerformanceEntry::kResource:
      for (const auto& resource : resource_timing_buffer_)
        entries.push_back(resource);
      break;
    case PerformanceEntry::kNavigation:
      if (!navigation_timing_)
        navigation_timing_ = CreateNavigationTimingInstance();
      if (navigation_timing_)
        entries.push_back(navigation_timing_);
      break;
    case PerformanceEntry::kComposite:
    case PerformanceEntry::kRender:
      for (const auto& frame : frame_timing_buffer_) {
        if (type == frame->EntryTypeEnum()) {
          entries.push_back(frame);
        }
      }
      break;
    case PerformanceEntry::kMark:
      if (user_timing_)
        entries.AppendVector(user_timing_->GetMarks());
      break;
    case PerformanceEntry::kMeasure:
      if (user_timing_)
        entries.AppendVector(user_timing_->GetMeasures());
      break;
    case PerformanceEntry::kServer:
      entries.AppendVector(server_timing_buffer_);
      break;
    // Unsupported for Paint, LongTask, TaskAttribution.
    // Per the spec, these entries can only be accessed via
    // Performance Observer. No separate buffer is maintained.
    case PerformanceEntry::kPaint:
      break;
    case PerformanceEntry::kLongTask:
      break;
    case PerformanceEntry::kTaskAttribution:
      break;
    case PerformanceEntry::kInvalid:
      break;
  }

  std::sort(entries.begin(), entries.end(),
            PerformanceEntry::StartTimeCompareLessThan);
  return entries;
}

PerformanceEntryVector PerformanceBase::getEntriesByName(
    const String& name,
    const String& entry_type) {
  PerformanceEntryVector entries;
  PerformanceEntry::EntryType type =
      PerformanceEntry::ToEntryTypeEnum(entry_type);

  if (!entry_type.IsNull() && type == PerformanceEntry::kInvalid)
    return entries;

  if (entry_type.IsNull() || type == PerformanceEntry::kResource) {
    for (const auto& resource : resource_timing_buffer_) {
      if (resource->name() == name)
        entries.push_back(resource);
    }
  }

  if (entry_type.IsNull() || type == PerformanceEntry::kNavigation) {
    if (!navigation_timing_)
      navigation_timing_ = CreateNavigationTimingInstance();
    if (navigation_timing_ && navigation_timing_->name() == name)
      entries.push_back(navigation_timing_);
  }

  if (entry_type.IsNull() || type == PerformanceEntry::kComposite ||
      type == PerformanceEntry::kRender) {
    for (const auto& frame : frame_timing_buffer_) {
      if (frame->name() == name &&
          (entry_type.IsNull() || entry_type == frame->entryType()))
        entries.push_back(frame);
    }
  }

  if (user_timing_) {
    if (entry_type.IsNull() || type == PerformanceEntry::kMark)
      entries.AppendVector(user_timing_->GetMarks(name));
    if (entry_type.IsNull() || type == PerformanceEntry::kMeasure)
      entries.AppendVector(user_timing_->GetMeasures(name));
  }

  if (entry_type.IsNull() || type == PerformanceEntry::kServer) {
    // This is inefficient, but this buffer has a max size of
    // 150 entries (controlled by kServerTimingBufferSize).
    for (const auto& entry : server_timing_buffer_) {
      if (entry->name() == name)
        entries.push_back(entry);
    }
  }

  std::sort(entries.begin(), entries.end(),
            PerformanceEntry::StartTimeCompareLessThan);
  return entries;
}

void PerformanceBase::clearResourceTimings() {
  resource_timing_buffer_.clear();
}

void PerformanceBase::setResourceTimingBufferSize(unsigned size) {
  resource_timing_buffer_size_ = size;
  if (IsResourceTimingBufferFull())
    DispatchEvent(Event::Create(EventTypeNames::resourcetimingbufferfull));
}

void PerformanceBase::clearFrameTimings() {
  frame_timing_buffer_.clear();
}

void PerformanceBase::setFrameTimingBufferSize(unsigned size) {
  frame_timing_buffer_size_ = size;
  if (IsFrameTimingBufferFull())
    DispatchEvent(Event::Create(EventTypeNames::frametimingbufferfull));
}

bool PerformanceBase::PassesTimingAllowCheck(
    const ResourceResponse& response,
    const SecurityOrigin& initiator_security_origin,
    const AtomicString& original_timing_allow_origin,
    ExecutionContext* context) {
  RefPtr<SecurityOrigin> resource_origin =
      SecurityOrigin::Create(response.Url());
  if (resource_origin->IsSameSchemeHostPort(&initiator_security_origin))
    return true;

  const AtomicString& timing_allow_origin_string =
      original_timing_allow_origin.IsEmpty()
          ? response.HttpHeaderField(HTTPNames::Timing_Allow_Origin)
          : original_timing_allow_origin;
  if (timing_allow_origin_string.IsEmpty() ||
      EqualIgnoringASCIICase(timing_allow_origin_string, "null"))
    return false;

  if (timing_allow_origin_string == "*") {
    UseCounter::Count(context, UseCounter::kStarInTimingAllowOrigin);
    return true;
  }

  const String& security_origin = initiator_security_origin.ToString();
  Vector<String> timing_allow_origins;
  timing_allow_origin_string.GetString().Split(' ', timing_allow_origins);
  if (timing_allow_origins.size() > 1)
    UseCounter::Count(context, UseCounter::kMultipleOriginsInTimingAllowOrigin);
  else if (timing_allow_origins.size() == 1)
    UseCounter::Count(context, UseCounter::kSingleOriginInTimingAllowOrigin);
  for (const String& allow_origin : timing_allow_origins) {
    if (allow_origin == security_origin)
      return true;
  }

  return false;
}

bool PerformanceBase::AllowsTimingRedirect(
    const Vector<ResourceResponse>& redirect_chain,
    const ResourceResponse& final_response,
    const SecurityOrigin& initiator_security_origin,
    ExecutionContext* context) {
  if (!PassesTimingAllowCheck(final_response, initiator_security_origin,
                              AtomicString(), context))
    return false;

  for (const ResourceResponse& response : redirect_chain) {
    if (!PassesTimingAllowCheck(response, initiator_security_origin,
                                AtomicString(), context))
      return false;
  }

  return true;
}

void PerformanceBase::AddServerTiming(const ResourceResponse& response,
                                      ShouldAddToBuffer shouldAddToBuffer) {
  if (shouldAddToBuffer == ShouldAddToBuffer::Never &&
      !HasObserverFor(PerformanceEntry::kServer)) {
    return;
  }

  ExecutionContext* context = GetExecutionContext();
  SecurityOrigin* securityOrigin = GetSecurityOrigin(context);
  if (!securityOrigin) {
    return;
  }
  bool allowTimingDetails = PassesTimingAllowCheck(
      response, *securityOrigin,
      response.HttpHeaderField(HTTPNames::Timing_Allow_Origin), context);

  std::unique_ptr<ServerTimingHeaderVector> headers = ParseServerTimingHeader(
      response.HttpHeaderField(HTTPNames::Server_Timing));
  if ((*headers).size() == 0) {
    return;
  }

  PerformanceEntryVector entries;
  for (const auto& header : *headers) {
    PerformanceEntry* entry = PerformanceServerTiming::create(
        response.Url().GetString(), header->metric,
        allowTimingDetails ? header->duration : 0.0,
        allowTimingDetails ? header->description : "");
    entries.push_back(*entry);
  }

  NotifyObserversOfEntries(entries);
  if (shouldAddToBuffer == ShouldAddToBuffer::Always &&
      server_timing_buffer_.size() + entries.size() <=
          kServerTimingBufferSize) {
    server_timing_buffer_.AppendVector(entries);
  }
}

void PerformanceBase::AddResourceTiming(const ResourceTimingInfo& info) {
  if (IsResourceTimingBufferFull() &&
      !HasObserverFor(PerformanceEntry::kResource))
    return;
  ExecutionContext* context = GetExecutionContext();
  SecurityOrigin* security_origin = GetSecurityOrigin(context);
  if (!security_origin)
    return;

  const ResourceResponse& final_response = info.FinalResponse();
  bool allow_timing_details =
      PassesTimingAllowCheck(final_response, *security_origin,
                             info.OriginalTimingAllowOrigin(), context);
  double start_time = info.InitialTime();

  if (info.RedirectChain().IsEmpty()) {
    PerformanceEntry* entry = PerformanceResourceTiming::Create(
        info, TimeOrigin(), start_time, allow_timing_details);
    NotifyObserversOfEntry(*entry);
    if (!IsResourceTimingBufferFull())
      AddResourceTimingBuffer(*entry);
    return;
  }

  const Vector<ResourceResponse>& redirect_chain = info.RedirectChain();
  bool allow_redirect_details = AllowsTimingRedirect(
      redirect_chain, final_response, *security_origin, context);

  if (!allow_redirect_details) {
    ResourceLoadTiming* final_timing = final_response.GetResourceLoadTiming();
    DCHECK(final_timing);
    if (final_timing)
      start_time = final_timing->RequestTime();
  }

  ResourceLoadTiming* last_redirect_timing =
      redirect_chain.back().GetResourceLoadTiming();
  DCHECK(last_redirect_timing);
  double last_redirect_end_time = last_redirect_timing->ReceiveHeadersEnd();

  PerformanceEntry* entry = PerformanceResourceTiming::Create(
      info, TimeOrigin(), start_time, last_redirect_end_time,
      allow_timing_details, allow_redirect_details);
  NotifyObserversOfEntry(*entry);
  if (!IsResourceTimingBufferFull())
    AddResourceTimingBuffer(*entry);
}

// Called after loadEventEnd happens.
void PerformanceBase::NotifyNavigationTimingToObservers() {
  if (!navigation_timing_)
    navigation_timing_ = CreateNavigationTimingInstance();
  if (navigation_timing_)
    NotifyObserversOfEntry(*navigation_timing_);
}

void PerformanceBase::AddFirstPaintTiming(double start_time) {
  AddPaintTiming(PerformancePaintTiming::PaintType::kFirstPaint, start_time);
}

void PerformanceBase::AddFirstContentfulPaintTiming(double start_time) {
  AddPaintTiming(PerformancePaintTiming::PaintType::kFirstContentfulPaint,
                 start_time);
}

void PerformanceBase::AddPaintTiming(PerformancePaintTiming::PaintType type,
                                     double start_time) {
  if (!RuntimeEnabledFeatures::performancePaintTimingEnabled())
    return;
  PerformanceEntry* entry = new PerformancePaintTiming(
      type, MonotonicTimeToDOMHighResTimeStamp(start_time));
  NotifyObserversOfEntry(*entry);
}

void PerformanceBase::AddResourceTimingBuffer(PerformanceEntry& entry) {
  resource_timing_buffer_.push_back(&entry);

  if (IsResourceTimingBufferFull())
    DispatchEvent(Event::Create(EventTypeNames::resourcetimingbufferfull));
}

bool PerformanceBase::IsResourceTimingBufferFull() {
  return resource_timing_buffer_.size() >= resource_timing_buffer_size_;
}

void PerformanceBase::AddFrameTimingBuffer(PerformanceEntry& entry) {
  frame_timing_buffer_.push_back(&entry);

  if (IsFrameTimingBufferFull())
    DispatchEvent(Event::Create(EventTypeNames::frametimingbufferfull));
}

bool PerformanceBase::IsFrameTimingBufferFull() {
  return frame_timing_buffer_.size() >= frame_timing_buffer_size_;
}

void PerformanceBase::AddLongTaskTiming(double start_time,
                                        double end_time,
                                        const String& name,
                                        const String& frame_src,
                                        const String& frame_id,
                                        const String& frame_name) {
  if (!HasObserverFor(PerformanceEntry::kLongTask))
    return;
  PerformanceEntry* entry = PerformanceLongTaskTiming::Create(
      MonotonicTimeToDOMHighResTimeStamp(start_time),
      MonotonicTimeToDOMHighResTimeStamp(end_time), name, frame_src, frame_id,
      frame_name);
  NotifyObserversOfEntry(*entry);
}

void PerformanceBase::mark(const String& mark_name,
                           ExceptionState& exception_state) {
  if (!user_timing_)
    user_timing_ = UserTiming::Create(*this);
  if (PerformanceEntry* entry = user_timing_->Mark(mark_name, exception_state))
    NotifyObserversOfEntry(*entry);
}

void PerformanceBase::clearMarks(const String& mark_name) {
  if (!user_timing_)
    user_timing_ = UserTiming::Create(*this);
  user_timing_->ClearMarks(mark_name);
}

void PerformanceBase::measure(const String& measure_name,
                              const String& start_mark,
                              const String& end_mark,
                              ExceptionState& exception_state) {
  if (!user_timing_)
    user_timing_ = UserTiming::Create(*this);
  if (PerformanceEntry* entry = user_timing_->Measure(
          measure_name, start_mark, end_mark, exception_state))
    NotifyObserversOfEntry(*entry);
}

void PerformanceBase::clearMeasures(const String& measure_name) {
  if (!user_timing_)
    user_timing_ = UserTiming::Create(*this);
  user_timing_->ClearMeasures(measure_name);
}

void PerformanceBase::RegisterPerformanceObserver(
    PerformanceObserver& observer) {
  observer_filter_options_ |= observer.FilterOptions();
  observers_.insert(&observer);
  UpdateLongTaskInstrumentation();
}

void PerformanceBase::UnregisterPerformanceObserver(
    PerformanceObserver& old_observer) {
  DCHECK(IsMainThread());
  // Deliver any pending observations on this observer before unregistering.
  if (active_observers_.Contains(&old_observer) &&
      !old_observer.ShouldBeSuspended()) {
    old_observer.Deliver();
    active_observers_.erase(&old_observer);
  }
  observers_.erase(&old_observer);
  UpdatePerformanceObserverFilterOptions();
  UpdateLongTaskInstrumentation();
}

void PerformanceBase::UpdatePerformanceObserverFilterOptions() {
  observer_filter_options_ = PerformanceEntry::kInvalid;
  for (const auto& observer : observers_) {
    observer_filter_options_ |= observer->FilterOptions();
  }
  UpdateLongTaskInstrumentation();
}

void PerformanceBase::NotifyObserversOfEntry(PerformanceEntry& entry) {
  for (auto& observer : observers_) {
    if (observer->FilterOptions() & entry.EntryTypeEnum())
      observer->EnqueuePerformanceEntry(entry);
  }
}

void PerformanceBase::NotifyObserversOfEntries(
    PerformanceEntryVector& entries) {
  for (const auto& entry : entries) {
    NotifyObserversOfEntry(*entry.Get());
  }
}

bool PerformanceBase::HasObserverFor(
    PerformanceEntry::EntryType filter_type) const {
  return observer_filter_options_ & filter_type;
}

void PerformanceBase::ActivateObserver(PerformanceObserver& observer) {
  if (active_observers_.IsEmpty())
    deliver_observations_timer_.StartOneShot(0, BLINK_FROM_HERE);

  active_observers_.insert(&observer);
}

void PerformanceBase::ResumeSuspendedObservers() {
  DCHECK(IsMainThread());
  if (suspended_observers_.IsEmpty())
    return;

  PerformanceObserverVector suspended;
  CopyToVector(suspended_observers_, suspended);
  for (size_t i = 0; i < suspended.size(); ++i) {
    if (!suspended[i]->ShouldBeSuspended()) {
      suspended_observers_.erase(suspended[i]);
      ActivateObserver(*suspended[i]);
    }
  }
}

void PerformanceBase::DeliverObservationsTimerFired(TimerBase*) {
  DCHECK(IsMainThread());
  PerformanceObservers observers;
  active_observers_.Swap(observers);
  for (const auto& observer : observers) {
    if (observer->ShouldBeSuspended())
      suspended_observers_.insert(observer);
    else
      observer->Deliver();
  }
}

// static
double PerformanceBase::ClampTimeResolution(double time_seconds) {
  const double kResolutionSeconds = 0.000005;
  return floor(time_seconds / kResolutionSeconds) * kResolutionSeconds;
}

// static
DOMHighResTimeStamp PerformanceBase::MonotonicTimeToDOMHighResTimeStamp(
    double time_origin,
    double monotonic_time,
    bool allow_negative_value) {
  // Avoid exposing raw platform timestamps.
  if (!monotonic_time || !time_origin)
    return 0.0;

  double time_in_seconds = monotonic_time - time_origin;
  if (time_in_seconds < 0 && !allow_negative_value)
    return 0.0;
  return ConvertSecondsToDOMHighResTimeStamp(
      ClampTimeResolution(time_in_seconds));
}

DOMHighResTimeStamp PerformanceBase::MonotonicTimeToDOMHighResTimeStamp(
    double monotonic_time) const {
  return MonotonicTimeToDOMHighResTimeStamp(time_origin_, monotonic_time,
                                            false /* allow_negative_value */);
}

DOMHighResTimeStamp PerformanceBase::now() const {
  return MonotonicTimeToDOMHighResTimeStamp(MonotonicallyIncreasingTime());
}

DEFINE_TRACE(PerformanceBase) {
  visitor->Trace(frame_timing_buffer_);
  visitor->Trace(resource_timing_buffer_);
  visitor->Trace(navigation_timing_);
  visitor->Trace(user_timing_);
  visitor->Trace(server_timing_buffer_);
  visitor->Trace(observers_);
  visitor->Trace(active_observers_);
  visitor->Trace(suspended_observers_);
  EventTargetWithInlineData::Trace(visitor);
}

}  // namespace blink
