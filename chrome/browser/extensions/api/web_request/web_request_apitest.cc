// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/active_tab_permission_granter.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/extensions/test_extension_dir.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/login/login_handler.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/extensions/extension_process_policy.h"
#include "chrome/test/base/search_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chromeos/login/scoped_test_public_session_login_state.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_type.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/api/web_request/web_request_api.h"
#include "extensions/browser/blocked_action_type.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/features/feature.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/test_data_directory.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request_context_getter.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"

#if defined(OS_CHROMEOS)
#include "chromeos/login/login_state.h"
#endif  // defined(OS_CHROMEOS)

using content::WebContents;

namespace extensions {

namespace {

class CancelLoginDialog : public content::NotificationObserver {
 public:
  CancelLoginDialog() {
    registrar_.Add(this,
                   chrome::NOTIFICATION_AUTH_NEEDED,
                   content::NotificationService::AllSources());
  }

  ~CancelLoginDialog() override {}

  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override {
    LoginHandler* handler =
        content::Details<LoginNotificationDetails>(details).ptr()->handler();
    handler->CancelAuth();
  }

 private:
  content::NotificationRegistrar registrar_;

 DISALLOW_COPY_AND_ASSIGN(CancelLoginDialog);
};

// Sends an XHR request to the provided host, port, and path, and responds when
// the request was sent.
const char kPerformXhrJs[] =
    "var url = 'http://%s:%d/%s';\n"
    "var xhr = new XMLHttpRequest();\n"
    "xhr.open('GET', url);\n"
    "xhr.onload = function() {\n"
    "  window.domAutomationController.send(true);\n"
    "};\n"
    "xhr.onerror = function() {\n"
    "  window.domAutomationController.send(false);\n"
    "};\n"
    "xhr.send();\n";

// Performs an XHR in the given |frame|, replying when complete.
void PerformXhrInFrame(content::RenderFrameHost* frame,
                      const std::string& host,
                      int port,
                      const std::string& page) {
  bool success = false;
  EXPECT_TRUE(ExecuteScriptAndExtractBool(
      frame,
      base::StringPrintf(kPerformXhrJs, host.c_str(), port, page.c_str()),
      &success));
  EXPECT_TRUE(success);
}

// Returns the current count of webRequests received by the |extension| in
// the background page (assumes the extension stores a value on the window
// object). Returns -1 if something goes awry.
int GetWebRequestCountFromBackgroundPage(const Extension* extension,
                                         content::BrowserContext* context) {
  ExtensionHost* host =
      ProcessManager::Get(context)->GetBackgroundHostForExtension(
          extension->id());
  if (!host || !host->host_contents())
    return -1;

  int count = -1;
  if (!ExecuteScriptAndExtractInt(
          host->host_contents(),
          "window.domAutomationController.send(window.webRequestCount)",
          &count))
    return -1;
  return count;
}

// A test delegate to wait allow waiting for responses to complete with an
// expected status and given content.
// TODO(devlin): Other similar classes exist elsewhere. Pull this into a common
// test class.
class TestURLFetcherDelegate : public net::URLFetcherDelegate {
 public:
  // Creating the TestURLFetcherDelegate automatically creates and starts a
  // URLFetcher.
  TestURLFetcherDelegate(
      scoped_refptr<net::URLRequestContextGetter> context_getter,
      const GURL& url,
      net::URLRequestStatus expected_request_status)
      : expected_request_status_(expected_request_status),
        fetcher_(net::URLFetcher::Create(url,
                                         net::URLFetcher::GET,
                                         this,
                                         TRAFFIC_ANNOTATION_FOR_TESTS)) {
    fetcher_->SetRequestContext(context_getter.get());
    fetcher_->Start();
  }
  ~TestURLFetcherDelegate() override {}

  void SetExpectedResponse(const std::string& expected_response) {
    expected_response_ = expected_response;
  }

  void WaitForCompletion() { run_loop_.Run(); }

