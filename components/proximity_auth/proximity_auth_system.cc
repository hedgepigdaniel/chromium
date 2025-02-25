// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/proximity_auth/proximity_auth_system.h"

#include "base/threading/thread_task_runner_handle.h"
#include "components/proximity_auth/logging/logging.h"
#include "components/proximity_auth/proximity_auth_client.h"
#include "components/proximity_auth/remote_device_life_cycle_impl.h"
#include "components/proximity_auth/unlock_manager_impl.h"

namespace proximity_auth {

ProximityAuthSystem::ProximityAuthSystem(
    ScreenlockType screenlock_type,
    ProximityAuthClient* proximity_auth_client)
    : proximity_auth_client_(proximity_auth_client),
      unlock_manager_(
          new UnlockManagerImpl(screenlock_type, proximity_auth_client)),
      suspended_(false),
      started_(false),
      weak_ptr_factory_(this) {}

ProximityAuthSystem::ProximityAuthSystem(
    ScreenlockType screenlock_type,
    ProximityAuthClient* proximity_auth_client,
    std::unique_ptr<UnlockManager> unlock_manager)
    : proximity_auth_client_(proximity_auth_client),
      unlock_manager_(std::move(unlock_manager)),
      suspended_(false),
      started_(false),
      weak_ptr_factory_(this) {}

ProximityAuthSystem::~ProximityAuthSystem() {
  ScreenlockBridge::Get()->RemoveObserver(this);
  unlock_manager_->SetRemoteDeviceLifeCycle(nullptr);
}

void ProximityAuthSystem::Start() {
  if (started_)
    return;
  started_ = true;
  ScreenlockBridge::Get()->AddObserver(this);
  const AccountId& focused_account_id =
      ScreenlockBridge::Get()->focused_account_id();
  if (focused_account_id.is_valid())
    OnFocusedUserChanged(focused_account_id);
}

void ProximityAuthSystem::Stop() {
  if (!started_)
    return;
  started_ = false;
  ScreenlockBridge::Get()->RemoveObserver(this);
  OnFocusedUserChanged(EmptyAccountId());
}

void ProximityAuthSystem::SetRemoteDevicesForUser(
    const AccountId& account_id,
    const cryptauth::RemoteDeviceList& remote_devices) {
  remote_devices_map_[account_id] = remote_devices;
  if (started_) {
    const AccountId& focused_account_id =
        ScreenlockBridge::Get()->focused_account_id();
    if (focused_account_id.is_valid())
      OnFocusedUserChanged(focused_account_id);
  }
}

cryptauth::RemoteDeviceList ProximityAuthSystem::GetRemoteDevicesForUser(
    const AccountId& account_id) const {
  if (remote_devices_map_.find(account_id) == remote_devices_map_.end())
    return cryptauth::RemoteDeviceList();
  return remote_devices_map_.at(account_id);
}

void ProximityAuthSystem::OnAuthAttempted(const AccountId& /* account_id */) {
  // TODO(tengs): There is no reason to pass the |account_id| argument anymore.
  unlock_manager_->OnAuthAttempted(ScreenlockBridge::LockHandler::USER_CLICK);
}

void ProximityAuthSystem::OnSuspend() {
  PA_LOG(INFO) << "Preparing for device suspension.";
  DCHECK(!suspended_);
  suspended_ = true;
  unlock_manager_->SetRemoteDeviceLifeCycle(nullptr);
  remote_device_life_cycle_.reset();
}

void ProximityAuthSystem::OnSuspendDone() {
  PA_LOG(INFO) << "Device resumed from suspension.";
  DCHECK(suspended_);
  suspended_ = false;

  if (!ScreenlockBridge::Get()->IsLocked()) {
    PA_LOG(INFO) << "Suspend done, but no lock screen.";
  } else if (!started_) {
    PA_LOG(INFO) << "Suspend done, but not system started.";
  } else {
    OnFocusedUserChanged(ScreenlockBridge::Get()->focused_account_id());
  }
}

std::unique_ptr<RemoteDeviceLifeCycle>
ProximityAuthSystem::CreateRemoteDeviceLifeCycle(
    const cryptauth::RemoteDevice& remote_device) {
  return std::unique_ptr<RemoteDeviceLifeCycle>(
      new RemoteDeviceLifeCycleImpl(remote_device, proximity_auth_client_));
}

void ProximityAuthSystem::OnLifeCycleStateChanged(
    RemoteDeviceLifeCycle::State old_state,
    RemoteDeviceLifeCycle::State new_state) {
  unlock_manager_->OnLifeCycleStateChanged();
}

void ProximityAuthSystem::OnScreenDidLock(
    ScreenlockBridge::LockHandler::ScreenType screen_type) {
  OnFocusedUserChanged(ScreenlockBridge::Get()->focused_account_id());
}

void ProximityAuthSystem::OnScreenDidUnlock(
    ScreenlockBridge::LockHandler::ScreenType screen_type) {
  unlock_manager_->SetRemoteDeviceLifeCycle(nullptr);
  remote_device_life_cycle_.reset();
}

void ProximityAuthSystem::OnFocusedUserChanged(const AccountId& account_id) {
  // Update the current RemoteDeviceLifeCycle to the focused user.
  if (remote_device_life_cycle_) {
    if (remote_device_life_cycle_->GetRemoteDevice().user_id !=
        account_id.GetUserEmail()) {
      PA_LOG(INFO) << "Focused user changed, destroying life cycle for "
                   << account_id.Serialize() << ".";
      unlock_manager_->SetRemoteDeviceLifeCycle(nullptr);
      remote_device_life_cycle_.reset();
    } else {
      PA_LOG(INFO) << "Refocused on a user who is already focused.";
      return;
    }
  }

  if (remote_devices_map_.find(account_id) == remote_devices_map_.end() ||
      remote_devices_map_[account_id].size() == 0) {
    PA_LOG(INFO) << "User " << account_id.Serialize()
                 << " does not have a RemoteDevice.";
    return;
  }

  // TODO(tengs): We currently assume each user has only one RemoteDevice, so we
  // can simply take the first item in the list.
  cryptauth::RemoteDevice remote_device = remote_devices_map_[account_id][0];
  if (!suspended_) {
    PA_LOG(INFO) << "Creating RemoteDeviceLifeCycle for focused user: "
                 << account_id.Serialize();
    remote_device_life_cycle_ = CreateRemoteDeviceLifeCycle(remote_device);
    unlock_manager_->SetRemoteDeviceLifeCycle(remote_device_life_cycle_.get());
    remote_device_life_cycle_->AddObserver(this);
    remote_device_life_cycle_->Start();
  }
}

}  // proximity_auth
