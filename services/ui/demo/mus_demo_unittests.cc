// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "services/service_manager/public/cpp/service_test.h"
#include "services/ui/public/interfaces/constants.mojom.h"
#include "services/ui/public/interfaces/window_server_test.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ui {
namespace demo {

namespace {

const char kTestAppName[] = "mus_demo_unittests";

void RunCallback(uint64_t* root_window_count,
                 const base::Closure& callback,
                 uint64_t result) {
  *root_window_count = result;
  callback.Run();
}

class MusDemoTest : public service_manager::test::ServiceTest {
 public:
  MusDemoTest() : service_manager::test::ServiceTest(kTestAppName) {}
  ~MusDemoTest() override {}

  void SetUp() override {
    base::CommandLine::ForCurrentProcess()->AppendSwitch("use-test-config");
    ServiceTest::SetUp();
  }

 protected:
  uint64_t StartDemoAndCountDrawnWindows() {
    connector()->Connect("mus_demo");

    ::ui::mojom::WindowServerTestPtr test_interface;
    connector()->BindInterface(ui::mojom::kServiceName, &test_interface);

    base::RunLoop run_loop;
    uint64_t root_window_count = 0;
    // TODO(kylechar): Fix WindowServer::CreateTreeForWindowManager so that the
    // WindowTree has the correct name instead of an empty name.
    // TODO(tonikitoo,fwang): Also fix the WindowTree name for MusDemoExternal.
    test_interface->EnsureClientHasDrawnRootWindows(
        "",  // WindowTree name is empty.
        base::Bind(&RunCallback, &root_window_count, run_loop.QuitClosure()));
    run_loop.Run();

    return root_window_count;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MusDemoTest);
};

}  // namespace

TEST_F(MusDemoTest, CheckMusDemoDraws) {
  EXPECT_EQ(1u, StartDemoAndCountDrawnWindows());
}

#if defined(USE_OZONE) && !defined(OS_CHROMEOS)
TEST_F(MusDemoTest, CheckMusDemoMultipleWindows) {
  uint64_t expected_root_window_count = 5;
  base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      "external-window-count", std::to_string(expected_root_window_count));
  EXPECT_EQ(expected_root_window_count, StartDemoAndCountDrawnWindows());
}
#endif  // defined(USE_OZONE) && !defined(OS_CHROMEOS)

}  // namespace demo
}  // namespace ui
