// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender},
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    futures::lock::Mutex,
    futures::prelude::*,
    std::sync::{Arc, RwLock},
};

// TODO(go/fxb/35873): Add AudioInput support.
impl Sender<AudioSettings> for AudioWatchResponder {
    fn send_response(self, data: AudioSettings) {
        self.send(&mut Ok(data)).unwrap();
    }
}

impl From<SettingResponse> for AudioSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Audio(info) = response {
            let mut audio_settings = AudioSettings::empty();
            let mut streams = Vec::new();
            for stream in info.streams {
                streams.push(AudioStreamSettings::from(stream));
            }

            audio_settings.streams = Some(streams);
            audio_settings
        } else {
            panic!("incorrect value sent to audio");
        }
    }
}

impl From<AudioStream> for AudioStreamSettings {
    fn from(stream: AudioStream) -> Self {
        AudioStreamSettings {
            stream: Some(AudioRenderUsage::from(stream.stream_type)),
            source: Some(AudioStreamSettingSource::from(stream.source)),
            user_volume: Some(Volume {
                level: Some(stream.user_volume_level),
                muted: Some(stream.user_volume_muted),
            }),
        }
    }
}

impl From<AudioRenderUsage> for AudioStreamType {
    fn from(usage: AudioRenderUsage) -> Self {
        match usage {
            AudioRenderUsage::Background => AudioStreamType::Background,
            AudioRenderUsage::Media => AudioStreamType::Media,
            AudioRenderUsage::Interruption => AudioStreamType::Interruption,
            AudioRenderUsage::SystemAgent => AudioStreamType::SystemAgent,
            AudioRenderUsage::Communication => AudioStreamType::Communication,
        }
    }
}

impl From<AudioStreamType> for AudioRenderUsage {
    fn from(usage: AudioStreamType) -> Self {
        match usage {
            AudioStreamType::Background => AudioRenderUsage::Background,
            AudioStreamType::Media => AudioRenderUsage::Media,
            AudioStreamType::Interruption => AudioRenderUsage::Interruption,
            AudioStreamType::SystemAgent => AudioRenderUsage::SystemAgent,
            AudioStreamType::Communication => AudioRenderUsage::Communication,
        }
    }
}

impl From<AudioStreamSettingSource> for AudioSettingSource {
    fn from(source: AudioStreamSettingSource) -> Self {
        match source {
            AudioStreamSettingSource::Default => AudioSettingSource::Default,
            AudioStreamSettingSource::User => AudioSettingSource::User,
            AudioStreamSettingSource::System => AudioSettingSource::System,
        }
    }
}

impl From<AudioSettingSource> for AudioStreamSettingSource {
    fn from(source: AudioSettingSource) -> Self {
        match source {
            AudioSettingSource::Default => AudioStreamSettingSource::Default,
            AudioSettingSource::User => AudioStreamSettingSource::User,
            AudioSettingSource::System => AudioStreamSettingSource::System,
        }
    }
}

fn to_request(settings: AudioSettings) -> Option<SettingRequest> {
    let mut request = None;
    if let Some(streams_value) = settings.streams {
        let mut streams = Vec::new();

        for stream in streams_value {
            let user_volume = stream.user_volume.unwrap();

            streams.push(AudioStream {
                stream_type: AudioStreamType::from(stream.stream.unwrap()),
                source: AudioSettingSource::from(stream.source.unwrap()),
                user_volume_level: user_volume.level.unwrap(),
                user_volume_muted: user_volume.muted.unwrap(),
            });
        }

        request = Some(SettingRequest::SetVolume(streams));
    }
    request
}

pub fn spawn_audio_fidl_handler(
    switchboard_handle: Arc<RwLock<SwitchboardImpl>>,
    mut stream: AudioRequestStream,
) {
    let switchboard_lock = switchboard_handle.clone();

    let hanging_get_handler: Arc<Mutex<HangingGetHandler<AudioSettings, AudioWatchResponder>>> =
        HangingGetHandler::create(switchboard_handle, SettingType::Audio);

    fasync::spawn(async move {
        while let Ok(Some(req)) = stream.try_next().await {
            // Support future expansion of FIDL
            #[allow(unreachable_patterns)]
            match req {
                AudioRequest::Set { settings, responder } => {
                    if let Some(request) = to_request(settings) {
                        set_volume(switchboard_lock.clone(), request, responder)
                    } else {
                        responder.send(&mut Err(Error::Unsupported)).unwrap();
                    }
                }
                AudioRequest::Watch { responder } => {
                    let mut hanging_get_lock = hanging_get_handler.lock().await;
                    hanging_get_lock.watch(responder).await;
                }
                _ => {}
            }
        }
    });
}

fn set_volume(
    switchboard: Arc<RwLock<dyn Switchboard + Send + Sync>>,
    request: SettingRequest,
    responder: AudioSetResponder,
) {
    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    if switchboard.write().unwrap().request(SettingType::Audio, request, response_tx).is_ok() {
        fasync::spawn(async move {
            // Return success if we get a Ok result from the
            // switchboard.
            if let Ok(Ok(_)) = response_rx.await {
                responder.send(&mut Ok(())).ok();
            } else {
                responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
            }
        });
    } else {
        responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
    }
}