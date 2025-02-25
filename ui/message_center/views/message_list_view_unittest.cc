// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <utility>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/message_center/fake_message_center.h"
#include "ui/message_center/notification.h"
#include "ui/message_center/notification_list.h"
#include "ui/message_center/views/message_center_controller.h"
#include "ui/message_center/views/message_list_view.h"
#include "ui/message_center/views/notification_view.h"
#include "ui/views/test/views_test_base.h"

using ::testing::ElementsAre;

namespace message_center {

static const char* kNotificationId1 = "notification id 1";
static const char* kNotificationId2 = "notification id 2";

namespace {

/* Types **********************************************************************/

enum CallType { GET_PREFERRED_SIZE, GET_HEIGHT_FOR_WIDTH, LAYOUT };

/* Instrumented/Mock NotificationView subclass ********************************/

class MockNotificationView : public NotificationView {
 public:
  class Test {
   public:
    virtual void RegisterCall(CallType type) = 0;
  };

  MockNotificationView(MessageCenterController* controller,
                       const Notification& notification,
                       Test* test);
  ~MockNotificationView() override;

  gfx::Size GetPreferredSize() const override;
  int GetHeightForWidth(int w) const override;
  void Layout() override;

 private:
  Test* test_;

  DISALLOW_COPY_AND_ASSIGN(MockNotificationView);
};

MockNotificationView::MockNotificationView(MessageCenterController* controller,
                                           const Notification& notification,
                                           Test* test)
    : NotificationView(controller, notification), test_(test) {
  // Calling SetPaintToLayer() to ensure that this view has its own layer.
  // This layer is needed to enable adding/removal animations.
  SetPaintToLayer();
}

MockNotificationView::~MockNotificationView() {}

gfx::Size MockNotificationView::GetPreferredSize() const {
  test_->RegisterCall(GET_PREFERRED_SIZE);
  DCHECK(child_count() > 0);
  return NotificationView::GetPreferredSize();
}

int MockNotificationView::GetHeightForWidth(int width) const {
  test_->RegisterCall(GET_HEIGHT_FOR_WIDTH);
  DCHECK(child_count() > 0);
  return NotificationView::GetHeightForWidth(width);
}

void MockNotificationView::Layout() {
  test_->RegisterCall(LAYOUT);
  DCHECK(child_count() > 0);
  NotificationView::Layout();
}

}  // anonymous namespace

/* Test fixture ***************************************************************/

class MessageListViewTest : public views::ViewsTestBase,
                            public MockNotificationView::Test,
                            public MessageListView::Observer,
                            public MessageCenterController {
 public:
  MessageListViewTest() {}

  ~MessageListViewTest() override {}

  void SetUp() override {
    views::ViewsTestBase::SetUp();

    message_list_view_.reset(new MessageListView());
    message_list_view_->AddObserver(this);
    message_list_view_->set_owned_by_client();

    widget_.reset(new views::Widget());
    views::Widget::InitParams params =
        CreateParams(views::Widget::InitParams::TYPE_POPUP);
    params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
    params.bounds = gfx::Rect(50, 50, 650, 650);
    widget_->Init(params);
    views::View* root = widget_->GetRootView();
    root->AddChildView(message_list_view_.get());
    widget_->Show();
    widget_->Activate();
  }

  void TearDown() override {
    widget_->CloseNow();
    widget_.reset();

    message_list_view_->RemoveObserver(this);
    message_list_view_.reset();

    views::ViewsTestBase::TearDown();
  }

 protected:
  MessageListView* message_list_view() const {
    return message_list_view_.get();
  }

  int& reposition_top() { return message_list_view_->reposition_top_; }

  int& fixed_height() { return message_list_view_->fixed_height_; }

  views::BoundsAnimator& animator() { return message_list_view_->animator_; }

  std::vector<int> ComputeRepositionOffsets(const std::vector<int>& heights,
                                            const std::vector<bool>& deleting,
                                            int target_index,
                                            int padding) {
    return message_list_view_->ComputeRepositionOffsets(heights, deleting,
                                                        target_index, padding);
  }

  MockNotificationView* CreateNotificationView(
      const Notification& notification) {
    return new MockNotificationView(this, notification, this);
  }

  void RunPendingAnimations() {
    while (animator().IsAnimating())
      RunPendingMessages();
  }

 private:
  // MockNotificationView::Test override
  void RegisterCall(CallType type) override {}

  // MessageListView::Observer override
  void OnAllNotificationsCleared() override {}

  // MessageCenterController override:
  void ClickOnNotification(const std::string& notification_id) override {}
  void RemoveNotification(const std::string& notification_id,
                          bool by_user) override {}
  std::unique_ptr<ui::MenuModel> CreateMenuModel(
      const NotifierId& notifier_id,
      const base::string16& display_source) override {
    NOTREACHED();
    return nullptr;
  }
  bool HasClickedListener(const std::string& notification_id) override {
    return false;
  }
  void ClickOnNotificationButton(const std::string& notification_id,
                                 int button_index) override {}
  void ClickOnSettingsButton(const std::string& notification_id) override {}
  void UpdateNotificationSize(const std::string& notification_id) override;

  // Widget to host a MessageListView.
  std::unique_ptr<views::Widget> widget_;
  // MessageListView to be tested.
  std::unique_ptr<MessageListView> message_list_view_;

  DISALLOW_COPY_AND_ASSIGN(MessageListViewTest);
};

void MessageListViewTest::UpdateNotificationSize(
    const std::string& notification_id) {
  // For this test, this method should not be invoked.
  NOTREACHED();
}

/* Unit tests *****************************************************************/

TEST_F(MessageListViewTest, AddNotification) {
  // Create a dummy notification.
  auto* notification_view = CreateNotificationView(
      Notification(NOTIFICATION_TYPE_SIMPLE, std::string(kNotificationId1),
                   base::UTF8ToUTF16("title"), base::UTF8ToUTF16("message1"),
                   gfx::Image(), base::UTF8ToUTF16("display source"), GURL(),
                   NotifierId(NotifierId::APPLICATION, "extension_id"),
                   message_center::RichNotificationData(), nullptr));

  EXPECT_EQ(0, message_list_view()->child_count());
  EXPECT_FALSE(message_list_view()->Contains(notification_view));

  // Add a notification.
  message_list_view()->AddNotificationAt(notification_view, 0);

  EXPECT_EQ(1, message_list_view()->child_count());
  EXPECT_TRUE(message_list_view()->Contains(notification_view));
}

TEST_F(MessageListViewTest, RepositionOffsets) {
  const auto insets = message_list_view()->GetInsets();
  const int top = insets.top();
  std::vector<int> positions;

  // Notification above grows. |reposition_top| should remain at the same
  // offset from the bottom.
  fixed_height() = 4 + insets.height();
  reposition_top() = 1 + top;
  positions =
      ComputeRepositionOffsets({2, 1, 1, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 4));
  EXPECT_EQ(4 + insets.height() + 1, fixed_height());
  EXPECT_EQ(1 + top + 1, reposition_top());

  // Notification above shrinks. The message center keeps its height.
  // All notifications should remain at the same position from the top.
  fixed_height() = 5 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 1, 1, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 4));
  EXPECT_EQ(5 + insets.height(), fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Notification above is being deleted. |reposition_top| should remain at the
  // same place.
  fixed_height() = 5 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 1, 1, 1}, {true, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 4));
  EXPECT_EQ(5 + insets.height(), fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Notification above is inserted. |reposition_top| should remain at the
  // same offset from the bottom.
  fixed_height() = 5 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 1, 1, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 4));
  EXPECT_EQ(5 + insets.height(), fixed_height());
  EXPECT_EQ(top + 2, reposition_top());

  // Target notification grows with no free space. |reposition_top| is forced to
  // change its offset from the bottom.
  fixed_height() = 5 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 2, 1, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 4, top + 5));
  EXPECT_EQ(5 + insets.height() + 1, fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Target notification grows with free space. |reposition_top| should remain
  // at the same offset from the bottom.
  fixed_height() = 6 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 2, 1, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 4, top + 5));
  EXPECT_EQ(6 + insets.height(), fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Target notification grows with not enough free space. |reposition_top|
  // should change its offset as little as possible, and consume the free space.
  fixed_height() = 6 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 3, 1, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 5, top + 6));
  EXPECT_EQ(6 + insets.height() + 1, fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Target notification shrinks. |reposition_top| should remain at the
  // same offset from the bottom.
  fixed_height() = 7 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 1, 1, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 4));
  EXPECT_EQ(7 + insets.height(), fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Notification below grows with no free space. |reposition_top| is forced to
  // change its offset from the bottom.
  fixed_height() = 7 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 1, 4, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 7));
  EXPECT_EQ(7 + insets.height() + 1, fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Notification below grows with free space. |reposition_top| should remain
  // at the same offset from the bottom.
  fixed_height() = 8 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 1, 2, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 5));
  EXPECT_EQ(8 + insets.height(), fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Notification below shrinks. |reposition_top| should remain at the same
  // offset from the bottom.
  fixed_height() = 8 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 1, 1, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 4));
  EXPECT_EQ(8 + insets.height(), fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Notification below is being deleted. |reposition_top| should remain at the
  // same offset from the bottom.
  fixed_height() = 8 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 1, 1, 1}, {false, false, true, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 3));
  EXPECT_EQ(8 + insets.height(), fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Notification below is inserted with free space. |reposition_top| should
  // remain at the same offset from the bottom.
  fixed_height() = 8 + insets.height();
  reposition_top() = 2 + top;
  positions =
      ComputeRepositionOffsets({1, 1, 1, 1}, {false, false, false, false},
                               1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 4));
  EXPECT_EQ(8 + insets.height(), fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Notification below is inserted with no free space. |reposition_top| is
  // forced to change its offset from the bottom.
  fixed_height() = 8 + insets.height();
  reposition_top() = 2 + top;
  positions = ComputeRepositionOffsets({1, 1, 1, 4, 1},
                                       {false, false, false, false, false},
                                       1 /* target_index */, 0 /* padding */);
  EXPECT_THAT(positions, ElementsAre(top, top + 2, top + 3, top + 4, top + 8));
  EXPECT_EQ(8 + insets.height() + 1, fixed_height());
  EXPECT_EQ(2 + top, reposition_top());

  // Test padding.
  fixed_height() = 20 + insets.height();
  reposition_top() = 5 + top;
  positions =
      ComputeRepositionOffsets({5, 3, 3, 3}, {false, false, false, false},
                               1 /* target_index */, 2 /* padding */);
  EXPECT_THAT(positions,
              ElementsAre(top, top + 7, top + 7 + 5, top + 7 + 5 + 5));
  EXPECT_EQ(20 + insets.height() + 2, fixed_height());
  EXPECT_EQ(5 + top + 2, reposition_top());
}

