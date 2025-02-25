// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/renderer/api_event_handler.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/memory/ptr_util.h"
#include "base/optional.h"
#include "base/test/mock_callback.h"
#include "base/values.h"
#include "extensions/common/event_filtering_info.h"
#include "extensions/renderer/api_binding_test.h"
#include "extensions/renderer/api_binding_test_util.h"
#include "gin/arguments.h"
#include "gin/converter.h"
#include "gin/public/context_holder.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace extensions {

namespace {

using MockEventChangeHandler = ::testing::StrictMock<
    base::MockCallback<APIEventHandler::EventListenersChangedMethod>>;

void DoNothingOnEventListenersChanged(const std::string& event_name,
                                      binding::EventListenersChanged change,
                                      const base::DictionaryValue* value,
                                      bool was_manual,
                                      v8::Local<v8::Context> context) {}

class APIEventHandlerTest : public APIBindingTest {
 protected:
  APIEventHandlerTest() {}
  ~APIEventHandlerTest() override {}

  void SetUp() override {
    APIBindingTest::SetUp();
    handler_ = base::MakeUnique<APIEventHandler>(
        base::Bind(&RunFunctionOnGlobalAndIgnoreResult),
        base::Bind(&DoNothingOnEventListenersChanged));
  }

  void TearDown() override {
    DisposeAllContexts();
    handler_.reset();
    APIBindingTest::TearDown();
  }

  void OnWillDisposeContext(v8::Local<v8::Context> context) override {
    ASSERT_TRUE(handler_);
    handler_->InvalidateContext(context);
  }

  void SetHandler(std::unique_ptr<APIEventHandler> handler) {
    handler_ = std::move(handler);
  }

  APIEventHandler* handler() { return handler_.get(); }

 private:
  std::unique_ptr<APIEventHandler> handler_;

  DISALLOW_COPY_AND_ASSIGN(APIEventHandlerTest);
};

}  // namespace

// Tests adding, removing, and querying event listeners by calling the
// associated methods on the JS object.
TEST_F(APIEventHandlerTest, AddingRemovingAndQueryingEventListeners) {
  const char kEventName[] = "alpha";
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  v8::Local<v8::Object> event =
      handler()->CreateEventInstance(kEventName, false, context);
  ASSERT_FALSE(event.IsEmpty());

  EXPECT_EQ(0u, handler()->GetNumEventListenersForTesting(kEventName, context));

  const char kListenerFunction[] = "(function() {})";
  v8::Local<v8::Function> listener_function =
      FunctionFromString(context, kListenerFunction);
  ASSERT_FALSE(listener_function.IsEmpty());

  const char kAddListenerFunction[] =
      "(function(event, listener) { event.addListener(listener); })";
  v8::Local<v8::Function> add_listener_function =
      FunctionFromString(context, kAddListenerFunction);

  {
    v8::Local<v8::Value> argv[] = {event, listener_function};
    RunFunction(add_listener_function, context, arraysize(argv), argv);
  }
  // There should only be one listener on the event.
  EXPECT_EQ(1u, handler()->GetNumEventListenersForTesting(kEventName, context));

  {
    v8::Local<v8::Value> argv[] = {event, listener_function};
    RunFunction(add_listener_function, context, arraysize(argv), argv);
  }
  // Trying to add the same listener again should be a no-op.
  EXPECT_EQ(1u, handler()->GetNumEventListenersForTesting(kEventName, context));

  // Test hasListener returns true for a listener that is present.
  const char kHasListenerFunction[] =
      "(function(event, listener) { return event.hasListener(listener); })";
  v8::Local<v8::Function> has_listener_function =
      FunctionFromString(context, kHasListenerFunction);
  {
    v8::Local<v8::Value> argv[] = {event, listener_function};
    v8::Local<v8::Value> result =
        RunFunction(has_listener_function, context, arraysize(argv), argv);
    bool has_listener = false;
    EXPECT_TRUE(gin::Converter<bool>::FromV8(isolate(), result, &has_listener));
    EXPECT_TRUE(has_listener);
  }

  // Test that hasListener returns false for a listener that isn't present.
  {
    v8::Local<v8::Function> not_a_listener =
        FunctionFromString(context, "(function() {})");
    v8::Local<v8::Value> argv[] = {event, not_a_listener};
    v8::Local<v8::Value> result =
        RunFunction(has_listener_function, context, arraysize(argv), argv);
    bool has_listener = false;
    EXPECT_TRUE(gin::Converter<bool>::FromV8(isolate(), result, &has_listener));
    EXPECT_FALSE(has_listener);
  }

  // Test hasListeners returns true
  const char kHasListenersFunction[] =
      "(function(event) { return event.hasListeners(); })";
  v8::Local<v8::Function> has_listeners_function =
      FunctionFromString(context, kHasListenersFunction);
  {
    v8::Local<v8::Value> argv[] = {event};
    v8::Local<v8::Value> result =
        RunFunction(has_listeners_function, context, arraysize(argv), argv);
    bool has_listeners = false;
    EXPECT_TRUE(
        gin::Converter<bool>::FromV8(isolate(), result, &has_listeners));
    EXPECT_TRUE(has_listeners);
  }

  const char kRemoveListenerFunction[] =
      "(function(event, listener) { event.removeListener(listener); })";
  v8::Local<v8::Function> remove_listener_function =
      FunctionFromString(context, kRemoveListenerFunction);
  {
    v8::Local<v8::Value> argv[] = {event, listener_function};
    RunFunction(remove_listener_function, context, arraysize(argv), argv);
  }
  EXPECT_EQ(0u, handler()->GetNumEventListenersForTesting(kEventName, context));

  {
    v8::Local<v8::Value> argv[] = {event};
    v8::Local<v8::Value> result =
        RunFunction(has_listeners_function, context, arraysize(argv), argv);
    bool has_listeners = false;
    EXPECT_TRUE(
        gin::Converter<bool>::FromV8(isolate(), result, &has_listeners));
    EXPECT_FALSE(has_listeners);
  }
}

