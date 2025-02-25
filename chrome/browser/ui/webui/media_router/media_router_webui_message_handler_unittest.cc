// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/media_router/media_router_webui_message_handler.h"
#include "base/macros.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/media/router/mock_media_router.h"
#include "chrome/browser/media/router/mojo/media_router_mojo_test.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/media_router/media_router_ui.h"
#include "chrome/browser/ui/webui/media_router/media_router_web_ui_test.h"
#include "content/public/browser/browser_context.h"
#include "content/public/test/test_web_ui.h"
#include "extensions/common/constants.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace media_router {

namespace {

const char kProviderExtensionIdForTesting[] = "test_id";
const char kControllerPathForTesting[] = "test_path";
const char kUserEmailForTesting[] = "nobody@example.com";
const char kUserDomainForTesting[] = "example.com";

bool GetBooleanFromDict(const base::DictionaryValue* dict,
                        const std::string& key) {
  bool value = false;
  EXPECT_TRUE(dict->GetBoolean(key, &value));
  return value;
}

double GetDoubleFromDict(const base::DictionaryValue* dict,
                         const std::string& key) {
  double value = 0;
  EXPECT_TRUE(dict->GetDouble(key, &value));
  return value;
}

int GetIntegerFromDict(const base::DictionaryValue* dict,
                       const std::string& key) {
  int value = 0;
  EXPECT_TRUE(dict->GetInteger(key, &value));
  return value;
}

std::string GetStringFromDict(const base::DictionaryValue* dict,
                              const std::string& key) {
  std::string value;
  EXPECT_TRUE(dict->GetString(key, &value));
  return value;
}

// Creates a local route for display.
MediaRoute CreateRoute() {
  MediaRoute::Id route_id("routeId123");
  MediaSink::Id sink_id("sinkId123");
  MediaSink sink(sink_id, "The sink", MediaSink::IconType::CAST);
  std::string description("This is a route");
  bool is_local = true;
  bool is_for_display = true;
  MediaRoute route(route_id, MediaSource("mediaSource"), sink_id, description,
                   is_local, kControllerPathForTesting, is_for_display);

  return route;
}

MediaSinkWithCastModes CreateMediaSinkWithCastMode(const std::string& sink_id,
                                                   MediaCastMode cast_mode) {
  std::string sink_name("The sink");
  MediaSinkWithCastModes media_sink_with_cast_modes(
      MediaSink(sink_id, sink_name, MediaSink::IconType::CAST));
  media_sink_with_cast_modes.cast_modes.insert(cast_mode);

  return media_sink_with_cast_modes;
}

}  // namespace

class MockMediaRouterUI : public MediaRouterUI {
 public:
  explicit MockMediaRouterUI(content::WebUI* web_ui)
      : MediaRouterUI(web_ui) {}
  ~MockMediaRouterUI() {}

  MOCK_METHOD0(UIInitialized, void());
  MOCK_CONST_METHOD0(UserSelectedTabMirroringForCurrentOrigin, bool());
  MOCK_METHOD1(RecordCastModeSelection, void(MediaCastMode cast_mode));
  MOCK_CONST_METHOD0(cast_modes, const std::set<MediaCastMode>&());
  MOCK_CONST_METHOD0(GetRouteProviderExtensionId, const std::string&());
  MOCK_METHOD1(OnMediaControllerUIAvailable,
               void(const MediaRoute::Id& route_id));
  MOCK_METHOD0(OnMediaControllerUIClosed, void());
  MOCK_METHOD0(PlayRoute, void());
  MOCK_METHOD0(PauseRoute, void());
  MOCK_METHOD1(SeekRoute, void(base::TimeDelta time));
  MOCK_METHOD1(SetRouteMute, void(bool mute));
  MOCK_METHOD1(SetRouteVolume, void(float volume));
  MOCK_CONST_METHOD0(GetMediaRouteController, const MediaRouteController*());
};