TEST_F(MessageListViewTest, RemoveNotification) {
  message_list_view()->SetBounds(0, 0, 800, 600);

  // Create dummy notifications.
  auto* notification_view = CreateNotificationView(
      Notification(NOTIFICATION_TYPE_SIMPLE, std::string(kNotificationId1),
                   base::UTF8ToUTF16("title"), base::UTF8ToUTF16("message1"),
                   gfx::Image(), base::UTF8ToUTF16("display source"), GURL(),
                   NotifierId(NotifierId::APPLICATION, "extension_id"),
                   message_center::RichNotificationData(), nullptr));

  message_list_view()->AddNotificationAt(notification_view, 0);
  EXPECT_EQ(1, message_list_view()->child_count());
  EXPECT_TRUE(message_list_view()->Contains(notification_view));

  RunPendingAnimations();

  message_list_view()->RemoveNotification(notification_view);

  RunPendingAnimations();

  EXPECT_EQ(0, message_list_view()->child_count());
}

TEST_F(MessageListViewTest, ClearAllClosableNotifications) {
  message_list_view()->SetBounds(0, 0, 800, 600);

  // Create dummy notifications.
  auto* notification_view1 = CreateNotificationView(
      Notification(NOTIFICATION_TYPE_SIMPLE, std::string(kNotificationId1),
                   base::UTF8ToUTF16("title"), base::UTF8ToUTF16("message1"),
                   gfx::Image(), base::UTF8ToUTF16("display source"), GURL(),
                   NotifierId(NotifierId::APPLICATION, "extension_id"),
                   message_center::RichNotificationData(), nullptr));
  auto* notification_view2 = CreateNotificationView(
      Notification(NOTIFICATION_TYPE_SIMPLE, std::string(kNotificationId2),
                   base::UTF8ToUTF16("title 2"), base::UTF8ToUTF16("message2"),
                   gfx::Image(), base::UTF8ToUTF16("display source"), GURL(),
                   NotifierId(NotifierId::APPLICATION, "extension_id"),
                   message_center::RichNotificationData(), nullptr));

  message_list_view()->AddNotificationAt(notification_view1, 0);
  EXPECT_EQ(1, message_list_view()->child_count());
  EXPECT_TRUE(notification_view1->visible());

  RunPendingAnimations();

  message_list_view()->AddNotificationAt(notification_view2, 1);
  EXPECT_EQ(2, message_list_view()->child_count());
  EXPECT_TRUE(notification_view2->visible());

  RunPendingAnimations();

  message_list_view()->ClearAllClosableNotifications(
      message_list_view()->bounds());

  RunPendingAnimations();

  EXPECT_EQ(0, message_list_view()->child_count());
}