// Tests listening for and firing different events.
TEST_F(APIEventHandlerTest, FiringEvents) {
  const char kAlphaName[] = "alpha";
  const char kBetaName[] = "beta";
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  v8::Local<v8::Object> alpha_event =
      handler()->CreateEventInstance(kAlphaName, false, context);
  v8::Local<v8::Object> beta_event =
      handler()->CreateEventInstance(kBetaName, false, context);
  ASSERT_FALSE(alpha_event.IsEmpty());
  ASSERT_FALSE(beta_event.IsEmpty());

  const char kAlphaListenerFunction1[] =
      "(function() {\n"
      "  if (!this.alphaCount1) this.alphaCount1 = 0;\n"
      "  ++this.alphaCount1;\n"
      "});\n";
  v8::Local<v8::Function> alpha_listener1 =
      FunctionFromString(context, kAlphaListenerFunction1);
  const char kAlphaListenerFunction2[] =
      "(function() {\n"
      "  if (!this.alphaCount2) this.alphaCount2 = 0;\n"
      "  ++this.alphaCount2;\n"
      "});\n";
  v8::Local<v8::Function> alpha_listener2 =
      FunctionFromString(context, kAlphaListenerFunction2);
  const char kBetaListenerFunction[] =
      "(function() {\n"
      "  if (!this.betaCount) this.betaCount = 0;\n"
      "  ++this.betaCount;\n"
      "});\n";
  v8::Local<v8::Function> beta_listener =
      FunctionFromString(context, kBetaListenerFunction);
  ASSERT_FALSE(alpha_listener1.IsEmpty());
  ASSERT_FALSE(alpha_listener2.IsEmpty());
  ASSERT_FALSE(beta_listener.IsEmpty());

  {
    const char kAddListenerFunction[] =
        "(function(event, listener) { event.addListener(listener); })";
    v8::Local<v8::Function> add_listener_function =
        FunctionFromString(context, kAddListenerFunction);
    {
      v8::Local<v8::Value> argv[] = {alpha_event, alpha_listener1};
      RunFunction(add_listener_function, context, arraysize(argv), argv);
    }
    {
      v8::Local<v8::Value> argv[] = {alpha_event, alpha_listener2};
      RunFunction(add_listener_function, context, arraysize(argv), argv);
    }
    {
      v8::Local<v8::Value> argv[] = {beta_event, beta_listener};
      RunFunction(add_listener_function, context, arraysize(argv), argv);
    }
  }

  EXPECT_EQ(2u, handler()->GetNumEventListenersForTesting(kAlphaName, context));
  EXPECT_EQ(1u, handler()->GetNumEventListenersForTesting(kBetaName, context));

  auto get_fired_count = [&context](const char* name) {
    v8::Local<v8::Value> res =
        GetPropertyFromObject(context->Global(), context, name);
    if (res->IsUndefined())
      return 0;
    int32_t count = 0;
    EXPECT_TRUE(
        gin::Converter<int32_t>::FromV8(context->GetIsolate(), res, &count))
        << name;
    return count;
  };

  EXPECT_EQ(0, get_fired_count("alphaCount1"));
  EXPECT_EQ(0, get_fired_count("alphaCount2"));
  EXPECT_EQ(0, get_fired_count("betaCount"));

  handler()->FireEventInContext(kAlphaName, context, base::ListValue(),
                                EventFilteringInfo());
  EXPECT_EQ(2u, handler()->GetNumEventListenersForTesting(kAlphaName, context));
  EXPECT_EQ(1u, handler()->GetNumEventListenersForTesting(kBetaName, context));

  EXPECT_EQ(1, get_fired_count("alphaCount1"));
  EXPECT_EQ(1, get_fired_count("alphaCount2"));
  EXPECT_EQ(0, get_fired_count("betaCount"));

  handler()->FireEventInContext(kAlphaName, context, base::ListValue(),
                                EventFilteringInfo());
  EXPECT_EQ(2, get_fired_count("alphaCount1"));
  EXPECT_EQ(2, get_fired_count("alphaCount2"));
  EXPECT_EQ(0, get_fired_count("betaCount"));

  handler()->FireEventInContext(kBetaName, context, base::ListValue(),
                                EventFilteringInfo());
  EXPECT_EQ(2, get_fired_count("alphaCount1"));
  EXPECT_EQ(2, get_fired_count("alphaCount2"));
  EXPECT_EQ(1, get_fired_count("betaCount"));
}

