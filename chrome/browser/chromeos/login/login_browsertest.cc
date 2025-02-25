// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/system/tray/system_tray.h"
#include "ash/wm_window.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/login/login_manager_test.h"
#include "chrome/browser/chromeos/login/login_wizard.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/login/test/js_checker.h"
#include "chrome/browser/chromeos/login/ui/login_display_host_impl.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/stub_install_attributes.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/fake_auth_policy_client.h"
#include "chromeos/settings/cros_settings_names.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/user_manager/user_names.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/extension_system.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/test/rect_test_util.h"

using ::gfx::test::RectContains;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;

namespace chromeos {
namespace {

const char kGaiaId[] = "12345";
const char kTestUser[] = "test-user@gmail.com";
const char kPassword[] = "password";

class LoginUserTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitchASCII(switches::kLoginUser, "TestUser@gmail.com");
    command_line->AppendSwitchASCII(switches::kLoginProfile, "hash");
  }
};

class LoginGuestTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kGuestSession);
    command_line->AppendSwitch(::switches::kIncognito);
    command_line->AppendSwitchASCII(switches::kLoginProfile, "hash");
    command_line->AppendSwitchASCII(
        switches::kLoginUser, user_manager::GuestAccountId().GetUserEmail());
  }
};

class LoginCursorTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kLoginManager);
  }
};

class LoginSigninTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kLoginManager);
    command_line->AppendSwitch(switches::kForceLoginManagerInTests);
  }

  void TearDownOnMainThread() override {
    // Close the login manager, which otherwise holds a KeepAlive that is not
    // cleared in time by the end of the test.
    LoginDisplayHost::default_host()->Finalize();
  }

  void SetUpOnMainThread() override {
    LoginDisplayHostImpl::DisableRestrictiveProxyCheckForTest();
  }
};

class LoginTest : public LoginManagerTest {
 public:
  LoginTest() : LoginManagerTest(true) {}
  ~LoginTest() override {}

  void StartGaiaAuthOffline() {
    content::DOMMessageQueue message_queue;
    const std::string js = "(function() {"
      "var authenticator = $('gaia-signin').gaiaAuthHost_;"
      "authenticator.addEventListener('ready',"
        "function f() {"
          "authenticator.removeEventListener('ready', f);"
          "window.domAutomationController.setAutomationId(0);"
          "window.domAutomationController.send('offlineLoaded');"
        "});"
      "$('error-offline-login-link').onclick();"
    "})();";
    ASSERT_TRUE(content::ExecuteScript(web_contents(), js));

    std::string message;
    do {
      ASSERT_TRUE(message_queue.WaitForMessage(&message));
    } while (message != "\"offlineLoaded\"");
  }

  void SubmitGaiaAuthOfflineForm(const std::string& user_email,
                                 const std::string& password) {
    const std::string animated_pages =
        "document.querySelector('#offline-gaia /deep/ "
        "#animatedPages')";
    const std::string email_input =
        "document.querySelector('#offline-gaia /deep/ #emailInput')";
    const std::string email_next_button =
        "document.querySelector('#offline-gaia /deep/ #emailSection "
        "/deep/ #button')";
    const std::string password_input =
        "document.querySelector('#offline-gaia /deep/ "
        "#passwordInput')";
    const std::string password_next_button =
        "document.querySelector('#offline-gaia /deep/ #passwordSection"
        " /deep/ #button')";

    content::DOMMessageQueue message_queue;
    JSExpect("!document.querySelector('#offline-gaia').hidden");
    JSExpect("document.querySelector('#signin-frame').hidden");
    const std::string js =
        animated_pages +
        ".addEventListener('neon-animation-finish',"
        "function() {"
        "window.domAutomationController.setAutomationId(0);"
        "window.domAutomationController.send('switchToPassword');"
        "})";
    ASSERT_TRUE(content::ExecuteScript(web_contents(), js));
    std::string set_email = email_input + ".value = '$Email'";
    base::ReplaceSubstringsAfterOffset(&set_email, 0, "$Email", user_email);
    ASSERT_TRUE(content::ExecuteScript(web_contents(), set_email));
    ASSERT_TRUE(content::ExecuteScript(web_contents(),
                                       email_next_button + ".fire('tap')"));
    std::string message;
    do {
      ASSERT_TRUE(message_queue.WaitForMessage(&message));
    } while (message != "\"switchToPassword\"");

    std::string set_password = password_input + ".value = '$Password'";
    base::ReplaceSubstringsAfterOffset(&set_password, 0, "$Password", password);
    ASSERT_TRUE(content::ExecuteScript(web_contents(), set_password));
    ASSERT_TRUE(content::ExecuteScript(web_contents(),
                                       password_next_button + ".fire('tap')"));
  }

