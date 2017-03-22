// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/public/interfaces/constants.mojom.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/run_loop.h"
#include "mash/quick_launch/public/interfaces/constants.mojom.h"
#include "services/service_manager/public/cpp/service_test.h"
#include "services/ui/public/interfaces/constants.mojom.h"
#include "services/ui/public/interfaces/window_server_test.mojom.h"

namespace ash {
namespace mus {

void RunCallback(uint64_t* root_window_count,
                 const base::Closure& callback,
                 uint64_t result) {
  *root_window_count = result;
  callback.Run();
}

class AppLaunchTest : public service_manager::test::ServiceTest {
 public:
  AppLaunchTest() : ServiceTest("mash_unittests") {}
  ~AppLaunchTest() override {}

 private:
  void SetUp() override {
    base::CommandLine::ForCurrentProcess()->AppendSwitch("use-test-config");
    ServiceTest::SetUp();
  }

  DISALLOW_COPY_AND_ASSIGN(AppLaunchTest);
};

TEST_F(AppLaunchTest, TestQuickLaunch) {
  connector()->Connect(mojom::kServiceName);
  connector()->Connect(mash::quick_launch::mojom::kServiceName);

  ui::mojom::WindowServerTestPtr test_interface;
  connector()->BindInterface(ui::mojom::kServiceName, &test_interface);

  base::RunLoop run_loop;
  uint64_t root_window_count = 0;
  test_interface->EnsureClientHasDrawnRootWindows(
      mash::quick_launch::mojom::kServiceName,
      base::Bind(&RunCallback, &root_window_count, run_loop.QuitClosure()));
  run_loop.Run();
  EXPECT_EQ(1u, root_window_count);
}

}  // namespace mus
}  // namespace ash