// Tests firing events with arguments.
TEST_F(APIEventHandlerTest, EventArguments) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  const char kEventName[] = "alpha";
  v8::Local<v8::Object> event =
      handler()->CreateEventInstance(kEventName, false, context);
  ASSERT_FALSE(event.IsEmpty());

  const char kListenerFunction[] =
      "(function() { this.eventArgs = Array.from(arguments); })";
  v8::Local<v8::Function> listener_function =
      FunctionFromString(context, kListenerFunction);
  ASSERT_FALSE(listener_function.IsEmpty());

  {
    const char kAddListenerFunction[] =
        "(function(event, listener) { event.addListener(listener); })";
    v8::Local<v8::Function> add_listener_function =
        FunctionFromString(context, kAddListenerFunction);
    v8::Local<v8::Value> argv[] = {event, listener_function};
    RunFunction(add_listener_function, context, arraysize(argv), argv);
  }

  const char kArguments[] = "['foo',1,{'prop1':'bar'}]";
  std::unique_ptr<base::ListValue> event_args = ListValueFromString(kArguments);
  ASSERT_TRUE(event_args);
  handler()->FireEventInContext(kEventName, context, *event_args,
                                EventFilteringInfo());

  EXPECT_EQ(
      ReplaceSingleQuotes(kArguments),
      GetStringPropertyFromObject(context->Global(), context, "eventArgs"));
}

// Test dispatching events to multiple contexts.
TEST_F(APIEventHandlerTest, MultipleContexts) {
  v8::HandleScope handle_scope(isolate());

  v8::Local<v8::Context> context_a = MainContext();
  v8::Local<v8::Context> context_b = AddContext();

  const char kEventName[] = "onFoo";


  v8::Local<v8::Function> listener_a = FunctionFromString(
      context_a, "(function(arg) { this.eventArgs = arg + 'alpha'; })");
  ASSERT_FALSE(listener_a.IsEmpty());
  v8::Local<v8::Function> listener_b = FunctionFromString(
      context_b, "(function(arg) { this.eventArgs = arg + 'beta'; })");
  ASSERT_FALSE(listener_b.IsEmpty());

  // Create two instances of the same event in different contexts.
  v8::Local<v8::Object> event_a =
      handler()->CreateEventInstance(kEventName, false, context_a);
  ASSERT_FALSE(event_a.IsEmpty());
  v8::Local<v8::Object> event_b =
      handler()->CreateEventInstance(kEventName, false, context_b);
  ASSERT_FALSE(event_b.IsEmpty());

  // Add two separate listeners to the event, one in each context.
  const char kAddListenerFunction[] =
      "(function(event, listener) { event.addListener(listener); })";
  {
    v8::Local<v8::Function> add_listener_a =
        FunctionFromString(context_a, kAddListenerFunction);
    v8::Local<v8::Value> argv[] = {event_a, listener_a};
    RunFunction(add_listener_a, context_a, arraysize(argv), argv);
  }
  EXPECT_EQ(1u,
            handler()->GetNumEventListenersForTesting(kEventName, context_a));
  EXPECT_EQ(0u,
            handler()->GetNumEventListenersForTesting(kEventName, context_b));

  {
    v8::Local<v8::Function> add_listener_b =
        FunctionFromString(context_b, kAddListenerFunction);
    v8::Local<v8::Value> argv[] = {event_b, listener_b};
    RunFunction(add_listener_b, context_b, arraysize(argv), argv);
  }
  EXPECT_EQ(1u,
            handler()->GetNumEventListenersForTesting(kEventName, context_a));
  EXPECT_EQ(1u,
            handler()->GetNumEventListenersForTesting(kEventName, context_b));

  // Dispatch the event in context_a - the listener in context_b should not be
  // notified.
  std::unique_ptr<base::ListValue> arguments_a =
      ListValueFromString("['result_a:']");
  ASSERT_TRUE(arguments_a);

  handler()->FireEventInContext(kEventName, context_a, *arguments_a,
                                EventFilteringInfo());
  {
    EXPECT_EQ("\"result_a:alpha\"",
              GetStringPropertyFromObject(context_a->Global(), context_a,
                                          "eventArgs"));
  }
  {
    EXPECT_EQ("undefined", GetStringPropertyFromObject(context_b->Global(),
                                                       context_b, "eventArgs"));
  }

  // Dispatch the event in context_b - the listener in context_a should not be
  // notified.
  std::unique_ptr<base::ListValue> arguments_b =
      ListValueFromString("['result_b:']");
  ASSERT_TRUE(arguments_b);
  handler()->FireEventInContext(kEventName, context_b, *arguments_b,
                                EventFilteringInfo());
  {
    EXPECT_EQ("\"result_a:alpha\"",
              GetStringPropertyFromObject(context_a->Global(), context_a,
                                          "eventArgs"));
  }
  {
    EXPECT_EQ("\"result_b:beta\"",
              GetStringPropertyFromObject(context_b->Global(), context_b,
                                          "eventArgs"));
  }
}