  // net::URLFetcherDelegate:
  void OnURLFetchComplete(const net::URLFetcher* source) override {
    EXPECT_EQ(expected_request_status_.status(), source->GetStatus().status());
    EXPECT_EQ(expected_request_status_.error(), source->GetStatus().error());
    if (expected_response_) {
      std::string response;
      ASSERT_TRUE(fetcher_->GetResponseAsString(&response));
      EXPECT_EQ(*expected_response_, response);
    }

    run_loop_.Quit();
  }

 private:
  const net::URLRequestStatus expected_request_status_;
  base::Optional<std::string> expected_response_;
  std::unique_ptr<net::URLFetcher> fetcher_;
  base::RunLoop run_loop_;

  DISALLOW_COPY_AND_ASSIGN(TestURLFetcherDelegate);
};

}  // namespace

class ExtensionWebRequestApiTest : public ExtensionApiTest {
 public:
  void SetUpInProcessBrowserTestFixture() override {
    ExtensionApiTest::SetUpInProcessBrowserTestFixture();
    host_resolver()->AddRule("*", "127.0.0.1");
  }

  void RunPermissionTest(
      const char* extension_directory,
      bool load_extension_with_incognito_permission,
      bool wait_for_extension_loaded_in_incognito,
      const char* expected_content_regular_window,
      const char* exptected_content_incognito_window);
};

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestApi) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_api.html")) << message_;
}

// Fails often on Windows dbg bots. http://crbug.com/177163
#if defined(OS_WIN)
#define MAYBE_WebRequestSimple DISABLED_WebRequestSimple
#else
#define MAYBE_WebRequestSimple WebRequestSimple
#endif  // defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, MAYBE_WebRequestSimple) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_simple.html")) <<
      message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestComplex) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_complex.html")) <<
      message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestTypes) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_types.html")) << message_;
}

#if defined(OS_CHROMEOS)
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestPublicSession) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  chromeos::ScopedTestPublicSessionLoginState login_state;
  // Disable a CHECK while doing api tests.
  WebRequestPermissions::AllowAllExtensionLocationsInPublicSessionForTesting(
      true);
  ASSERT_TRUE(RunExtensionSubtest("webrequest_public_session", "test.html")) <<
      message_;
  WebRequestPermissions::AllowAllExtensionLocationsInPublicSessionForTesting(
      false);
}
#endif  // defined(OS_CHROMEOS)

// Test that a request to an OpenSearch description document (OSDD) generates
// an event with the expected details.
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestTestOSDD) {
  // An OSDD request is only generated when a main frame at is loaded at /, so
  // serve osdd/index.html from the root of the test server:
  embedded_test_server()->ServeFilesFromDirectory(
      test_data_dir_.AppendASCII("webrequest/osdd"));
  ASSERT_TRUE(StartEmbeddedTestServer());

  search_test_utils::WaitForTemplateURLServiceToLoad(
      TemplateURLServiceFactory::GetForProfile(profile()));
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_osdd.html")) << message_;
}

// Test that the webRequest events are dispatched with the expected details when
// a frame or tab is removed while a response is being received.
// Flaky: https://crbug.com/617865
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       DISABLED_WebRequestUnloadAfterRequest) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_unload.html?1")) <<
      message_;
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_unload.html?2")) <<
      message_;
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_unload.html?3")) <<
      message_;
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_unload.html?4")) <<
      message_;
}

// Test that the webRequest events are dispatched with the expected details when
// a frame or tab is immediately removed after starting a request.
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestUnloadImmediately) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_unload.html?5")) <<
      message_;
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_unload.html?6")) <<
      message_;
}

// Flaky (sometimes crash): http://crbug.com/140976
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       DISABLED_WebRequestAuthRequired) {
  CancelLoginDialog login_dialog_helper;

  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_auth_required.html")) <<
      message_;
}

// This test times out regularly on win_rel trybots. See http://crbug.com/122178
// Also on Linux/ChromiumOS debug, ASAN and MSAN builds.
// https://crbug.com/670415
#if defined(OS_WIN) || !defined(NDEBUG) || defined(ADDRESS_SANITIZER) || \
    defined(MEMORY_SANITIZER)
#define MAYBE_WebRequestBlocking DISABLED_WebRequestBlocking
#else
#define MAYBE_WebRequestBlocking WebRequestBlocking
#endif
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, MAYBE_WebRequestBlocking) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_blocking.html")) <<
      message_;
}