class TestMediaRouterWebUIMessageHandler
    : public MediaRouterWebUIMessageHandler {
 public:
  explicit TestMediaRouterWebUIMessageHandler(MediaRouterUI* media_router_ui)
      : MediaRouterWebUIMessageHandler(media_router_ui),
        email_(kUserEmailForTesting),
        domain_(kUserDomainForTesting) {}
  ~TestMediaRouterWebUIMessageHandler() override = default;

  AccountInfo GetAccountInfo() override {
    AccountInfo info = AccountInfo();
    info.account_id = info.gaia = info.email = email_;
    info.hosted_domain = domain_;
    info.full_name = info.given_name = "name";
    info.locale = "locale";
    info.picture_url = "picture";

    return info;
  }

  void SetEmailAndDomain(const std::string& email, const std::string& domain) {
    email_ = email;
    domain_ = domain;
  }

 private:
  std::string email_;
  std::string domain_;
};

class MediaRouterWebUIMessageHandlerTest : public MediaRouterWebUITest {
 public:
  MediaRouterWebUIMessageHandlerTest()
      : web_ui_(base::MakeUnique<content::TestWebUI>()),
        provider_extension_id_(kProviderExtensionIdForTesting) {}
  ~MediaRouterWebUIMessageHandlerTest() override {}

  // BrowserWithTestWindowTest:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    chrome::NewTab(browser());
    web_ui_->set_web_contents(
        browser()->tab_strip_model()->GetActiveWebContents());
    mock_media_router_ui_ = base::MakeUnique<MockMediaRouterUI>(web_ui_.get());
    handler_ = base::MakeUnique<TestMediaRouterWebUIMessageHandler>(
        mock_media_router_ui_.get());
    handler_->SetWebUIForTest(web_ui_.get());
  }

  void TearDown() override {
    handler_.reset();
    mock_media_router_ui_.reset();
    web_ui_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  const std::string& provider_extension_id() const {
    return provider_extension_id_;
  }

  // Gets the call data for the function call made to |web_ui_|. There needs
  // to be one call made, and its function name must be |function_name|.
  const content::TestWebUI::CallData& GetCallData(
      const std::string& function_name) {
    EXPECT_EQ(1u, web_ui_->call_data().size());
    const content::TestWebUI::CallData& call_data = *web_ui_->call_data()[0];
    EXPECT_EQ(function_name, call_data.function_name());

    return call_data;
  }

  // Gets the dictionary passed into a call to the |web_ui_| as the argument.
  // There needs to be one call made, and its function name must be
  // |function_name|.
  const base::DictionaryValue* ExtractDictFromCallArg(
      const std::string& function_name) {
    const content::TestWebUI::CallData& call_data = GetCallData(function_name);
    const base::Value* arg1 = call_data.arg1();
    const base::DictionaryValue* dict_value = nullptr;
    CHECK(arg1->GetAsDictionary(&dict_value));

    return dict_value;
  }

  // Gets the first element of the list passed in as the argument to a call to
  // the |web_ui_| as a dictionary. There needs to be one call made, and its
  // function name must be |function_name|.
  const base::DictionaryValue* ExtractDictFromListFromCallArg(
      const std::string& function_name) {
    const content::TestWebUI::CallData& call_data = GetCallData(function_name);
    const base::Value* arg1 = call_data.arg1();
    const base::ListValue* list_value = nullptr;
    CHECK(arg1->GetAsList(&list_value));
    const base::DictionaryValue* dict_value = nullptr;
    CHECK(list_value->GetDictionary(0, &dict_value));

    return dict_value;
  }

 protected:
  std::unique_ptr<content::TestWebUI> web_ui_;
  std::unique_ptr<MockMediaRouterUI> mock_media_router_ui_;
  std::unique_ptr<TestMediaRouterWebUIMessageHandler> handler_;
  const std::string provider_extension_id_;
};