TEST_F(APIEventHandlerTest, DifferentCallingMethods) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  const char kEventName[] = "alpha";
  v8::Local<v8::Object> event =
      handler()->CreateEventInstance(kEventName, false, context);
  ASSERT_FALSE(event.IsEmpty());

  const char kAddListenerOnNull[] =
      "(function(event) {\n"
      "  event.addListener.call(null, function() {});\n"
      "})";
  {
    v8::Local<v8::Value> args[] = {event};
    // TODO(devlin): This is the generic type error that gin throws. It's not
    // very descriptive, nor does it match the web (which would just say e.g.
    // "Illegal invocation"). Might be worth updating later.
    RunFunctionAndExpectError(
        FunctionFromString(context, kAddListenerOnNull),
        context, 1, args,
        "Uncaught TypeError: Error processing argument at index -1,"
        " conversion failure from undefined");
  }
  EXPECT_EQ(0u, handler()->GetNumEventListenersForTesting(kEventName, context));

  const char kAddListenerOnEvent[] =
      "(function(event) {\n"
      "  event.addListener.call(event, function() {});\n"
      "})";
  {
    v8::Local<v8::Value> args[] = {event};
    RunFunction(FunctionFromString(context, kAddListenerOnEvent),
                context, 1, args);
  }
  EXPECT_EQ(1u, handler()->GetNumEventListenersForTesting(kEventName, context));

  // Call addListener with a function that captures the event, creating a cycle.
  // If we don't properly clean up, the context will leak.
  const char kAddListenerOnEventWithCapture[] =
      "(function(event) {\n"
      "  event.addListener(function listener() {\n"
      "    event.hasListener(listener);\n"
      "  });\n"
      "})";
  {
    v8::Local<v8::Value> args[] = {event};
    RunFunction(FunctionFromString(context, kAddListenerOnEventWithCapture),
                context, 1, args);
  }
  EXPECT_EQ(2u, handler()->GetNumEventListenersForTesting(kEventName, context));
}

TEST_F(APIEventHandlerTest, TestDispatchFromJs) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  v8::Local<v8::Object> event =
      handler()->CreateEventInstance("alpha", false, context);
  ASSERT_FALSE(event.IsEmpty());

  const char kListenerFunction[] =
      "(function() {\n"
      "  this.eventArgs = Array.from(arguments);\n"
      "});";
  v8::Local<v8::Function> listener =
      FunctionFromString(context, kListenerFunction);

  const char kAddListenerFunction[] =
      "(function(event, listener) { event.addListener(listener); })";
  v8::Local<v8::Function> add_listener_function =
      FunctionFromString(context, kAddListenerFunction);

  {
    v8::Local<v8::Value> argv[] = {event, listener};
    RunFunctionOnGlobal(add_listener_function, context, arraysize(argv), argv);
  }

  v8::Local<v8::Function> fire_event_function =
      FunctionFromString(
          context,
          "(function(event) { event.dispatch(42, 'foo', {bar: 'baz'}); })");
  {
    v8::Local<v8::Value> argv[] = {event};
    RunFunctionOnGlobal(fire_event_function, context, arraysize(argv), argv);
  }

  EXPECT_EQ("[42,\"foo\",{\"bar\":\"baz\"}]",
            GetStringPropertyFromObject(
                context->Global(), context, "eventArgs"));
}