  void PrepareOfflineLogin() {
    bool show_user;
    ASSERT_TRUE(CrosSettings::Get()->GetBoolean(
        kAccountsPrefShowUserNamesOnSignIn, &show_user));
    ASSERT_FALSE(show_user);

    StartGaiaAuthOffline();

    UserContext user_context(
        AccountId::FromUserEmailGaiaId(kTestUser, kGaiaId));
    user_context.SetKey(Key(kPassword));
    SetExpectedCredentials(user_context);
  }
};

const char kDeviceId[] = "device_id";
const char kTestRealm[] = "test_realm.com";
const char kTestActiveDirectoryUser[] = "test-user";
const char kAdMachineInput[] = "machineNameInput";
const char kAdUserInput[] = "userInput";
const char kAdPasswordInput[] = "passwordInput";
const char kAdButton[] = "button";
const char kAdWelcomMessage[] = "welcomeMsg";
const char kAdAutocompleteRealm[] = "userInput /deep/ #domainLabel";

class ActiveDirectoryLoginTest : public LoginManagerTest {
 public:
  ActiveDirectoryLoginTest()
      : LoginManagerTest(true),
        install_attributes_(
            ScopedStubInstallAttributes::CreateActiveDirectoryManaged(
                kTestRealm,
                kDeviceId)) {}

  ~ActiveDirectoryLoginTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kOobeSkipPostLogin);
    LoginManagerTest::SetUpCommandLine(command_line);
  }

  void SetUpOnMainThread() override {
    LoginManagerTest::SetUpOnMainThread();
    fake_auth_policy_client_ = static_cast<FakeAuthPolicyClient*>(
        DBusThreadManager::Get()->GetAuthPolicyClient());
  }

  void MarkAsActiveDirectoryEnterprise() {
    StartupUtils::MarkOobeCompleted();
    base::RunLoop loop;
    fake_auth_policy_client_->RefreshDevicePolicy(
        base::Bind(&ActiveDirectoryLoginTest::OnRefreshedPolicy,
                   base::Unretained(this), loop.QuitClosure()));
    loop.Run();
  }

  // Checks if Active Directory login is visible.
  void TestLoginVisible() {
    // Checks if Gaia signin is hidden.
    JSExpect("document.querySelector('#signin-frame').hidden");

    // Checks if Active Directory signin is visible.
    JSExpect("!document.querySelector('#offline-ad-auth').hidden");
    JSExpect(JSElement(kAdMachineInput) + ".hidden");
    JSExpect("!" + JSElement(kAdUserInput) + ".hidden");
    JSExpect("!" + JSElement(kAdPasswordInput) + ".hidden");

    const std::string innerText(".innerText");
    // Checks if Active Directory welcome message contains realm.
    EXPECT_EQ(l10n_util::GetStringFUTF8(IDS_AD_DOMAIN_AUTH_WELCOME_MESSAGE,
                                        base::UTF8ToUTF16(kTestRealm)),
              js_checker().GetString(JSElement(kAdWelcomMessage) + innerText));

    // Checks if realm is set to autocomplete username.
    EXPECT_EQ(
        "@" + std::string(kTestRealm),
        js_checker().GetString(JSElement(kAdAutocompleteRealm) + innerText));

    // Checks if bottom bar is visible.
    JSExpect("!Oobe.getInstance().headerHidden");
  }

  // Checks if user input is marked as invalid.
  void TestUserError() {
    TestLoginVisible();
    JSExpect(JSElement(kAdUserInput) + ".isInvalid");
  }

  // Checks if password input is marked as invalid.
  void TestPasswordError() {
    TestLoginVisible();
    JSExpect(JSElement(kAdPasswordInput) + ".isInvalid");
  }

  // Checks if autocomplete domain is visible for the user input.
  void TestDomainVisible() {
    JSExpect("!" + JSElement(kAdAutocompleteRealm) + ".hidden");
  }

  // Checks if autocomplete domain is hidden for the user input.
  void TestDomainHidden() {
    JSExpect(JSElement(kAdAutocompleteRealm) + ".hidden");
  }

  // Sets username and password for the Active Directory login and submits it.
  void SubmitActiveDirectoryCredentials(const std::string& username,
                                        const std::string& password) {
    js_checker().ExecuteAsync(JSElement(kAdUserInput) + ".value='" + username +
                              "'");
    js_checker().ExecuteAsync(JSElement(kAdPasswordInput) + ".value='" +
                              password + "'");
    js_checker().Evaluate(JSElement(kAdButton) + ".fire('tap')");
  }

  void SetupActiveDirectoryJSNotifications() {
    js_checker().Evaluate(
        "var testInvalidateAd = login.GaiaSigninScreen.invalidateAd;"
        "login.GaiaSigninScreen.invalidateAd = function(user, errorState) {"
        "  testInvalidateAd(user, errorState);"
        "  window.domAutomationController.setAutomationId(0);"
        "  window.domAutomationController.send('ShowAuthError');"
        "}");
  }

  void WaitForMessage(content::DOMMessageQueue* message_queue,
                      const std::string& expected_message) {
    std::string message;
    do {
      ASSERT_TRUE(message_queue->WaitForMessage(&message));
    } while (message != expected_message);
  }

 protected:
  // Returns string representing element with id=|element_id| inside Active
  // Directory login element.
  std::string JSElement(const std::string& element_id) {
    return "document.querySelector('#offline-ad-auth /deep/ #" + element_id +
           "')";
  }
  FakeAuthPolicyClient* fake_auth_policy_client_ = nullptr;

 private:
  // Used for the callback from FakeAuthPolicy::RefreshDevicePolicy.
  void OnRefreshedPolicy(const base::Closure& closure, bool status) {
    EXPECT_TRUE(status);
    closure.Run();
  }

  ScopedStubInstallAttributes install_attributes_;

  DISALLOW_COPY_AND_ASSIGN(ActiveDirectoryLoginTest);
};

