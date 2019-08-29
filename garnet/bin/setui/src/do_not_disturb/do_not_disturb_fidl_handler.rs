use {
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender},
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    fidl_fuchsia_settings::{
        DoNotDisturbRequest, DoNotDisturbRequestStream, DoNotDisturbSettings,
        DoNotDisturbWatchResponder, Error,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
    futures::TryStreamExt,
    std::sync::{Arc, RwLock},
};

impl Sender<DoNotDisturbSettings> for DoNotDisturbWatchResponder {
    fn send_response(self, data: DoNotDisturbSettings) {
        match self.send(data) {
            Ok(_) => {},
            Err(e) => fx_log_err!("failed to send do_not_disturb, {:#?}", e),
        }
    }
}

impl From<SettingResponse> for DoNotDisturbSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::DoNotDisturb(info) = response {
            let mut dnd_settings = fidl_fuchsia_settings::DoNotDisturbSettings::empty();
            dnd_settings.user_initiated_do_not_disturb = Some(info.user_dnd);
            dnd_settings.night_mode_initiated_do_not_disturb = Some(info.night_mode_dnd);
            dnd_settings
        } else {
            panic!("incorrect value sent to do_not_disturb");
        }
    }
}

fn to_request(settings: DoNotDisturbSettings) -> Option<SettingRequest> {
    let mut request = None;
    if let Some(user_dnd) = settings.user_initiated_do_not_disturb {
        request = Some(SettingRequest::SetUserInitiatedDoNotDisturb(user_dnd));
    } else if let Some(night_mode_dnd) = settings.night_mode_initiated_do_not_disturb {
        request = Some(SettingRequest::SetNightModeInitiatedDoNotDisturb(night_mode_dnd));
    }
    request
}

pub fn spawn_do_not_disturb_fidl_handler(
    switchboard_handle: Arc<RwLock<SwitchboardImpl>>,
    mut stream: DoNotDisturbRequestStream,
) {
    let switchboard_lock = switchboard_handle.clone();

    type DNDHangingGetHandler = Arc<
        Mutex<HangingGetHandler<DoNotDisturbSettings, DoNotDisturbWatchResponder>>
    >;
    let hanging_get_handler: DNDHangingGetHandler =
        HangingGetHandler::create(switchboard_handle, SettingType::DoNotDisturb);

    fasync::spawn(async move {
        while let Ok(Some(req)) = stream.try_next().await {
            // Support future expansion of FIDL
            #[allow(unreachable_patterns)]
            match req {
                DoNotDisturbRequest::Set { settings, responder } => {
                    if let Some(request) = to_request(settings) {
                        let (response_tx, _response_rx) =
                            futures::channel::oneshot::channel::<SettingResponseResult>();
                        let mut switchboard = match switchboard_lock.write() {
                            Ok(lock) => lock,
                            Err(err) => {
                                panic!("failed to get switchboard write lock: {:#?}", err);
                            }
                        };
                        let result =
                            switchboard.request(SettingType::DoNotDisturb, request, response_tx);
                        match result {
                            Ok(_) => responder.send(&mut Ok(())).unwrap(),
                            Err(_err) => responder.send(&mut Err(Error::Unsupported)).unwrap(),
                        }
                    } else {
                        responder.send(&mut Err(Error::Unsupported)).unwrap();
                    }
                }
                DoNotDisturbRequest::Watch { responder } => {
                    let mut hanging_get_lock = hanging_get_handler.lock().await;
                    hanging_get_lock.watch(responder).await;
                }
                _ => {
                    fx_log_err!("Unsupported DoNotDisturbRequest type");
                }
            }
        }
    })
}