// Test listeners that remove themselves in their handling of the event.
TEST_F(APIEventHandlerTest, RemovingListenersWhileHandlingEvent) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  const char kEventName[] = "alpha";
  v8::Local<v8::Object> event =
      handler()->CreateEventInstance(kEventName, false, context);
  ASSERT_FALSE(event.IsEmpty());
  {
    // Cache the event object on the global in order to allow for easy removal.
    v8::Local<v8::Function> set_event_on_global =
        FunctionFromString(
            context,
           "(function(event) { this.testEvent = event; })");
    v8::Local<v8::Value> args[] = {event};
    RunFunctionOnGlobal(set_event_on_global, context, arraysize(args), args);
    EXPECT_EQ(event,
              GetPropertyFromObject(context->Global(), context, "testEvent"));
  }

  // A listener function that removes itself as a listener.
  const char kListenerFunction[] =
      "(function() {\n"
      "  return function listener() {\n"
      "    this.testEvent.removeListener(listener);\n"
      "  };\n"
      "})();";

  // Create and add a bunch of listeners.
  std::vector<v8::Local<v8::Function>> listeners;
  const size_t kNumListeners = 20u;
  listeners.reserve(kNumListeners);
  for (size_t i = 0; i < kNumListeners; ++i)
    listeners.push_back(FunctionFromString(context, kListenerFunction));

  const char kAddListenerFunction[] =
      "(function(event, listener) { event.addListener(listener); })";
  v8::Local<v8::Function> add_listener_function =
      FunctionFromString(context, kAddListenerFunction);

  for (const auto& listener : listeners) {
    v8::Local<v8::Value> argv[] = {event, listener};
    RunFunctionOnGlobal(add_listener_function, context, arraysize(argv), argv);
  }

  // Fire the event. All listeners should be removed (and we shouldn't crash).
  EXPECT_EQ(kNumListeners,
            handler()->GetNumEventListenersForTesting(kEventName, context));
  handler()->FireEventInContext(kEventName, context, base::ListValue(),
                                EventFilteringInfo());
  EXPECT_EQ(0u, handler()->GetNumEventListenersForTesting(kEventName, context));

  // TODO(devlin): Another possible test: register listener a and listener b,
  // where a removes b and b removes a. Theoretically, only one should be
  // notified. Investigate what we currently do in JS-style bindings.
}

// Test an event listener throwing an exception.
TEST_F(APIEventHandlerTest, TestEventListenersThrowingExceptions) {
  // The default test util methods (RunFunction*) assume no errors will ever
  // be encountered. Instead, use an implementation that allows errors.
  auto run_js_and_expect_error = [](v8::Local<v8::Function> function,
                                    v8::Local<v8::Context> context, int argc,
                                    v8::Local<v8::Value> argv[]) {
    v8::MaybeLocal<v8::Value> maybe_result =
        function->Call(context, context->Global(), argc, argv);
    v8::Global<v8::Value> result;
    v8::Local<v8::Value> local;
    if (maybe_result.ToLocal(&local))
      result.Reset(context->GetIsolate(), local);
  };

  SetHandler(base::MakeUnique<APIEventHandler>(
      base::Bind(run_js_and_expect_error),
      base::Bind(&DoNothingOnEventListenersChanged)));

  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  const char kEventName[] = "alpha";
  v8::Local<v8::Object> event =
      handler()->CreateEventInstance(kEventName, false, context);
  ASSERT_FALSE(event.IsEmpty());

  bool did_throw = false;
  auto message_listener = [](v8::Local<v8::Message> message,
                             v8::Local<v8::Value> data) {
    ASSERT_FALSE(data.IsEmpty());
    ASSERT_TRUE(data->IsExternal());
    bool* did_throw = static_cast<bool*>(data.As<v8::External>()->Value());
    *did_throw = true;
    EXPECT_EQ("Uncaught Error: Event handler error",
              gin::V8ToString(message->Get()));
  };

  isolate()->AddMessageListener(message_listener,
                                v8::External::New(isolate(), &did_throw));
  base::ScopedClosureRunner remove_message_listener(base::Bind(
      [](v8::Isolate* isolate, v8::MessageCallback listener) {
        isolate->RemoveMessageListeners(listener);
      },
      isolate(), message_listener));

  // A listener that will throw an exception. We guarantee that we throw the
  // exception first so that we don't rely on event listener ordering.
  const char kListenerFunction[] =
      "(function() {\n"
      "  if (!this.didThrow) {\n"
      "    this.didThrow = true;\n"
      "    throw new Error('Event handler error');\n"
      "  }\n"
      "  this.eventArgs = Array.from(arguments);\n"
      "});";

  const char kAddListenerFunction[] =
      "(function(event, listener) { event.addListener(listener); })";
  v8::Local<v8::Function> add_listener_function =
      FunctionFromString(context, kAddListenerFunction);

  for (int i = 0; i < 2; ++i) {
    v8::Local<v8::Function> listener =
        FunctionFromString(context, kListenerFunction);
    v8::Local<v8::Value> argv[] = {event, listener};
    RunFunctionOnGlobal(add_listener_function, context, arraysize(argv), argv);
  }
  EXPECT_EQ(2u, handler()->GetNumEventListenersForTesting(kEventName, context));

  std::unique_ptr<base::ListValue> event_args = ListValueFromString("[42]");
  ASSERT_TRUE(event_args);
  handler()->FireEventInContext(kEventName, context, *event_args,
                                EventFilteringInfo());

  // An exception should have been thrown by the first listener and the second
  // listener should have recorded the event arguments.
  EXPECT_EQ("true", GetStringPropertyFromObject(context->Global(), context,
                                                "didThrow"));
  EXPECT_EQ("[42]", GetStringPropertyFromObject(context->Global(), context,
                                                "eventArgs"));
  EXPECT_TRUE(did_throw);
}

