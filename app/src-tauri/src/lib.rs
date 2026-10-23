use std::path::PathBuf;

use serde::Serialize;
use tauri::Manager;
use tauri_plugin_fs::FsExt;

/// Settings from the environment, read once at startup:
/// - `IRSENSOR_DATA_DIR`: where recordings are kept (default `~/IRSensor`, spec §10.4)
/// - `IRSENSOR_DEVICE_URL`: device base URL to use instead of the saved one,
///   e.g. `http://127.0.0.1:8080` for the mock
#[derive(Serialize, Clone)]
struct StartupConfig {
    data_dir: String,
    device_url: Option<String>,
}

#[tauri::command]
fn startup_config(config: tauri::State<StartupConfig>) -> StartupConfig {
    config.inner().clone()
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_http::init())
        .setup(|app| {
            let dir = match std::env::var_os("IRSENSOR_DATA_DIR") {
                Some(d) => PathBuf::from(d),
                None => app.path().home_dir()?.join("IRSensor"),
            };
            // The static fs scope covers ~/IRSensor; this also admits an override
            app.fs_scope().allow_directory(&dir, true)?;
            app.manage(StartupConfig {
                data_dir: dir.to_string_lossy().into_owned(),
                device_url: std::env::var("IRSENSOR_DEVICE_URL").ok().filter(|u| !u.is_empty()),
            });
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![startup_config])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