TEST_F(MediaRouterWebUIMessageHandlerTest, UpdateSinks) {
  MediaSink::Id sink_id("sinkId123");
  MediaSinkWithCastModes media_sink_with_cast_modes =
      CreateMediaSinkWithCastMode(sink_id, MediaCastMode::TAB_MIRROR);

  handler_->UpdateSinks({media_sink_with_cast_modes});
  const base::DictionaryValue* sinks_with_identity_value =
      ExtractDictFromCallArg("media_router.ui.setSinkListAndIdentity");

  // Email is not displayed if there is no sinks with domain.
  EXPECT_FALSE(GetBooleanFromDict(sinks_with_identity_value, "showEmail"));

  // Domain is not displayed if there is no sinks with domain.
  EXPECT_FALSE(GetBooleanFromDict(sinks_with_identity_value, "showDomain"));

  const base::ListValue* sinks_list_value = nullptr;
  ASSERT_TRUE(sinks_with_identity_value->GetList("sinks", &sinks_list_value));
  const base::DictionaryValue* sink_value = nullptr;
  ASSERT_TRUE(sinks_list_value->GetDictionary(0, &sink_value));

  EXPECT_EQ(sink_id, GetStringFromDict(sink_value, "id"));
  EXPECT_EQ(media_sink_with_cast_modes.sink.name(),
            GetStringFromDict(sink_value, "name"));
  EXPECT_EQ(static_cast<int>(MediaCastMode::TAB_MIRROR),
            GetIntegerFromDict(sink_value, "castModes"));
}

TEST_F(MediaRouterWebUIMessageHandlerTest, UpdateSinksWithIdentity) {
  MediaSinkWithCastModes media_sink_with_cast_modes =
      CreateMediaSinkWithCastMode("sinkId123", MediaCastMode::TAB_MIRROR);
  media_sink_with_cast_modes.sink.set_domain(kUserDomainForTesting);

  handler_->UpdateSinks({media_sink_with_cast_modes});
  const base::DictionaryValue* sinks_with_identity_value =
      ExtractDictFromCallArg("media_router.ui.setSinkListAndIdentity");

  EXPECT_TRUE(GetBooleanFromDict(sinks_with_identity_value, "showEmail"));
  // Sink domain is not displayed if it matches user domain.
  EXPECT_FALSE(GetBooleanFromDict(sinks_with_identity_value, "showDomain"));
  EXPECT_EQ(kUserEmailForTesting,
            GetStringFromDict(sinks_with_identity_value, "userEmail"));
}

TEST_F(MediaRouterWebUIMessageHandlerTest,
       UpdateSinksWithIdentityAndPseudoSink) {
  MediaSinkWithCastModes media_sink_with_cast_modes =
      CreateMediaSinkWithCastMode("pseudo:sinkId1", MediaCastMode::TAB_MIRROR);
  media_sink_with_cast_modes.sink.set_domain(kUserDomainForTesting);

  handler_->UpdateSinks({media_sink_with_cast_modes});
  const base::DictionaryValue* sinks_with_identity_value =
      ExtractDictFromCallArg("media_router.ui.setSinkListAndIdentity");

  EXPECT_FALSE(GetBooleanFromDict(sinks_with_identity_value, "showEmail"));
}

TEST_F(MediaRouterWebUIMessageHandlerTest, UpdateSinksWithIdentityAndDomain) {
  MediaSinkWithCastModes media_sink_with_cast_modes =
      CreateMediaSinkWithCastMode("sinkId123", MediaCastMode::TAB_MIRROR);
  std::string domain_name("google.com");
  media_sink_with_cast_modes.sink.set_domain(domain_name);

  handler_->UpdateSinks({media_sink_with_cast_modes});
  const base::DictionaryValue* sinks_with_identity_value =
      ExtractDictFromCallArg("media_router.ui.setSinkListAndIdentity");

  // Domain is displayed for sinks with domains that are not the user domain.
  EXPECT_TRUE(GetBooleanFromDict(sinks_with_identity_value, "showDomain"));
}