// Used to make sure that the system tray is visible and within the screen
// bounds after login.
void TestSystemTrayIsVisible(bool otr) {
  ash::SystemTray* tray = ash::Shell::Get()->GetPrimarySystemTray();
  aura::Window* primary_win = ash::Shell::GetPrimaryRootWindow();
  ash::WmShelf* wm_shelf = ash::WmShelf::ForWindow(primary_win);
  SCOPED_TRACE(testing::Message()
               << "ShelfVisibilityState=" << wm_shelf->GetVisibilityState()
               << " ShelfAutoHideBehavior=" << wm_shelf->auto_hide_behavior());
  EXPECT_TRUE(tray->visible());

  // This check flakes for LoginGuestTest: https://crbug.com/693106.
  if (!otr)
    EXPECT_TRUE(RectContains(primary_win->bounds(), tray->GetBoundsInScreen()));
}

}  // namespace

// After a chrome crash, the session manager will restart chrome with
// the -login-user flag indicating that the user is already logged in.
// This profile should NOT be an OTR profile.
IN_PROC_BROWSER_TEST_F(LoginUserTest, UserPassed) {
  Profile* profile = browser()->profile();
  std::string profile_base_path("hash");
  profile_base_path.insert(0, chrome::kProfileDirPrefix);
  EXPECT_EQ(profile_base_path, profile->GetPath().BaseName().value());
  EXPECT_FALSE(profile->IsOffTheRecord());

  TestSystemTrayIsVisible(false);
}

// Verifies the cursor is not hidden at startup when user is logged in.
IN_PROC_BROWSER_TEST_F(LoginUserTest, CursorShown) {
  EXPECT_TRUE(ash::Shell::Get()->cursor_manager()->IsCursorVisible());

  TestSystemTrayIsVisible(false);
}

// After a guest login, we should get the OTR default profile.
IN_PROC_BROWSER_TEST_F(LoginGuestTest, GuestIsOTR) {
  Profile* profile = browser()->profile();
  EXPECT_TRUE(profile->IsOffTheRecord());
  // Ensure there's extension service for this profile.
  EXPECT_TRUE(extensions::ExtensionSystem::Get(profile)->extension_service());

  TestSystemTrayIsVisible(true);
}

// Verifies the cursor is not hidden at startup when running guest session.
IN_PROC_BROWSER_TEST_F(LoginGuestTest, CursorShown) {
  EXPECT_TRUE(ash::Shell::Get()->cursor_manager()->IsCursorVisible());

  TestSystemTrayIsVisible(true);
}