// Fails often on Windows dbg bots. http://crbug.com/177163
#if defined(OS_WIN)
#define MAYBE_WebRequestNewTab DISABLED_WebRequestNewTab
#else
#define MAYBE_WebRequestNewTab WebRequestNewTab
#endif  // defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, MAYBE_WebRequestNewTab) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  // Wait for the extension to set itself up and return control to us.
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_newTab.html"))
      << message_;

  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  content::WaitForLoadStop(tab);

  ResultCatcher catcher;

  ExtensionService* service =
      ExtensionSystem::Get(browser()->profile())->extension_service();
  const Extension* extension =
      service->GetExtensionById(last_loaded_extension_id(), false);
  GURL url = extension->GetResourceURL("newTab/a.html");

  ui_test_utils::NavigateToURL(browser(), url);

  // There's a link on a.html with target=_blank. Click on it to open it in a
  // new tab.
  blink::WebMouseEvent mouse_event(blink::WebInputEvent::kMouseDown,
                                   blink::WebInputEvent::kNoModifiers,
                                   blink::WebInputEvent::kTimeStampForTesting);
  mouse_event.button = blink::WebMouseEvent::Button::kLeft;
  mouse_event.SetPositionInWidget(7, 7);
  mouse_event.click_count = 1;
  tab->GetRenderViewHost()->GetWidget()->ForwardMouseEvent(mouse_event);
  mouse_event.SetType(blink::WebInputEvent::kMouseUp);
  tab->GetRenderViewHost()->GetWidget()->ForwardMouseEvent(mouse_event);

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestDeclarative1) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_declarative1.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestDeclarative2) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_declarative2.html"))
      << message_;
}

void ExtensionWebRequestApiTest::RunPermissionTest(
    const char* extension_directory,
    bool load_extension_with_incognito_permission,
    bool wait_for_extension_loaded_in_incognito,
    const char* expected_content_regular_window,
    const char* exptected_content_incognito_window) {
  ResultCatcher catcher;
  catcher.RestrictToBrowserContext(browser()->profile());
  ResultCatcher catcher_incognito;
  catcher_incognito.RestrictToBrowserContext(
      browser()->profile()->GetOffTheRecordProfile());

  ExtensionTestMessageListener listener("done", false);
  ExtensionTestMessageListener listener_incognito("done_incognito", false);

  int load_extension_flags = kFlagNone;
  if (load_extension_with_incognito_permission)
    load_extension_flags |= kFlagEnableIncognito;
  ASSERT_TRUE(LoadExtensionWithFlags(
      test_data_dir_.AppendASCII("webrequest_permissions")
                    .AppendASCII(extension_directory),
      load_extension_flags));

  // Test that navigation in regular window is properly redirected.
  EXPECT_TRUE(listener.WaitUntilSatisfied());

  // This navigation should be redirected.
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("/extensions/test_file.html"));

  std::string body;
  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecuteScriptAndExtractString(
        tab,
        "window.domAutomationController.send(document.body.textContent)",
        &body));
  EXPECT_EQ(expected_content_regular_window, body);

  // Test that navigation in OTR window is properly redirected.
  Browser* otr_browser =
      OpenURLOffTheRecord(browser()->profile(), GURL("about:blank"));

  if (wait_for_extension_loaded_in_incognito)
    EXPECT_TRUE(listener_incognito.WaitUntilSatisfied());

  // This navigation should be redirected if
  // load_extension_with_incognito_permission is true.
  ui_test_utils::NavigateToURL(
      otr_browser,
      embedded_test_server()->GetURL("/extensions/test_file.html"));

  body.clear();
  WebContents* otr_tab = otr_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecuteScriptAndExtractString(
      otr_tab,
      "window.domAutomationController.send(document.body.textContent)",
      &body));
  EXPECT_EQ(exptected_content_incognito_window, body);
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestDeclarativePermissionSpanning1) {
  // Test spanning with incognito permission.
  ASSERT_TRUE(StartEmbeddedTestServer());
  RunPermissionTest("spanning", true, false, "redirected1", "redirected1");
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestDeclarativePermissionSpanning2) {
  // Test spanning without incognito permission.
  ASSERT_TRUE(StartEmbeddedTestServer());
  RunPermissionTest("spanning", false, false, "redirected1", "");
}


IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestDeclarativePermissionSplit1) {
  // Test split with incognito permission.
  ASSERT_TRUE(StartEmbeddedTestServer());
  RunPermissionTest("split", true, true, "redirected1", "redirected2");
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestDeclarativePermissionSplit2) {
  // Test split without incognito permission.
  ASSERT_TRUE(StartEmbeddedTestServer());
  RunPermissionTest("split", false, false, "redirected1", "");
}

// TODO(vabr): Cure these flaky tests, http://crbug.com/238179.
#if !defined(NDEBUG)
#define MAYBE_PostData1 DISABLED_PostData1
#define MAYBE_PostData2 DISABLED_PostData2
#else
#define MAYBE_PostData1 PostData1
#define MAYBE_PostData2 PostData2
#endif
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, MAYBE_PostData1) {
  // Test HTML form POST data access with the default and "url" encoding.
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_post1.html")) <<
      message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, MAYBE_PostData2) {
  // Test HTML form POST data access with the multipart and plaintext encoding.
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_post2.html")) <<
      message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       DeclarativeSendMessage) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionTest("webrequest_sendmessage")) << message_;
}

// Check that reloading an extension that runs in incognito split mode and
// has two active background pages with registered events does not crash the
// browser. Regression test for http://crbug.com/224094
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, IncognitoSplitModeReload) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  // Wait for rules to be set up.
  ExtensionTestMessageListener listener("done", false);
  ExtensionTestMessageListener listener_incognito("done_incognito", false);

  const Extension* extension = LoadExtensionWithFlags(
      test_data_dir_.AppendASCII("webrequest_reload"), kFlagEnableIncognito);
  ASSERT_TRUE(extension);
  OpenURLOffTheRecord(browser()->profile(), GURL("about:blank"));

  EXPECT_TRUE(listener.WaitUntilSatisfied());
  EXPECT_TRUE(listener_incognito.WaitUntilSatisfied());

  // Reload extension and wait for rules to be set up again. This should not
  // crash the browser.
  ExtensionTestMessageListener listener2("done", false);
  ExtensionTestMessageListener listener_incognito2("done_incognito", false);

  ReloadExtension(extension->id());

  EXPECT_TRUE(listener2.WaitUntilSatisfied());
  EXPECT_TRUE(listener_incognito2.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, ExtensionRequests) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ExtensionTestMessageListener listener_main1("web_request_status1", true);
  ExtensionTestMessageListener listener_main2("web_request_status2", true);

  ExtensionTestMessageListener listener_app("app_done", false);
  ExtensionTestMessageListener listener_extension("extension_done", false);

  // Set up webRequest listener
  ASSERT_TRUE(LoadExtension(
          test_data_dir_.AppendASCII("webrequest_extensions/main")));
  EXPECT_TRUE(listener_main1.WaitUntilSatisfied());
  EXPECT_TRUE(listener_main2.WaitUntilSatisfied());

  // Perform some network activity in an app and another extension.
  ASSERT_TRUE(LoadExtension(
          test_data_dir_.AppendASCII("webrequest_extensions/app")));
  ASSERT_TRUE(LoadExtension(
          test_data_dir_.AppendASCII("webrequest_extensions/extension")));

  EXPECT_TRUE(listener_app.WaitUntilSatisfied());
  EXPECT_TRUE(listener_extension.WaitUntilSatisfied());

  // Load a page, a content script will ping us when it is ready.
  ExtensionTestMessageListener listener_pageready("contentscript_ready", true);
  ui_test_utils::NavigateToURL(browser(), embedded_test_server()->GetURL(
          "/extensions/test_file.html?match_webrequest_test"));
  EXPECT_TRUE(listener_pageready.WaitUntilSatisfied());

  // The extension and app-generated requests should not have triggered any
  // webRequest event filtered by type 'xmlhttprequest'.
  // (check this here instead of before the navigation, in case the webRequest
  // event routing is slow for some reason).
  ExtensionTestMessageListener listener_result(false);
  listener_main1.Reply("");
  EXPECT_TRUE(listener_result.WaitUntilSatisfied());
  EXPECT_EQ("Did not intercept any requests.", listener_result.message());

  ExtensionTestMessageListener listener_contentscript("contentscript_done",
                                                      false);
  ExtensionTestMessageListener listener_framescript("framescript_done", false);

  // Proceed with the final tests: Let the content script fire a request and
  // then load an iframe which also fires a XHR request.
  listener_pageready.Reply("");
  EXPECT_TRUE(listener_contentscript.WaitUntilSatisfied());
  EXPECT_TRUE(listener_framescript.WaitUntilSatisfied());

  // Collect the visited URLs. The content script and subframe does not run in
  // the extension's process, so the requests should be visible to the main
  // extension.
  listener_result.Reset();
  listener_main2.Reply("");
  EXPECT_TRUE(listener_result.WaitUntilSatisfied());

  // The extension frame does run in the extension's process.
  EXPECT_EQ("Intercepted requests: ?contentscript", listener_result.message());
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, HostedAppRequest) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  GURL hosted_app_url(
      embedded_test_server()->GetURL(
          "/extensions/api_test/webrequest_hosted_app/index.html"));
  scoped_refptr<Extension> hosted_app =
      ExtensionBuilder()
          .SetManifest(
              DictionaryBuilder()
                  .Set("name", "Some hosted app")
                  .Set("version", "1")
                  .Set("manifest_version", 2)
                  .Set("app", DictionaryBuilder()
                                  .Set("launch", DictionaryBuilder()
                                                     .Set("web_url",
                                                          hosted_app_url.spec())
                                                     .Build())
                                  .Build())
                  .Build())
          .Build();
  ExtensionSystem::Get(browser()->profile())
      ->extension_service()
      ->AddExtension(hosted_app.get());

  ExtensionTestMessageListener listener1("main_frame", false);
  ExtensionTestMessageListener listener2("xmlhttprequest", false);

  ASSERT_TRUE(LoadExtension(
          test_data_dir_.AppendASCII("webrequest_hosted_app")));

  ui_test_utils::NavigateToURL(browser(), hosted_app_url);

  EXPECT_TRUE(listener1.WaitUntilSatisfied());
  EXPECT_TRUE(listener2.WaitUntilSatisfied());
}