// Tests being notified as listeners are added or removed from events.
TEST_F(APIEventHandlerTest, CallbackNotifications) {
  MockEventChangeHandler change_handler;
  SetHandler(base::MakeUnique<APIEventHandler>(
      base::Bind(&RunFunctionOnGlobalAndIgnoreResult), change_handler.Get()));

  v8::HandleScope handle_scope(isolate());

  v8::Local<v8::Context> context_a = MainContext();
  v8::Local<v8::Context> context_b = AddContext();

  const char kEventName1[] = "onFoo";
  const char kEventName2[] = "onBar";
  v8::Local<v8::Object> event1_a =
      handler()->CreateEventInstance(kEventName1, false, context_a);
  ASSERT_FALSE(event1_a.IsEmpty());
  v8::Local<v8::Object> event2_a =
      handler()->CreateEventInstance(kEventName2, false, context_a);
  ASSERT_FALSE(event2_a.IsEmpty());
  v8::Local<v8::Object> event1_b =
      handler()->CreateEventInstance(kEventName1, false, context_b);
  ASSERT_FALSE(event1_b.IsEmpty());

  const char kAddListenerFunction[] =
      "(function(event, listener) { event.addListener(listener); })";
  const char kRemoveListenerFunction[] =
      "(function(event, listener) { event.removeListener(listener); })";

  // Add a listener to the first event. The APIEventHandler should notify
  // since it's a change in state (no listeners -> listeners).
  v8::Local<v8::Function> add_listener =
      FunctionFromString(context_a, kAddListenerFunction);
  v8::Local<v8::Function> listener1 =
      FunctionFromString(context_a, "(function() {})");
  {
    EXPECT_CALL(change_handler,
                Run(kEventName1, binding::EventListenersChanged::HAS_LISTENERS,
                    nullptr, true, context_a))
        .Times(1);
    v8::Local<v8::Value> argv[] = {event1_a, listener1};
    RunFunction(add_listener, context_a, arraysize(argv), argv);
    ::testing::Mock::VerifyAndClearExpectations(&change_handler);
  }
  EXPECT_EQ(1u,
            handler()->GetNumEventListenersForTesting(kEventName1, context_a));

  // Add a second listener to the same event. We should not be notified, since
  // the event already had listeners.
  v8::Local<v8::Function> listener2 =
      FunctionFromString(context_a, "(function() {})");
  {
    v8::Local<v8::Value> argv[] = {event1_a, listener2};
    RunFunction(add_listener, context_a, arraysize(argv), argv);
  }
  EXPECT_EQ(2u,
            handler()->GetNumEventListenersForTesting(kEventName1, context_a));

  // Remove the first listener of the event. Again, since the event has
  // listeners, we shouldn't be notified.
  v8::Local<v8::Function> remove_listener =
      FunctionFromString(context_a, kRemoveListenerFunction);
  {
    v8::Local<v8::Value> argv[] = {event1_a, listener1};
    RunFunction(remove_listener, context_a, arraysize(argv), argv);
  }

  EXPECT_EQ(1u,
            handler()->GetNumEventListenersForTesting(kEventName1, context_a));

  // Remove the final listener from the event. We should be notified that the
  // event no longer has listeners.
  {
    EXPECT_CALL(change_handler,
                Run(kEventName1, binding::EventListenersChanged::NO_LISTENERS,
                    nullptr, true, context_a))
        .Times(1);
    v8::Local<v8::Value> argv[] = {event1_a, listener2};
    RunFunction(remove_listener, context_a, arraysize(argv), argv);
    ::testing::Mock::VerifyAndClearExpectations(&change_handler);
  }
  EXPECT_EQ(0u,
            handler()->GetNumEventListenersForTesting(kEventName1, context_a));

  // Add a listener to a separate event to ensure we receive the right
  // notifications.
  v8::Local<v8::Function> listener3 =
      FunctionFromString(context_a, "(function() {})");
  {
    EXPECT_CALL(change_handler,
                Run(kEventName2, binding::EventListenersChanged::HAS_LISTENERS,
                    nullptr, true, context_a))
        .Times(1);
    v8::Local<v8::Value> argv[] = {event2_a, listener3};
    RunFunction(add_listener, context_a, arraysize(argv), argv);
    ::testing::Mock::VerifyAndClearExpectations(&change_handler);
  }
  EXPECT_EQ(1u,
            handler()->GetNumEventListenersForTesting(kEventName2, context_a));

  {
    EXPECT_CALL(change_handler,
                Run(kEventName1, binding::EventListenersChanged::HAS_LISTENERS,
                    nullptr, true, context_b))
        .Times(1);
    // And add a listener to an event in a different context to make sure the
    // associated context is correct.
    v8::Local<v8::Function> add_listener =
        FunctionFromString(context_b, kAddListenerFunction);
    v8::Local<v8::Function> listener =
        FunctionFromString(context_b, "(function() {})");
    v8::Local<v8::Value> argv[] = {event1_b, listener};
    RunFunction(add_listener, context_b, arraysize(argv), argv);
    ::testing::Mock::VerifyAndClearExpectations(&change_handler);
  }
  EXPECT_EQ(1u,
            handler()->GetNumEventListenersForTesting(kEventName1, context_b));

  // When the contexts are invalidated, we should receive listener removed
  // notifications. Additionally, since this was the context being torn down,
  // rather than a removeListener call, was_manual should be false.
  EXPECT_CALL(change_handler,
              Run(kEventName2, binding::EventListenersChanged::NO_LISTENERS,
                  nullptr, false, context_a))
      .Times(1);
  DisposeContext(context_a);
  ::testing::Mock::VerifyAndClearExpectations(&change_handler);

  EXPECT_CALL(change_handler,
              Run(kEventName1, binding::EventListenersChanged::NO_LISTENERS,
                  nullptr, false, context_b))
      .Times(1);
  DisposeContext(context_b);
  ::testing::Mock::VerifyAndClearExpectations(&change_handler);
}