// Verifies the cursor is hidden at startup on login screen.
IN_PROC_BROWSER_TEST_F(LoginCursorTest, CursorHidden) {
  // Login screen needs to be shown explicitly when running test.
  ShowLoginWizard(OobeScreen::SCREEN_SPECIAL_LOGIN);

  // Cursor should be hidden at startup
  EXPECT_FALSE(ash::Shell::Get()->cursor_manager()->IsCursorVisible());

  // Cursor should be shown after cursor is moved.
  EXPECT_TRUE(ui_test_utils::SendMouseMoveSync(gfx::Point()));
  EXPECT_TRUE(ash::Shell::Get()->cursor_manager()->IsCursorVisible());

  base::ThreadTaskRunnerHandle::Get()->DeleteSoon(
      FROM_HERE, LoginDisplayHost::default_host());

  TestSystemTrayIsVisible(false);
}

// Verifies that the webui for login comes up successfully.
IN_PROC_BROWSER_TEST_F(LoginSigninTest, WebUIVisible) {
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_LOGIN_OR_LOCK_WEBUI_VISIBLE,
      content::NotificationService::AllSources())
      .Wait();
}


IN_PROC_BROWSER_TEST_F(LoginTest, PRE_GaiaAuthOffline) {
  RegisterUser(kTestUser);
  StartupUtils::MarkOobeCompleted();
  CrosSettings::Get()->SetBoolean(kAccountsPrefShowUserNamesOnSignIn, false);
}

// Flaky, see http://crbug/692364.
IN_PROC_BROWSER_TEST_F(LoginTest, DISABLED_GaiaAuthOffline) {
  PrepareOfflineLogin();
  content::WindowedNotificationObserver session_start_waiter(
      chrome::NOTIFICATION_SESSION_STARTED,
      content::NotificationService::AllSources());
  SubmitGaiaAuthOfflineForm(kTestUser, kPassword);
  session_start_waiter.Wait();

  TestSystemTrayIsVisible(false);
}

// Marks as Active Directory enterprise device and OOBE as completed.
IN_PROC_BROWSER_TEST_F(ActiveDirectoryLoginTest, PRE_LoginSuccess) {
  MarkAsActiveDirectoryEnterprise();
}

// Test successful Active Directory login.
IN_PROC_BROWSER_TEST_F(ActiveDirectoryLoginTest, LoginSuccess) {
  TestLoginVisible();
  TestDomainVisible();

  content::WindowedNotificationObserver session_start_waiter(
      chrome::NOTIFICATION_SESSION_STARTED,
      content::NotificationService::AllSources());
  SubmitActiveDirectoryCredentials(kTestActiveDirectoryUser, kPassword);
  session_start_waiter.Wait();

  // Uncomment once flakiness is fixed, see http://crbug/692364.
  // TestSystemTrayIsVisible();
}

// Marks as Active Directory enterprise device and OOBE as completed.
IN_PROC_BROWSER_TEST_F(ActiveDirectoryLoginTest, PRE_LoginErrors) {
  MarkAsActiveDirectoryEnterprise();
}

// Test different UI errors for Active Directory login.
IN_PROC_BROWSER_TEST_F(ActiveDirectoryLoginTest, LoginErrors) {
  SetupActiveDirectoryJSNotifications();
  TestLoginVisible();
  TestDomainVisible();

  content::DOMMessageQueue message_queue;

  SubmitActiveDirectoryCredentials("", "");
  TestUserError();
  TestDomainVisible();

  SubmitActiveDirectoryCredentials(kTestActiveDirectoryUser, "");
  TestPasswordError();
  TestDomainVisible();

  SubmitActiveDirectoryCredentials(std::string(kTestActiveDirectoryUser) + "@",
                                   kPassword);
  TestUserError();
  TestDomainHidden();

  fake_auth_policy_client_->set_auth_error(authpolicy::ERROR_BAD_USER_NAME);
  SubmitActiveDirectoryCredentials(
      std::string(kTestActiveDirectoryUser) + "@" + kTestRealm, kPassword);
  WaitForMessage(&message_queue, "\"ShowAuthError\"");
  TestUserError();
  TestDomainVisible();

  fake_auth_policy_client_->set_auth_error(authpolicy::ERROR_BAD_PASSWORD);
  SubmitActiveDirectoryCredentials(kTestActiveDirectoryUser, kPassword);
  WaitForMessage(&message_queue, "\"ShowAuthError\"");
  TestPasswordError();
  TestDomainVisible();
}

}  // namespace chromeos
