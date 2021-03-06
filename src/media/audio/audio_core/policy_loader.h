// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_POLICY_LOADER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_POLICY_LOADER_H_

#include <fuchsia/media/cpp/fidl.h>

#include <rapidjson/document.h>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/audio_policy.h"

namespace media {
namespace audio {

class PolicyLoader {
 public:
  static std::optional<AudioPolicy> LoadDefaultPolicy();

  static std::optional<AudioPolicy> ParseConfig(const char* file_body);

 private:
  static std::optional<AudioPolicy> LoadConfigFromFile(const std::string config);
};

}  // namespace audio
}  // namespace media

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_POLICY_LOADER_H_