// Test registering an argument massager for a given event.
TEST_F(APIEventHandlerTest, TestArgumentMassagers) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  const char kEventName[] = "alpha";
  v8::Local<v8::Object> event =
      handler()->CreateEventInstance(kEventName, false, context);
  ASSERT_FALSE(event.IsEmpty());

  const char kArgumentMassager[] =
      "(function(originalArgs, dispatch) {\n"
      "  this.originalArgs = originalArgs;\n"
      "  dispatch(['primary', 'secondary']);\n"
      "});";
  v8::Local<v8::Function> massager =
      FunctionFromString(context, kArgumentMassager);
  handler()->RegisterArgumentMassager(context, "alpha", massager);

  const char kListenerFunction[] =
      "(function() { this.eventArgs = Array.from(arguments); })";
  v8::Local<v8::Function> listener_function =
      FunctionFromString(context, kListenerFunction);
  ASSERT_FALSE(listener_function.IsEmpty());

  {
    const char kAddListenerFunction[] =
        "(function(event, listener) { event.addListener(listener); })";
    v8::Local<v8::Function> add_listener_function =
        FunctionFromString(context, kAddListenerFunction);
    v8::Local<v8::Value> argv[] = {event, listener_function};
    RunFunction(add_listener_function, context, arraysize(argv), argv);
  }

  const char kArguments[] = "['first','second']";
  std::unique_ptr<base::ListValue> event_args = ListValueFromString(kArguments);
  ASSERT_TRUE(event_args);
  handler()->FireEventInContext(kEventName, context, *event_args,
                                EventFilteringInfo());

  EXPECT_EQ(
      "[\"first\",\"second\"]",
      GetStringPropertyFromObject(context->Global(), context, "originalArgs"));
  EXPECT_EQ(
      "[\"primary\",\"secondary\"]",
      GetStringPropertyFromObject(context->Global(), context, "eventArgs"));
}

// Test registering an argument massager for a given event and dispatching
// asynchronously.
TEST_F(APIEventHandlerTest, TestArgumentMassagersAsyncDispatch) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  const char kEventName[] = "alpha";
  v8::Local<v8::Object> event =
      handler()->CreateEventInstance(kEventName, false, context);
  ASSERT_FALSE(event.IsEmpty());

  const char kArgumentMassager[] =
      "(function(originalArgs, dispatch) {\n"
      "  this.originalArgs = originalArgs;\n"
      "  this.dispatch = dispatch;\n"
      "});";
  v8::Local<v8::Function> massager =
      FunctionFromString(context, kArgumentMassager);
  handler()->RegisterArgumentMassager(context, "alpha", massager);

  const char kListenerFunction[] =
      "(function() { this.eventArgs = Array.from(arguments); })";
  v8::Local<v8::Function> listener_function =
      FunctionFromString(context, kListenerFunction);
  ASSERT_FALSE(listener_function.IsEmpty());

  {
    const char kAddListenerFunction[] =
        "(function(event, listener) { event.addListener(listener); })";
    v8::Local<v8::Function> add_listener_function =
        FunctionFromString(context, kAddListenerFunction);
    v8::Local<v8::Value> argv[] = {event, listener_function};
    RunFunction(add_listener_function, context, arraysize(argv), argv);
  }

  const char kArguments[] = "['first','second']";
  std::unique_ptr<base::ListValue> event_args = ListValueFromString(kArguments);
  ASSERT_TRUE(event_args);
  handler()->FireEventInContext(kEventName, context, *event_args,
                                EventFilteringInfo());

  // The massager should have been triggered, but since it doesn't call
  // dispatch(), the listener shouldn't have been notified.
  EXPECT_EQ(
      "[\"first\",\"second\"]",
      GetStringPropertyFromObject(context->Global(), context, "originalArgs"));
  EXPECT_EQ("undefined", GetStringPropertyFromObject(context->Global(), context,
                                                     "eventArgs"));

  // Dispatch the event.
  v8::Local<v8::Value> dispatch_value =
      GetPropertyFromObject(context->Global(), context, "dispatch");
  ASSERT_FALSE(dispatch_value.IsEmpty());
  ASSERT_TRUE(dispatch_value->IsFunction());
  v8::Local<v8::Value> dispatch_args[] = {
      V8ValueFromScriptSource(context, "['primary', 'secondary']"),
  };
  RunFunction(dispatch_value.As<v8::Function>(), context,
              arraysize(dispatch_args), dispatch_args);

  EXPECT_EQ(
      "[\"primary\",\"secondary\"]",
      GetStringPropertyFromObject(context->Global(), context, "eventArgs"));
}