// Tests that webRequest works with the --scripts-require-action feature.
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestWithWithheldPermissions) {
  FeatureSwitch::ScopedOverride enable_scripts_require_action(
      FeatureSwitch::scripts_require_action(), true);

  content::SetupCrossSiteRedirector(embedded_test_server());
  ASSERT_TRUE(embedded_test_server()->Start());

  // Load an extension that registers a listener for webRequest events, and
  // wait 'til it's initialized.
  ExtensionTestMessageListener listener("ready", false);
  const Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII("webrequest_activetab"));
  ASSERT_TRUE(extension) << message_;
  EXPECT_TRUE(listener.WaitUntilSatisfied());

  // Navigate the browser to a page in a new tab.
  GURL url = embedded_test_server()->GetURL(
                 "/cross-site/a.com/iframe_cross_site.html");
  const std::string kHost = "a.com";
  chrome::NavigateParams params(browser(), url, ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  ui_test_utils::NavigateToURL(&params);

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);
  ExtensionActionRunner* runner =
      ExtensionActionRunner::GetForWebContents(web_contents);
  ASSERT_TRUE(runner);

  int port = embedded_test_server()->port();
  const std::string kXhrPath = "simple.html";

  // The extension shouldn't have currently received any webRequest events,
  // since it doesn't have permission (and shouldn't receive any from an XHR).
  EXPECT_EQ(0, GetWebRequestCountFromBackgroundPage(extension, profile()));

  content::RenderFrameHost* main_frame = nullptr;
  content::RenderFrameHost* child_frame = nullptr;
  auto get_main_and_child_frame = [](content::WebContents* web_contents,
                                     content::RenderFrameHost** main_frame,
                                     content::RenderFrameHost** child_frame) {
    *child_frame = nullptr;
    *main_frame = web_contents->GetMainFrame();
    std::vector<content::RenderFrameHost*> all_frames =
        web_contents->GetAllFrames();
    ASSERT_EQ(3u, all_frames.size());
    *child_frame = all_frames[0] == *main_frame ? all_frames[1] : all_frames[0];
    ASSERT_TRUE(*child_frame);
  };

  get_main_and_child_frame(web_contents, &main_frame, &child_frame);
  const std::string kMainHost = main_frame->GetLastCommittedURL().host();
  const std::string kChildHost = child_frame->GetLastCommittedURL().host();

  PerformXhrInFrame(main_frame, kHost, port, kXhrPath);
  PerformXhrInFrame(child_frame, kChildHost, port, kXhrPath);
  EXPECT_EQ(0, GetWebRequestCountFromBackgroundPage(extension, profile()));
  EXPECT_EQ(BLOCKED_ACTION_WEB_REQUEST, runner->GetBlockedActions(extension));

  // Grant activeTab permission, and perform another XHR. The extension should
  // receive the event.
  runner->set_default_bubble_close_action_for_testing(
      base::WrapUnique(new ToolbarActionsBarBubbleDelegate::CloseAction(
          ToolbarActionsBarBubbleDelegate::CLOSE_EXECUTE)));
  runner->RunAction(extension, true);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));
  // The runner will have refreshed the page...
  get_main_and_child_frame(web_contents, &main_frame, &child_frame);
  EXPECT_EQ(BLOCKED_ACTION_NONE, runner->GetBlockedActions(extension));

  int xhr_count = GetWebRequestCountFromBackgroundPage(extension, profile());
  // ... which means that we should have a non-zero xhr count...
  EXPECT_GT(xhr_count, 0);
  // ... and the extension should receive future events.
  PerformXhrInFrame(main_frame, kHost, port, kXhrPath);
  ++xhr_count;
  EXPECT_EQ(xhr_count,
            GetWebRequestCountFromBackgroundPage(extension, profile()));

  // However, activeTab only grants access to the main frame, not to child
  // frames. As such, trying to XHR in the child frame should still fail.
  PerformXhrInFrame(child_frame, kChildHost, port, kXhrPath);
  EXPECT_EQ(xhr_count,
            GetWebRequestCountFromBackgroundPage(extension, profile()));
  // But since there's no way for the user to currently grant access to child
  // frames, this shouldn't show up as a blocked action.
  EXPECT_EQ(BLOCKED_ACTION_NONE, runner->GetBlockedActions(extension));

  // If we revoke the extension's tab permissions, it should no longer receive
  // webRequest events.
  ActiveTabPermissionGranter* granter =
      TabHelper::FromWebContents(web_contents)->active_tab_permission_granter();
  ASSERT_TRUE(granter);
  granter->RevokeForTesting();
  base::RunLoop().RunUntilIdle();
  PerformXhrInFrame(main_frame, kHost, port, kXhrPath);
  EXPECT_EQ(xhr_count,
            GetWebRequestCountFromBackgroundPage(extension, profile()));
  EXPECT_EQ(BLOCKED_ACTION_WEB_REQUEST, runner->GetBlockedActions(extension));
}