TEST_F(MediaRouterWebUIMessageHandlerTest, UpdateSinksWithNoDomain) {
  MediaSinkWithCastModes media_sink_with_cast_modes =
      CreateMediaSinkWithCastMode("sinkId123", MediaCastMode::TAB_MIRROR);
  std::string user_email("nobody@gmail.com");
  std::string user_domain("NO_HOSTED_DOMAIN");
  std::string domain_name("default");
  media_sink_with_cast_modes.sink.set_domain(domain_name);

  handler_->SetEmailAndDomain(user_email, user_domain);
  handler_->UpdateSinks({media_sink_with_cast_modes});
  const base::DictionaryValue* sinks_with_identity_value =
      ExtractDictFromCallArg("media_router.ui.setSinkListAndIdentity");

  const base::ListValue* sinks_list_value = nullptr;
  ASSERT_TRUE(sinks_with_identity_value->GetList("sinks", &sinks_list_value));
  const base::DictionaryValue* sink_value = nullptr;
  ASSERT_TRUE(sinks_list_value->GetDictionary(0, &sink_value));

  // Domain should not be shown if there were only default sink domains.
  EXPECT_FALSE(GetBooleanFromDict(sinks_with_identity_value, "showDomain"));

  // Sink domain should be empty if user has no hosted domain.
  EXPECT_EQ(std::string(), GetStringFromDict(sink_value, "domain"));
}

TEST_F(MediaRouterWebUIMessageHandlerTest, UpdateSinksWithDefaultDomain) {
  MediaSinkWithCastModes media_sink_with_cast_modes =
      CreateMediaSinkWithCastMode("sinkId123", MediaCastMode::TAB_MIRROR);
  std::string domain_name("default");
  media_sink_with_cast_modes.sink.set_domain(domain_name);

  handler_->UpdateSinks({media_sink_with_cast_modes});
  const base::DictionaryValue* sinks_with_identity_value =
      ExtractDictFromCallArg("media_router.ui.setSinkListAndIdentity");

  const base::ListValue* sinks_list_value = nullptr;
  ASSERT_TRUE(sinks_with_identity_value->GetList("sinks", &sinks_list_value));
  const base::DictionaryValue* sink_value = nullptr;
  ASSERT_TRUE(sinks_list_value->GetDictionary(0, &sink_value));

  // Domain should not be shown if there were only default sink domains.
  EXPECT_FALSE(GetBooleanFromDict(sinks_with_identity_value, "showDomain"));

  // Sink domain should be updated from 'default' to user domain.
  EXPECT_EQ(kUserDomainForTesting, GetStringFromDict(sink_value, "domain"));
}

TEST_F(MediaRouterWebUIMessageHandlerTest, UpdateRoutes) {
  const MediaRoute route = CreateRoute();
  std::vector<MediaRoute::Id> joinable_route_ids = {route.media_route_id()};
  std::unordered_map<MediaRoute::Id, MediaCastMode> current_cast_modes;
  current_cast_modes.insert(
      std::make_pair(route.media_route_id(), MediaCastMode::DEFAULT));

  EXPECT_CALL(*mock_media_router_ui_, GetRouteProviderExtensionId()).WillOnce(
      ReturnRef(provider_extension_id()));
  handler_->UpdateRoutes({route}, joinable_route_ids, current_cast_modes);
  const base::DictionaryValue* route_value =
      ExtractDictFromListFromCallArg("media_router.ui.setRouteList");

  EXPECT_EQ(route.media_route_id(), GetStringFromDict(route_value, "id"));
  EXPECT_EQ(route.media_sink_id(), GetStringFromDict(route_value, "sinkId"));
  EXPECT_EQ(route.description(), GetStringFromDict(route_value, "description"));
  EXPECT_EQ(route.is_local(), GetBooleanFromDict(route_value, "isLocal"));
  EXPECT_TRUE(GetBooleanFromDict(route_value, "canJoin"));
  EXPECT_EQ(MediaCastMode::DEFAULT,
            GetIntegerFromDict(route_value, "currentCastMode"));
  std::string expected_path = base::StringPrintf("%s://%s/%s",
                                  extensions::kExtensionScheme,
                                  kProviderExtensionIdForTesting,
                                  kControllerPathForTesting);
  EXPECT_EQ(expected_path,
            GetStringFromDict(route_value, "customControllerPath"));
}

