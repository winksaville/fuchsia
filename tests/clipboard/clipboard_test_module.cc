// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>

#include "lib/app/cpp/connect.h"
#include "lib/app_driver/cpp/module_driver.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/clipboard/defs.h"
#include "peridot/tests/common/defs.h"

using fuchsia::modular::testing::Signal;
using fuchsia::modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Clipboard module initialized"};
  TestPoint successful_peek_{"Clipboard pushed and peeked value"};

  TestApp(fuchsia::modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    fuchsia::modular::testing::Init(module_host->application_context(),
                                    __FILE__);
    initialized_.Pass();

    SetUp();

    const std::string expected_value = "hello there";
    clipboard_->Push(expected_value);
    clipboard_->Peek([this, expected_value](fidl::StringPtr text) {
      if (expected_value == text) {
        successful_peek_.Pass();
      }
      Signal(fuchsia::modular::testing::kTestShutdown);
    });
  }

  TestPoint stopped_{"Clipboard module stopped"};

  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    fuchsia::modular::testing::Done(done);
  }

 private:
  void SetUp() {
    module_host_->module_context()->GetComponentContext(
        component_context_.NewRequest());

    component::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kClipboardAgentUrl,
                                       agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    ConnectToService(agent_services.get(), clipboard_.NewRequest());
  }

  fuchsia::modular::ModuleHost* const module_host_;
  fuchsia::modular::AgentControllerPtr agent_controller_;
  fuchsia::modular::ClipboardPtr clipboard_;
  fuchsia::modular::ComponentContextPtr component_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  fuchsia::modular::ModuleDriver<TestApp> driver(app_context.get(),
                                                 [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