// Regression test for crbug.com/713983
TEST_F(MessageListViewTest, RemoveWhileClearAll) {
  message_list_view()->SetBounds(0, 0, 800, 600);

  // Create dummy notifications.
  auto* notification_view1 = CreateNotificationView(
      Notification(NOTIFICATION_TYPE_SIMPLE, std::string(kNotificationId1),
                   base::UTF8ToUTF16("title"), base::UTF8ToUTF16("message1"),
                   gfx::Image(), base::UTF8ToUTF16("display source"), GURL(),
                   NotifierId(NotifierId::APPLICATION, "extension_id"),
                   message_center::RichNotificationData(), nullptr));
  auto* notification_view2 = CreateNotificationView(
      Notification(NOTIFICATION_TYPE_SIMPLE, std::string(kNotificationId2),
                   base::UTF8ToUTF16("title 2"), base::UTF8ToUTF16("message2"),
                   gfx::Image(), base::UTF8ToUTF16("display source"), GURL(),
                   NotifierId(NotifierId::APPLICATION, "extension_id"),
                   message_center::RichNotificationData(), nullptr));

  message_list_view()->AddNotificationAt(notification_view1, 0);
  EXPECT_EQ(1, message_list_view()->child_count());

  RunPendingAnimations();

  message_list_view()->AddNotificationAt(notification_view2, 1);
  EXPECT_EQ(2, message_list_view()->child_count());

  RunPendingAnimations();

  // Call RemoveNotification()
  EXPECT_TRUE(message_list_view()->Contains(notification_view2));
  message_list_view()->RemoveNotification(notification_view2);

  // Call "Clear All" while notification_view2 is still in message_list_view.
  EXPECT_TRUE(message_list_view()->Contains(notification_view2));
  message_list_view()->ClearAllClosableNotifications(
      message_list_view()->bounds());

  RunPendingAnimations();
  EXPECT_EQ(0, message_list_view()->child_count());
}

}  // namespace