TEST_F(MediaRouterWebUIMessageHandlerTest, UpdateRoutesIncognito) {
  handler_->set_incognito_for_test(true);
  const MediaRoute route = CreateRoute();

  EXPECT_CALL(*mock_media_router_ui_, GetRouteProviderExtensionId())
      .WillOnce(ReturnRef(provider_extension_id()));
  handler_->UpdateRoutes({route}, std::vector<MediaRoute::Id>(),
                         std::unordered_map<MediaRoute::Id, MediaCastMode>());
  const base::DictionaryValue* route_value =
      ExtractDictFromListFromCallArg("media_router.ui.setRouteList");

  EXPECT_EQ(route.media_route_id(), GetStringFromDict(route_value, "id"));
  EXPECT_EQ(route.media_sink_id(), GetStringFromDict(route_value, "sinkId"));
  EXPECT_EQ(route.description(), GetStringFromDict(route_value, "description"));
  EXPECT_EQ(route.is_local(), GetBooleanFromDict(route_value, "isLocal"));
  EXPECT_FALSE(GetBooleanFromDict(route_value, "canJoin"));

  int actual_current_cast_mode = -1;
  EXPECT_FALSE(
      route_value->GetInteger("currentCastMode", &actual_current_cast_mode));
  std::string custom_controller_path;
  EXPECT_FALSE(
      route_value->GetString("customControllerPath", &custom_controller_path));
}

TEST_F(MediaRouterWebUIMessageHandlerTest, UpdateMediaRouteStatus) {
  MediaStatus status;
  status.title = "test title";
  status.description = "test description";
  status.can_play_pause = true;
  status.can_set_volume = true;
  status.is_paused = true;
  status.duration = base::TimeDelta::FromSeconds(90);
  status.current_time = base::TimeDelta::FromSeconds(80);
  status.volume = 0.9;

  handler_->UpdateMediaRouteStatus(status);
  const base::DictionaryValue* status_value =
      ExtractDictFromCallArg("media_router.ui.updateRouteStatus");

  EXPECT_EQ(status.title, GetStringFromDict(status_value, "title"));
  EXPECT_EQ(status.description, GetStringFromDict(status_value, "description"));
  EXPECT_EQ(status.can_play_pause,
            GetBooleanFromDict(status_value, "canPlayPause"));
  EXPECT_EQ(status.can_mute, GetBooleanFromDict(status_value, "canMute"));
  EXPECT_EQ(status.can_set_volume,
            GetBooleanFromDict(status_value, "canSetVolume"));
  EXPECT_EQ(status.can_seek, GetBooleanFromDict(status_value, "canSeek"));
  EXPECT_EQ(status.is_paused, GetBooleanFromDict(status_value, "isPaused"));
  EXPECT_EQ(status.is_muted, GetBooleanFromDict(status_value, "isMuted"));
  EXPECT_EQ(status.duration.InSeconds(),
            GetIntegerFromDict(status_value, "duration"));
  EXPECT_EQ(status.current_time.InSeconds(),
            GetIntegerFromDict(status_value, "currentTime"));
  EXPECT_EQ(status.volume, GetDoubleFromDict(status_value, "volume"));
}

