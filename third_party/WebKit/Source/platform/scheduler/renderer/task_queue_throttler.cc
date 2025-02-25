// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/scheduler/renderer/task_queue_throttler.h"

#include <cstdint>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/optional.h"
#include "platform/WebFrameScheduler.h"
#include "platform/scheduler/base/real_time_domain.h"
#include "platform/scheduler/base/trace_helper.h"
#include "platform/scheduler/child/scheduler_tqm_delegate.h"
#include "platform/scheduler/renderer/budget_pool.h"
#include "platform/scheduler/renderer/renderer_scheduler_impl.h"
#include "platform/scheduler/renderer/throttled_time_domain.h"
#include "platform/scheduler/renderer/web_frame_scheduler_impl.h"

namespace blink {
namespace scheduler {

namespace {

base::Optional<base::TimeTicks> NextTaskRunTime(LazyNow* lazy_now,
                                                TaskQueue* queue) {
  if (queue->HasTaskToRunImmediately())
    return lazy_now->Now();
  return queue->GetNextScheduledWakeUp();
}

template <class T>
T Min(const base::Optional<T>& optional, const T& value) {
  if (!optional) {
    return value;
  }
  return std::min(optional.value(), value);
}

template <class T>
base::Optional<T> Min(const base::Optional<T>& a, const base::Optional<T>& b) {
  if (!b)
    return a;
  if (!a)
    return b;
  return std::min(a.value(), b.value());
}

template <class T>
T Max(const base::Optional<T>& optional, const T& value) {
  if (!optional)
    return value;
  return std::max(optional.value(), value);
}

template <class T>
base::Optional<T> Max(const base::Optional<T>& a, const base::Optional<T>& b) {
  if (!b)
    return a;
  if (!a)
    return b;
  return std::max(a.value(), b.value());
}

}  // namespace

TaskQueueThrottler::TaskQueueThrottler(
    RendererSchedulerImpl* renderer_scheduler)
    : control_task_queue_(renderer_scheduler->ControlTaskQueue()),
      renderer_scheduler_(renderer_scheduler),
      tick_clock_(renderer_scheduler->tick_clock()),
      time_domain_(new ThrottledTimeDomain()),
      allow_throttling_(true),
      weak_factory_(this) {
  pump_throttled_tasks_closure_.Reset(base::Bind(
      &TaskQueueThrottler::PumpThrottledTasks, weak_factory_.GetWeakPtr()));
  forward_immediate_work_callback_ =
      base::Bind(&TaskQueueThrottler::OnQueueNextWakeUpChanged,
                 weak_factory_.GetWeakPtr());

  renderer_scheduler_->RegisterTimeDomain(time_domain_.get());
}

TaskQueueThrottler::~TaskQueueThrottler() {
  // It's possible for queues to be still throttled, so we need to tidy up
  // before unregistering the time domain.
  for (const TaskQueueMap::value_type& map_entry : queue_details_) {
    TaskQueue* task_queue = map_entry.first;
    if (IsThrottled(task_queue)) {
      task_queue->SetTimeDomain(renderer_scheduler_->real_time_domain());
      task_queue->RemoveFence();
    }
    if (map_entry.second.throttling_ref_count != 0)
      task_queue->SetObserver(nullptr);
  }

  renderer_scheduler_->UnregisterTimeDomain(time_domain_.get());
}

void TaskQueueThrottler::IncreaseThrottleRefCount(TaskQueue* task_queue) {
  DCHECK_NE(task_queue, control_task_queue_.get());

  std::pair<TaskQueueMap::iterator, bool> insert_result =
      queue_details_.insert(std::make_pair(task_queue, Metadata()));
  insert_result.first->second.throttling_ref_count++;

  // If ref_count is 1, the task queue is newly throttled.
  if (insert_result.first->second.throttling_ref_count != 1)
    return;

  TRACE_EVENT1("renderer.scheduler", "TaskQueueThrottler_TaskQueueThrottled",
               "task_queue", task_queue);

  task_queue->SetObserver(this);

  if (!allow_throttling_)
    return;

  task_queue->SetTimeDomain(time_domain_.get());
  // This blocks any tasks from |task_queue| until PumpThrottledTasks() to
  // enforce task alignment.
  task_queue->InsertFence(TaskQueue::InsertFencePosition::BEGINNING_OF_TIME);

  if (!task_queue->IsQueueEnabled())
    return;

  if (!task_queue->IsEmpty()) {
    LazyNow lazy_now(tick_clock_);
    OnQueueNextWakeUpChanged(task_queue,
                             NextTaskRunTime(&lazy_now, task_queue).value());
  }
}

void TaskQueueThrottler::DecreaseThrottleRefCount(TaskQueue* task_queue) {
  TaskQueueMap::iterator iter = queue_details_.find(task_queue);

  if (iter == queue_details_.end())
    return;
  if (iter->second.throttling_ref_count == 0)
    return;
  if (--iter->second.throttling_ref_count != 0)
    return;

  TRACE_EVENT1("renderer.scheduler", "TaskQueueThrottler_TaskQueueUnthrottled",
               "task_queue", task_queue);

  task_queue->SetObserver(nullptr);

  MaybeDeleteQueueMetadata(iter);

  if (!allow_throttling_)
    return;

  task_queue->SetTimeDomain(renderer_scheduler_->real_time_domain());
  task_queue->RemoveFence();
}

bool TaskQueueThrottler::IsThrottled(TaskQueue* task_queue) const {
  if (!allow_throttling_)
    return false;

  auto find_it = queue_details_.find(task_queue);
  if (find_it == queue_details_.end())
    return false;
  return find_it->second.throttling_ref_count > 0;
}

void TaskQueueThrottler::UnregisterTaskQueue(TaskQueue* task_queue) {
  auto find_it = queue_details_.find(task_queue);
  if (find_it == queue_details_.end())
    return;

  std::unordered_set<BudgetPool*> budget_pools = find_it->second.budget_pools;
  for (BudgetPool* budget_pool : budget_pools) {
    budget_pool->UnregisterQueue(task_queue);
  }

  // Iterator may have been deleted by BudgetPool::RemoveQueue, so don't
  // use it here.
  queue_details_.erase(task_queue);

  // NOTE: Observer is automatically unregistered when unregistering task queue.
}

void TaskQueueThrottler::OnQueueNextWakeUpChanged(
    TaskQueue* queue,
    base::TimeTicks next_wake_up) {
  if (!control_task_queue_->RunsTasksOnCurrentThread()) {
    control_task_queue_->PostTask(
        FROM_HERE,
        base::Bind(forward_immediate_work_callback_, queue, next_wake_up));
    return;
  }

  TRACE_EVENT0("renderer.scheduler",
               "TaskQueueThrottler::OnQueueNextWakeUpChanged");

  // We don't expect this to get called for disabled queues, but we can't DCHECK
  // because of the above thread hop.  Just bail out if the queue is disabled.
  if (!queue->IsQueueEnabled())
    return;

  base::TimeTicks now = tick_clock_->NowTicks();

  auto find_it = queue_details_.find(queue);
  if (find_it == queue_details_.end())
    return;

  for (BudgetPool* budget_pool : find_it->second.budget_pools) {
    budget_pool->OnQueueNextWakeUpChanged(queue, now, next_wake_up);
  }

  // TODO(altimin): This probably can be removed —- budget pools should
  // schedule this.
  base::TimeTicks next_allowed_run_time =
      GetNextAllowedRunTime(queue, next_wake_up);
  MaybeSchedulePumpThrottledTasks(
      FROM_HERE, now, std::max(next_wake_up, next_allowed_run_time));
}

void TaskQueueThrottler::PumpThrottledTasks() {
  TRACE_EVENT0("renderer.scheduler", "TaskQueueThrottler::PumpThrottledTasks");
  pending_pump_throttled_tasks_runtime_.reset();

  LazyNow lazy_now(tick_clock_);
  base::Optional<base::TimeTicks> next_scheduled_delayed_task;

  for (const auto& pair : budget_pools_)
    pair.first->OnWakeUp(lazy_now.Now());

  for (const TaskQueueMap::value_type& map_entry : queue_details_) {
    TaskQueue* task_queue = map_entry.first;
    UpdateQueueThrottlingStateInternal(lazy_now.Now(), task_queue, true);
  }
}

/* static */
base::TimeTicks TaskQueueThrottler::AlignedThrottledRunTime(
    base::TimeTicks unthrottled_runtime) {
  const base::TimeDelta one_second = base::TimeDelta::FromSeconds(1);
  return unthrottled_runtime + one_second -
         ((unthrottled_runtime - base::TimeTicks()) % one_second);
}

void TaskQueueThrottler::MaybeSchedulePumpThrottledTasks(
    const tracked_objects::Location& from_here,
    base::TimeTicks now,
    base::TimeTicks unaligned_runtime) {
  if (!allow_throttling_)
    return;

  base::TimeTicks runtime =
      AlignedThrottledRunTime(std::max(now, unaligned_runtime));
  DCHECK_LE(now, runtime);

  // If there is a pending call to PumpThrottledTasks and it's sooner than
  // |runtime| then return.
  if (pending_pump_throttled_tasks_runtime_ &&
      runtime >= pending_pump_throttled_tasks_runtime_.value()) {
    return;
  }

  pending_pump_throttled_tasks_runtime_ = runtime;

  pump_throttled_tasks_closure_.Cancel();

  base::TimeDelta delay = pending_pump_throttled_tasks_runtime_.value() - now;
  TRACE_EVENT1("renderer.scheduler",
               "TaskQueueThrottler::MaybeSchedulePumpThrottledTasks",
               "delay_till_next_pump_ms", delay.InMilliseconds());
  control_task_queue_->PostDelayedTask(
      from_here, pump_throttled_tasks_closure_.GetCallback(), delay);
}

CPUTimeBudgetPool* TaskQueueThrottler::CreateCPUTimeBudgetPool(
    const char* name) {
  CPUTimeBudgetPool* time_budget_pool =
      new CPUTimeBudgetPool(name, this, tick_clock_->NowTicks());
  budget_pools_[time_budget_pool] = base::WrapUnique(time_budget_pool);
  return time_budget_pool;
}

WakeUpBudgetPool* TaskQueueThrottler::CreateWakeUpBudgetPool(const char* name) {
  WakeUpBudgetPool* wake_up_budget_pool =
      new WakeUpBudgetPool(name, this, tick_clock_->NowTicks());
  budget_pools_[wake_up_budget_pool] = base::WrapUnique(wake_up_budget_pool);
  return wake_up_budget_pool;
}

void TaskQueueThrottler::OnTaskRunTimeReported(TaskQueue* task_queue,
                                               base::TimeTicks start_time,
                                               base::TimeTicks end_time) {
  if (!IsThrottled(task_queue))
    return;

  auto find_it = queue_details_.find(task_queue);
  if (find_it == queue_details_.end())
    return;

  for (BudgetPool* budget_pool : find_it->second.budget_pools) {
    budget_pool->RecordTaskRunTime(task_queue, start_time, end_time);
  }
}

void TaskQueueThrottler::UpdateQueueThrottlingState(base::TimeTicks now,
                                                    TaskQueue* queue) {
  UpdateQueueThrottlingStateInternal(now, queue, false);
}

void TaskQueueThrottler::UpdateQueueThrottlingStateInternal(base::TimeTicks now,
                                                            TaskQueue* queue,
                                                            bool is_wake_up) {
  if (!queue->IsQueueEnabled() || !IsThrottled(queue)) {
    return;
  }

  LazyNow lazy_now(now);

  base::Optional<base::TimeTicks> next_desired_run_time =
      NextTaskRunTime(&lazy_now, queue);

  if (!next_desired_run_time) {
    // This queue is empty. Given that new task can arrive at any moment,
    // block the queue completely and update the state upon the notification
    // about a new task.
    queue->InsertFence(TaskQueue::InsertFencePosition::NOW);
    return;
  }

  if (CanRunTasksAt(queue, now, false) &&
      CanRunTasksAt(queue, next_desired_run_time.value(), false)) {
    // We can run up until the next task uninterrupted unless something changes.
    // Remove the fence to allow new tasks to run immediately and handle
    // the situation change in the notification about the said change.
    queue->RemoveFence();

    // TaskQueueThrottler does not schedule wake-ups implicitly, we need
    // to be explicit.
    if (next_desired_run_time.value() != now) {
      time_domain_->SetNextTaskRunTime(next_desired_run_time.value());
    }
    return;
  }

  if (CanRunTasksAt(queue, now, is_wake_up)) {
    // We can run task now, but we can't run until the next scheduled task.
    // Insert a fresh fence to unblock queue and schedule a pump for the
    // next wake-up.
    queue->InsertFence(TaskQueue::InsertFencePosition::NOW);

    base::Optional<base::TimeTicks> next_wake_up =
        queue->GetNextScheduledWakeUp();
    if (next_wake_up) {
      MaybeSchedulePumpThrottledTasks(
          FROM_HERE, now, GetNextAllowedRunTime(queue, next_wake_up.value()));
    }
    return;
  }

  base::TimeTicks next_run_time =
      GetNextAllowedRunTime(queue, next_desired_run_time.value());

  // Insert a fence of an approriate type.
  base::Optional<QueueBlockType> block_type = GetQueueBlockType(now, queue);
  DCHECK(block_type);

  switch (block_type.value()) {
    case QueueBlockType::kAllTasks:
      queue->InsertFence(TaskQueue::InsertFencePosition::BEGINNING_OF_TIME);

      {
        // Braces limit the scope for a declared variable. Does not compile
        // otherwise.
        TRACE_EVENT1(
            "renderer.scheduler",
            "TaskQueueThrottler::PumpThrottledTasks_ExpensiveTaskThrottled",
            "throttle_time_in_seconds",
            (next_run_time - next_desired_run_time.value()).InSecondsF());
      }
      break;
    case QueueBlockType::kNewTasksOnly:
      if (!queue->HasFence()) {
        // Insert a new non-fully blocking fence only when there is no fence
        // already in order avoid undesired unblocking of old tasks.
        queue->InsertFence(TaskQueue::InsertFencePosition::NOW);
      }
      break;
  }

  // Schedule a pump.
  MaybeSchedulePumpThrottledTasks(FROM_HERE, now, next_run_time);
}

base::Optional<QueueBlockType> TaskQueueThrottler::GetQueueBlockType(
    base::TimeTicks now,
    TaskQueue* queue) {
  auto find_it = queue_details_.find(queue);
  if (find_it == queue_details_.end())
    return base::nullopt;

  bool has_new_tasks_only_block = false;

  for (BudgetPool* budget_pool : find_it->second.budget_pools) {
    if (!budget_pool->CanRunTasksAt(now, false)) {
      if (budget_pool->GetBlockType() == QueueBlockType::kAllTasks)
        return QueueBlockType::kAllTasks;
      DCHECK_EQ(budget_pool->GetBlockType(), QueueBlockType::kNewTasksOnly);
      has_new_tasks_only_block = true;
    }
  }

  if (has_new_tasks_only_block)
    return QueueBlockType::kNewTasksOnly;
  return base::nullopt;
}

void TaskQueueThrottler::AsValueInto(base::trace_event::TracedValue* state,
                                     base::TimeTicks now) const {
  if (pending_pump_throttled_tasks_runtime_) {
    state->SetDouble(
        "next_throttled_tasks_pump_in_seconds",
        (pending_pump_throttled_tasks_runtime_.value() - now).InSecondsF());
  }

  state->SetBoolean("allow_throttling", allow_throttling_);

  state->BeginDictionary("time_budget_pools");
  for (const auto& map_entry : budget_pools_) {
    BudgetPool* pool = map_entry.first;
    pool->AsValueInto(state, now);
  }
  state->EndDictionary();

  state->BeginDictionary("queue_details");
  for (const auto& map_entry : queue_details_) {
    state->BeginDictionaryWithCopiedName(
        trace_helper::PointerToString(map_entry.first));

    state->SetInteger("throttling_ref_count",
                      map_entry.second.throttling_ref_count);

    state->EndDictionary();
  }
  state->EndDictionary();
}

void TaskQueueThrottler::AddQueueToBudgetPool(TaskQueue* queue,
                                              BudgetPool* budget_pool) {
  std::pair<TaskQueueMap::iterator, bool> insert_result =
      queue_details_.insert(std::make_pair(queue, Metadata()));

  Metadata& metadata = insert_result.first->second;

  DCHECK(metadata.budget_pools.find(budget_pool) ==
         metadata.budget_pools.end());

  metadata.budget_pools.insert(budget_pool);
}

void TaskQueueThrottler::RemoveQueueFromBudgetPool(TaskQueue* queue,
                                                   BudgetPool* budget_pool) {
  auto find_it = queue_details_.find(queue);
  DCHECK(find_it != queue_details_.end() &&
         find_it->second.budget_pools.find(budget_pool) !=
             find_it->second.budget_pools.end());

  find_it->second.budget_pools.erase(budget_pool);

  MaybeDeleteQueueMetadata(find_it);
}

void TaskQueueThrottler::UnregisterBudgetPool(BudgetPool* budget_pool) {
  budget_pools_.erase(budget_pool);
}

base::TimeTicks TaskQueueThrottler::GetNextAllowedRunTime(
    TaskQueue* queue,
    base::TimeTicks desired_run_time) {
  base::TimeTicks next_run_time = desired_run_time;

  auto find_it = queue_details_.find(queue);
  if (find_it == queue_details_.end())
    return next_run_time;

  for (BudgetPool* budget_pool : find_it->second.budget_pools) {
    next_run_time = std::max(
        next_run_time, budget_pool->GetNextAllowedRunTime(desired_run_time));
  }

  return next_run_time;
}

bool TaskQueueThrottler::CanRunTasksAt(TaskQueue* queue,
                                       base::TimeTicks moment,
                                       bool is_wake_up) {
  auto find_it = queue_details_.find(queue);
  if (find_it == queue_details_.end())
    return true;

  for (BudgetPool* budget_pool : find_it->second.budget_pools) {
    if (!budget_pool->CanRunTasksAt(moment, is_wake_up))
      return false;
  }

  return true;
}

void TaskQueueThrottler::MaybeDeleteQueueMetadata(TaskQueueMap::iterator it) {
  if (it->second.throttling_ref_count == 0 && it->second.budget_pools.empty())
    queue_details_.erase(it);
}

void TaskQueueThrottler::DisableThrottling() {
  if (!allow_throttling_)
    return;

  allow_throttling_ = false;

  for (const auto& map_entry : queue_details_) {
    if (map_entry.second.throttling_ref_count == 0)
      continue;

    TaskQueue* queue = map_entry.first;

    queue->SetTimeDomain(renderer_scheduler_->GetActiveTimeDomain());

    queue->RemoveFence();
  }

  pump_throttled_tasks_closure_.Cancel();
  pending_pump_throttled_tasks_runtime_ = base::nullopt;

  TRACE_EVENT0("renderer.scheduler", "TaskQueueThrottler_DisableThrottling");
}

void TaskQueueThrottler::EnableThrottling() {
  if (allow_throttling_)
    return;

  allow_throttling_ = true;

  LazyNow lazy_now(tick_clock_);

  for (const auto& map_entry : queue_details_) {
    if (map_entry.second.throttling_ref_count == 0)
      continue;

    TaskQueue* queue = map_entry.first;

    // Throttling is enabled and task queue should be blocked immediately
    // to enforce task alignment.
    queue->InsertFence(TaskQueue::InsertFencePosition::BEGINNING_OF_TIME);
    queue->SetTimeDomain(time_domain_.get());
    UpdateQueueThrottlingState(lazy_now.Now(), queue);
  }

  TRACE_EVENT0("renderer.scheduler", "TaskQueueThrottler_EnableThrottling");
}

}  // namespace scheduler
}  // namespace blink