// Verify that requests to clientsX.google.com are protected properly.
// First test requests from a standard renderer and a webui renderer.
// Then test a request from the browser process.
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestClientsGoogleComProtection) {
  ASSERT_TRUE(embedded_test_server()->Start());
  int port = embedded_test_server()->port();

  // Load an extension that registers a listener for webRequest events, and
  // wait 'til it's initialized.
  ExtensionTestMessageListener listener("ready", false);
  const Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("webrequest_clients_google_com"));
  ASSERT_TRUE(extension) << message_;
  EXPECT_TRUE(listener.WaitUntilSatisfied());

  // Perform requests to https://client1.google.com from renderer processes.

  struct TestCase {
    const char* main_frame_url;
    bool request_to_clients1_google_com_visible;
  } testcases[] = {
      {"http://www.example.com", true}, {"chrome://settings", false},
  };

  // Expected number of requests to clients1.google.com observed so far.
  int expected_requests_observed = 0;
  EXPECT_EQ(expected_requests_observed,
            GetWebRequestCountFromBackgroundPage(extension, profile()));

  for (const auto& testcase : testcases) {
    SCOPED_TRACE(testcase.main_frame_url);

    GURL url;
    if (base::StartsWith(testcase.main_frame_url, "chrome://",
                         base::CompareCase::INSENSITIVE_ASCII)) {
      url = GURL(testcase.main_frame_url);
    } else {
      url = GURL(base::StringPrintf("%s:%d/simple.html",
                                    testcase.main_frame_url, port));
    }

    chrome::NavigateParams params(browser(), url, ui::PAGE_TRANSITION_TYPED);
    ui_test_utils::NavigateToURL(&params);

    EXPECT_EQ(expected_requests_observed,
              GetWebRequestCountFromBackgroundPage(extension, profile()));

    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(web_contents);

    const char kRequest[] =
        "var xhr = new XMLHttpRequest();\n"
        "xhr.open('GET', 'https://clients1.google.com');\n"
        "xhr.onload = () => {window.domAutomationController.send(true);};\n"
        "xhr.onerror = () => {window.domAutomationController.send(false);};\n"
        "xhr.send();\n";

    bool success = false;
    EXPECT_TRUE(ExecuteScriptAndExtractBool(web_contents->GetMainFrame(),
                                            kRequest, &success));
    // Requests always fail due to cross origin nature.
    EXPECT_FALSE(success);

    if (testcase.request_to_clients1_google_com_visible)
      ++expected_requests_observed;

    EXPECT_EQ(expected_requests_observed,
              GetWebRequestCountFromBackgroundPage(extension, profile()));
  }

  // Perform request to https://client1.google.com from browser process.

  class TestURLFetcherDelegate : public net::URLFetcherDelegate {
   public:
    explicit TestURLFetcherDelegate(const base::Closure& quit_loop_func)
        : quit_loop_func_(quit_loop_func) {}
    ~TestURLFetcherDelegate() override {}

    void OnURLFetchComplete(const net::URLFetcher* source) override {
      EXPECT_EQ(net::HTTP_OK, source->GetResponseCode());
      quit_loop_func_.Run();
    }

   private:
    base::Closure quit_loop_func_;
  };
  base::RunLoop run_loop;
  TestURLFetcherDelegate delegate(run_loop.QuitClosure());

  net::URLFetcherImplFactory url_fetcher_impl_factory;
  net::FakeURLFetcherFactory url_fetcher_factory(&url_fetcher_impl_factory);
  url_fetcher_factory.SetFakeResponse(GURL("https://client1.google.com"),
                                      "hello my friend", net::HTTP_OK,
                                      net::URLRequestStatus::SUCCESS);
  std::unique_ptr<net::URLFetcher> fetcher =
      url_fetcher_factory.CreateURLFetcher(1,
                                           GURL("https://client1.google.com"),
                                           net::URLFetcher::GET, &delegate);
  fetcher->Start();
  run_loop.Run();

  // This request should not be observed by the extension.
  EXPECT_EQ(expected_requests_observed,
            GetWebRequestCountFromBackgroundPage(extension, profile()));
}