TEST_F(MediaRouterWebUIMessageHandlerTest, OnCreateRouteResponseReceived) {
  MediaRoute route = CreateRoute();
  bool incognito = false;
  route.set_incognito(incognito);

  EXPECT_CALL(*mock_media_router_ui_, GetRouteProviderExtensionId())
      .WillOnce(ReturnRef(provider_extension_id()));
  handler_->OnCreateRouteResponseReceived(route.media_sink_id(), &route);
  const content::TestWebUI::CallData& call_data =
      GetCallData("media_router.ui.onCreateRouteResponseReceived");
  const base::Value* sink_id_value = nullptr;
  ASSERT_TRUE(call_data.arg1()->GetAsString(&sink_id_value));
  EXPECT_EQ(route.media_sink_id(), sink_id_value->GetString());

  const base::DictionaryValue* route_value = nullptr;
  ASSERT_TRUE(call_data.arg2()->GetAsDictionary(&route_value));
  EXPECT_EQ(route.media_route_id(), GetStringFromDict(route_value, "id"));
  EXPECT_EQ(route.media_sink_id(), GetStringFromDict(route_value, "sinkId"));
  EXPECT_EQ(route.description(), GetStringFromDict(route_value, "description"));
  EXPECT_EQ(route.is_local(), GetBooleanFromDict(route_value, "isLocal"));
  std::string expected_path = base::StringPrintf(
      "%s://%s/%s", extensions::kExtensionScheme,
      kProviderExtensionIdForTesting, kControllerPathForTesting);
  EXPECT_EQ(expected_path,
            GetStringFromDict(route_value, "customControllerPath"));

  bool route_for_display = false;
  ASSERT_TRUE(call_data.arg3()->GetAsBoolean(&route_for_display));
  EXPECT_TRUE(route_for_display);
}

TEST_F(MediaRouterWebUIMessageHandlerTest,
       OnCreateRouteResponseReceivedIncognito) {
  handler_->set_incognito_for_test(true);
  MediaRoute route = CreateRoute();
  bool incognito = true;
  route.set_incognito(incognito);

  EXPECT_CALL(*mock_media_router_ui_, GetRouteProviderExtensionId()).WillOnce(
      ReturnRef(provider_extension_id()));
  handler_->OnCreateRouteResponseReceived(route.media_sink_id(), &route);
  const content::TestWebUI::CallData& call_data =
      GetCallData("media_router.ui.onCreateRouteResponseReceived");
  const base::Value* sink_id_value = nullptr;
  ASSERT_TRUE(call_data.arg1()->GetAsString(&sink_id_value));
  EXPECT_EQ(route.media_sink_id(), sink_id_value->GetString());

  const base::DictionaryValue* route_value = nullptr;
  ASSERT_TRUE(call_data.arg2()->GetAsDictionary(&route_value));
  EXPECT_EQ(route.media_route_id(), GetStringFromDict(route_value, "id"));
  EXPECT_EQ(route.media_sink_id(), GetStringFromDict(route_value, "sinkId"));
  EXPECT_EQ(route.description(), GetStringFromDict(route_value, "description"));
  EXPECT_EQ(route.is_local(), GetBooleanFromDict(route_value, "isLocal"));

  std::string actual_path;
  EXPECT_FALSE(route_value->GetString("customControllerPath", &actual_path));

  bool route_for_display = false;
  ASSERT_TRUE(call_data.arg3()->GetAsBoolean(&route_for_display));
  EXPECT_TRUE(route_for_display);
}

