// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/privacy_settings_ptr.h"

#include <lib/fostr/fidl/fuchsia/settings/formatting.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/types.h>

#include "src/lib/fxl/logging.h"

namespace feedback {

PrivacySettingsWatcher::PrivacySettingsWatcher(std::shared_ptr<sys::ServiceDirectory> services,
                                               crashpad::Settings* crashpad_database_settings)
    : services_(services), crashpad_database_settings_(crashpad_database_settings) {}

void PrivacySettingsWatcher::StartWatching() {
  Connect();
  Watch();
}

void PrivacySettingsWatcher::Connect() {
  privacy_settings_ptr_ = services_->Connect<fuchsia::settings::Privacy>();
  privacy_settings_ptr_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.settings.Privacy";
    Reset();
    // TODO(fxb/6360): re-connect with exponential backoff.
  });
}

void PrivacySettingsWatcher::Watch() {
  privacy_settings_ptr_->Watch([this](fuchsia::settings::Privacy_Watch_Result result) {
    if (result.is_err()) {
      FX_LOGS(ERROR) << "Failed to obtain privacy settings: " << result.err();
      Reset();
    } else if (result.is_response()) {
      privacy_settings_ = std::move(result.response().settings);
      Update();
    }

    // We watch for the next update, following the hanging get pattern.
    Watch();
  });
}

void PrivacySettingsWatcher::Reset() {
  privacy_settings_.clear_user_data_sharing_consent();
  Update();
}

void PrivacySettingsWatcher::Update() {
  if (privacy_settings_.has_user_data_sharing_consent() &&
      privacy_settings_.user_data_sharing_consent()) {
    if (crashpad_database_settings_->SetUploadsEnabled(true)) {
      FX_LOGS(INFO) << "Enabled crash report upload";
    } else {
      FX_LOGS(ERROR) << "Failed to enable crash report upload";
    }
  } else {
    FXL_CHECK(crashpad_database_settings_->SetUploadsEnabled(false))
        << "Failed to disable crash report upload";
    FX_LOGS(INFO) << "Disabled crash report upload";
  }
}

}  // namespace feedback