// Test that the webRequest events are dispatched for the WebSocket handshake
// requests.
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebSocketRequest) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(StartWebSocketServer(net::GetWebSocketTestDataDirectory()));
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_websocket.html"))
      << message_;
}

// Test that the webRequest events are dispatched for the WebSocket handshake
// requests when authenrication is requested by server.
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebSocketRequestAuthRequired) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(StartWebSocketServer(net::GetWebSocketTestDataDirectory(), true));
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_websocket_auth.html"))
      << message_;
}

// Test behavior when intercepting requests from a browser-initiated url fetch.
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestURLFetcherInterception) {
  // Create an extension that intercepts (and blocks) requests to example.com.
  TestExtensionDir test_dir;
  test_dir.WriteManifest(
      R"({
           "name": "web_request_browser_interception",
           "description": "tests that browser requests aren't intercepted",
           "version": "0.1",
           "permissions": ["webRequest", "webRequestBlocking", "*://*/*"],
           "manifest_version": 2,
           "background": { "scripts": ["background.js"] }
         })");
  test_dir.WriteFile(FILE_PATH_LITERAL("background.js"),
                     R"(chrome.webRequest.onBeforeRequest.addListener(
             function(details) {
               return {cancel: details.url.indexOf('example.com') != -1};
             },
             {urls: ["<all_urls>"]},
             ["blocking"]);
         chrome.test.sendMessage('ready');)");

  ASSERT_TRUE(StartEmbeddedTestServer());
  const Extension* extension = nullptr;
  {
    ExtensionTestMessageListener listener("ready", false);
    extension = LoadExtension(test_dir.UnpackedPath());
    ASSERT_TRUE(extension);
    EXPECT_TRUE(listener.WaitUntilSatisfied());
  }

  GURL google_url =
      embedded_test_server()->GetURL("google.com", "/extensions/body1.html");
  // Taken from test/data/extensions/body1.html.
  const char kGoogleBodyContent[] = "dog";
  const char kGoogleFullContent[] = "<html>\n<body>dog</body>\n</html>\n";

  // First, check normal requets (e.g., navigations) to verify the extension
  // is working correctly.
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ui_test_utils::NavigateToURL(browser(), google_url);
  EXPECT_EQ(google_url, web_contents->GetLastCommittedURL());
  {
    // google.com should succeed.
    std::string content;
    EXPECT_TRUE(content::ExecuteScriptAndExtractString(
        web_contents,
        "domAutomationController.send(document.body.textContent.trim());",
        &content));
    EXPECT_EQ(kGoogleBodyContent, content);
  }

  GURL example_url =
      embedded_test_server()->GetURL("example.com", "/extensions/body2.html");
  // Taken from test/data/extensions/body2.html.
  const char kExampleBodyContent[] = "cat";
  const char kExampleFullContent[] = "<html>\n<body>cat</body>\n</html>\n";

  ui_test_utils::NavigateToURL(browser(), example_url);
  {
    // example.com should fail.
    content::NavigationEntry* nav_entry =
        web_contents->GetController().GetLastCommittedEntry();
    ASSERT_TRUE(nav_entry);
    EXPECT_EQ(content::PAGE_TYPE_ERROR, nav_entry->GetPageType());
    std::string content;
    EXPECT_TRUE(content::ExecuteScriptAndExtractString(
        web_contents,
        "domAutomationController.send(document.body.textContent.trim());",
        &content));
    EXPECT_NE(kExampleBodyContent, content);
  }

  // Next, try a series of requests through URLRequestFetchers (rather than a
  // renderer).
  net::URLRequestContextGetter* profile_context =
      browser()->profile()->GetRequestContext();
  {
    // google.com should be unaffected by the extension and should succeed.
    SCOPED_TRACE("google.com with Profile's request context");
    TestURLFetcherDelegate url_fetcher(profile_context, google_url,
                                       net::URLRequestStatus());
    url_fetcher.SetExpectedResponse(kGoogleFullContent);
    url_fetcher.WaitForCompletion();
  }
  {
    // example.com should fail.
    SCOPED_TRACE("example.com with Profile's request context");
    TestURLFetcherDelegate url_fetcher(
        profile_context, example_url,
        net::URLRequestStatus(net::URLRequestStatus::FAILED,
                              net::ERR_BLOCKED_BY_CLIENT));
    url_fetcher.WaitForCompletion();
  }

  // Requests going through the system request context should always succeed.
  net::URLRequestContextGetter* system_context =
      g_browser_process->system_request_context();
  {
    // google.com should succeed (again).
    SCOPED_TRACE("google.com with System's request context");
    TestURLFetcherDelegate url_fetcher(system_context, google_url,
                                       net::URLRequestStatus());
    url_fetcher.SetExpectedResponse(kGoogleFullContent);
    url_fetcher.WaitForCompletion();
  }
  {
    // example.com should also succeed, since it's not through the profile's
    // request context.
    SCOPED_TRACE("example.com with System's request context");
    TestURLFetcherDelegate url_fetcher(system_context, example_url,
                                       net::URLRequestStatus());
    url_fetcher.SetExpectedResponse(kExampleFullContent);
    url_fetcher.WaitForCompletion();
  }
}

}  // namespace extensions