TEST_F(MediaRouterWebUIMessageHandlerTest, UpdateIssue) {
  std::string issue_title("An issue");
  std::string issue_message("This is an issue");
  IssueInfo::Action default_action = IssueInfo::Action::LEARN_MORE;
  std::vector<IssueInfo::Action> secondary_actions;
  secondary_actions.push_back(IssueInfo::Action::DISMISS);
  MediaRoute::Id route_id("routeId123");
  IssueInfo info(issue_title, default_action, IssueInfo::Severity::FATAL);
  info.message = issue_message;
  info.secondary_actions = secondary_actions;
  info.route_id = route_id;
  Issue issue(info);
  const Issue::Id& issue_id = issue.id();

  handler_->UpdateIssue(issue);
  const base::DictionaryValue* issue_value =
      ExtractDictFromCallArg("media_router.ui.setIssue");

  // Initialized to invalid issue id.
  EXPECT_EQ(issue_id, GetIntegerFromDict(issue_value, "id"));
  EXPECT_EQ(issue_title, GetStringFromDict(issue_value, "title"));
  EXPECT_EQ(issue_message, GetStringFromDict(issue_value, "message"));

  // Initialized to invalid action type.
  EXPECT_EQ(static_cast<int>(default_action),
            GetIntegerFromDict(issue_value, "defaultActionType"));
  EXPECT_EQ(static_cast<int>(secondary_actions[0]),
            GetIntegerFromDict(issue_value, "secondaryActionType"));
  EXPECT_EQ(route_id, GetStringFromDict(issue_value, "routeId"));

  // The issue is blocking since it is FATAL.
  EXPECT_TRUE(GetBooleanFromDict(issue_value, "isBlocking"));
}

TEST_F(MediaRouterWebUIMessageHandlerTest, RecordCastModeSelection) {
  base::ListValue args;
  args.AppendInteger(MediaCastMode::DEFAULT);
  EXPECT_CALL(*mock_media_router_ui_,
              RecordCastModeSelection(MediaCastMode::DEFAULT))
      .Times(1);
  handler_->OnReportSelectedCastMode(&args);

  args.Clear();
  args.AppendInteger(MediaCastMode::TAB_MIRROR);
  EXPECT_CALL(*mock_media_router_ui_,
              RecordCastModeSelection(MediaCastMode::TAB_MIRROR))
      .Times(1);
  handler_->OnReportSelectedCastMode(&args);
}

TEST_F(MediaRouterWebUIMessageHandlerTest, RetrieveCastModeSelection) {
  base::ListValue args;
  std::set<MediaCastMode> cast_modes = {MediaCastMode::TAB_MIRROR};
  EXPECT_CALL(*mock_media_router_ui_, GetRouteProviderExtensionId())
      .WillRepeatedly(ReturnRef(provider_extension_id()));
  EXPECT_CALL(*mock_media_router_ui_, cast_modes())
      .WillRepeatedly(ReturnRef(cast_modes));

  EXPECT_CALL(*mock_media_router_ui_,
              UserSelectedTabMirroringForCurrentOrigin())
      .WillOnce(Return(true));
  handler_->OnRequestInitialData(&args);
  const content::TestWebUI::CallData& call_data1 = *web_ui_->call_data()[0];
  ASSERT_EQ("media_router.ui.setInitialData", call_data1.function_name());
  const base::DictionaryValue* initial_data = nullptr;
  ASSERT_TRUE(call_data1.arg1()->GetAsDictionary(&initial_data));
  EXPECT_TRUE(GetBooleanFromDict(initial_data, "useTabMirroring"));

  EXPECT_CALL(*mock_media_router_ui_,
              UserSelectedTabMirroringForCurrentOrigin())
      .WillOnce(Return(false));
  handler_->OnRequestInitialData(&args);
  const content::TestWebUI::CallData& call_data2 = *web_ui_->call_data()[1];
  ASSERT_EQ("media_router.ui.setInitialData", call_data2.function_name());
  ASSERT_TRUE(call_data2.arg1()->GetAsDictionary(&initial_data));
  EXPECT_FALSE(GetBooleanFromDict(initial_data, "useTabMirroring"));
}

TEST_F(MediaRouterWebUIMessageHandlerTest, OnRouteDetailsOpenedAndClosed) {
  const std::string route_id = "routeId123";
  base::ListValue args_list;
  base::DictionaryValue* args;
  args_list.Append(base::MakeUnique<base::DictionaryValue>());
  args_list.GetDictionary(0, &args);
  args->SetString("routeId", route_id);

  EXPECT_CALL(*mock_media_router_ui_, OnMediaControllerUIAvailable(route_id));
  handler_->OnMediaControllerAvailable(&args_list);

  args_list.Clear();
  EXPECT_CALL(*mock_media_router_ui_, OnMediaControllerUIClosed());
  handler_->OnMediaControllerClosed(&args_list);
}