// Test registering an argument massager and never dispatching.
TEST_F(APIEventHandlerTest, TestArgumentMassagersNeverDispatch) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  const char kEventName[] = "alpha";
  v8::Local<v8::Object> event =
      handler()->CreateEventInstance(kEventName, false, context);
  ASSERT_FALSE(event.IsEmpty());

  // A massager that never dispatches.
  const char kArgumentMassager[] = "(function(originalArgs, dispatch) {})";
  v8::Local<v8::Function> massager =
      FunctionFromString(context, kArgumentMassager);
  handler()->RegisterArgumentMassager(context, "alpha", massager);

  const char kListenerFunction[] = "(function() {})";
  v8::Local<v8::Function> listener_function =
      FunctionFromString(context, kListenerFunction);
  ASSERT_FALSE(listener_function.IsEmpty());

  const char kAddListenerFunction[] =
      "(function(event, listener) { event.addListener(listener); })";
  v8::Local<v8::Function> add_listener_function =
      FunctionFromString(context, kAddListenerFunction);
  v8::Local<v8::Value> argv[] = {event, listener_function};
  RunFunction(add_listener_function, context, arraysize(argv), argv);

  handler()->FireEventInContext(kEventName, context, base::ListValue(),
                                EventFilteringInfo());

  // Nothing should blow up. (We tested in the previous test that the event
  // isn't notified without calling dispatch, so all there is to test here is
  // that we don't crash.)
}

// Test creating a custom event, as is done by a few of our custom bindings.
TEST_F(APIEventHandlerTest, TestCreateCustomEvent) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  MockEventChangeHandler change_handler;
  APIEventHandler handler(base::Bind(&RunFunctionOnGlobalAndIgnoreResult),
                          change_handler.Get());

  v8::Local<v8::Object> event = handler.CreateAnonymousEventInstance(context);
  ASSERT_FALSE(event.IsEmpty());

  const char kAddListenerFunction[] =
      "(function(event) {\n"
      "  event.addListener(function() {\n"
      "    this.eventArgs = Array.from(arguments);\n"
      "  });\n"
      "})";
  v8::Local<v8::Value> add_listener_argv[] = {event};
  RunFunction(FunctionFromString(context, kAddListenerFunction), context,
              arraysize(add_listener_argv), add_listener_argv);

  // Test dispatching to the listeners.
  const char kDispatchEventFunction[] =
      "(function(event) { event.dispatch(1, 2, 3); })";
  v8::Local<v8::Function> dispatch_function =
      FunctionFromString(context, kDispatchEventFunction);

  v8::Local<v8::Value> dispatch_argv[] = {event};
  RunFunction(dispatch_function, context, arraysize(dispatch_argv),
              dispatch_argv);

  EXPECT_EQ("[1,2,3]", GetStringPropertyFromObject(context->Global(), context,
                                                   "eventArgs"));

  // Clean up so we can re-check eventArgs.
  ASSERT_TRUE(context->Global()
                  ->Delete(context, gin::StringToSymbol(isolate(), "eventArgs"))
                  .FromJust());

  // Invalidate the event and try dispatching again. Nothing should happen.
  handler.InvalidateCustomEvent(context, event);
  RunFunction(dispatch_function, context, arraysize(dispatch_argv),
              dispatch_argv);
  EXPECT_EQ("undefined", GetStringPropertyFromObject(context->Global(), context,
                                                     "eventArgs"));
}

// Test adding a custom event with a cyclic dependency. Nothing should leak.
TEST_F(APIEventHandlerTest, TestCreateCustomEventWithCyclicDependency) {
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Context> context = MainContext();

  MockEventChangeHandler change_handler;
  APIEventHandler handler(base::Bind(&RunFunctionOnGlobalAndIgnoreResult),
                          change_handler.Get());

  v8::Local<v8::Object> event = handler.CreateAnonymousEventInstance(context);
  ASSERT_FALSE(event.IsEmpty());

  const char kAddListenerFunction[] =
      "(function(event) {\n"
      "  event.addListener(function() {}.bind(null, event));\n"
      "})";
  v8::Local<v8::Value> add_listener_argv[] = {event};
  RunFunction(FunctionFromString(context, kAddListenerFunction), context,
              arraysize(add_listener_argv), add_listener_argv);

  DisposeContext(context);
}

}  // namespace extensions