TEST_F(MediaRouterWebUIMessageHandlerTest, OnMediaCommandsReceived) {
  mojom::MediaControllerPtr mojo_media_controller;
  mojo::MakeRequest(&mojo_media_controller);
  MockMediaRouter media_router;
  scoped_refptr<MockMediaRouteController> controller =
      new MockMediaRouteController("routeId", std::move(mojo_media_controller),
                                   &media_router);
  EXPECT_CALL(*mock_media_router_ui_, GetMediaRouteController())
      .WillRepeatedly(Return(controller.get()));
  MediaStatus status;
  status.duration = base::TimeDelta::FromSeconds(100);
  handler_->UpdateMediaRouteStatus(status);

  base::ListValue args_list;

  EXPECT_CALL(*controller, Play());
  handler_->OnPlayCurrentMedia(&args_list);

  EXPECT_CALL(*controller, Pause());
  handler_->OnPauseCurrentMedia(&args_list);

  base::DictionaryValue* args;
  args_list.Append(base::MakeUnique<base::DictionaryValue>());
  args_list.GetDictionary(0, &args);

  const int time = 50;
  args->SetInteger("time", time);
  EXPECT_CALL(*controller, Seek(base::TimeDelta::FromSeconds(time)));
  handler_->OnSeekCurrentMedia(&args_list);

  args->Clear();
  args->SetBoolean("mute", true);
  EXPECT_CALL(*controller, SetMute(true));
  handler_->OnSetCurrentMediaMute(&args_list);

  const double volume = 0.4;
  args->Clear();
  args->SetDouble("volume", volume);
  EXPECT_CALL(*controller, SetVolume(volume));
  handler_->OnSetCurrentMediaVolume(&args_list);
}

TEST_F(MediaRouterWebUIMessageHandlerTest, OnInvalidMediaCommandsReceived) {
  mojom::MediaControllerPtr mojo_media_controller;
  mojo::MakeRequest(&mojo_media_controller);
  MockMediaRouter media_router;
  scoped_refptr<MockMediaRouteController> controller =
      new MockMediaRouteController("routeId", std::move(mojo_media_controller),
                                   &media_router);
  EXPECT_CALL(*mock_media_router_ui_, GetMediaRouteController())
      .WillRepeatedly(Return(controller.get()));

  MediaStatus status;
  status.duration = base::TimeDelta::FromSeconds(100);
  handler_->UpdateMediaRouteStatus(status);

  EXPECT_CALL(*controller, Seek(_)).Times(0);
  EXPECT_CALL(*controller, SetVolume(_)).Times(0);

  base::ListValue args_list;

  base::DictionaryValue* args;
  args_list.Append(base::MakeUnique<base::DictionaryValue>());
  args_list.GetDictionary(0, &args);

  // Seek positions greater than the duration or negative should be ignored.
  args->SetInteger("time", 101);
  handler_->OnSeekCurrentMedia(&args_list);
  args->SetInteger("time", -10);
  handler_->OnSeekCurrentMedia(&args_list);

  args->Clear();

  // Volumes outside of the [0, 1] range should be ignored.
  args->SetDouble("volume", 1.5);
  handler_->OnSetCurrentMediaVolume(&args_list);
  args->SetDouble("volume", 1.5);
  handler_->OnSetCurrentMediaVolume(&args_list);
}

TEST_F(MediaRouterWebUIMessageHandlerTest, OnRouteControllerInvalidated) {
  handler_->OnRouteControllerInvalidated();
  EXPECT_EQ(1u, web_ui_->call_data().size());
  const content::TestWebUI::CallData& call_data = *web_ui_->call_data()[0];
  EXPECT_EQ("media_router.ui.onRouteControllerInvalidated",
            call_data.function_name());
}

}  // namespace media_router
