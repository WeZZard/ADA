//! Multimodal capture commands.
//!
//! Provides a minimal MVP that records screen, voice, and ADA trace output
//! into an .adabundle directory for handoff to an AI agent.

use anyhow::{bail, Context};
use clap::Subcommand;
use serde::Serialize;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tracer_backend::TracerController;

use crate::session_state::{self, SessionState, SessionStatus};

#[derive(Subcommand)]
pub enum CaptureCommands {
    /// Start a multimodal capture session
    ///
    /// Session data is stored in ~/.ada/sessions/<session_id>/
    /// The session directory IS the bundle (contains manifest.json, trace/, etc.)
    Start {
        /// Path to the binary to trace
        binary: String,

        /// Disable screen recording (enabled by default)
        #[arg(long = "no-screen")]
        no_screen: bool,

        /// Disable voice recording (enabled by default)
        #[arg(long = "no-voice")]
        no_voice: bool,

        /// Include microphone audio in screen recording
        #[arg(long, default_value_t = false)]
        screen_audio: bool,

        /// Audio device spec for ffmpeg (avfoundation), e.g. ":0"
        #[arg(long)]
        audio_device: Option<String>,

        /// Detail pre-roll in ms (flight recorder)
        #[arg(long, default_value_t = 0)]
        pre_roll_ms: u32,

        /// Detail post-roll in ms (flight recorder)
        #[arg(long, default_value_t = 0)]
        post_roll_ms: u32,

        /// Arguments to pass to the binary
        #[arg(trailing_var_arg = true)]
        args: Vec<String>,
    },
}

// LCOV_EXCL_START - Entry point delegates to start_capture which requires live hardware
pub fn run(cmd: CaptureCommands) -> anyhow::Result<()> {
    match cmd {
        CaptureCommands::Start {
            binary,
            no_screen,
            no_voice,
            screen_audio,
            audio_device,
            pre_roll_ms,
            post_roll_ms,
            args,
        } => start_capture(
            &binary,
            !no_screen,
            !no_voice,
            screen_audio,
            audio_device.as_deref(),
            pre_roll_ms,
            post_roll_ms,
            &args,
        ),
    }
}
// LCOV_EXCL_STOP

#[derive(Serialize)]
struct BundleManifest {
    version: u32,
    created_at_ms: u64,
    finished_at_ms: u64,
    session_name: String,
    trace_root: String,
    trace_session: Option<String>,
    screen_path: Option<String>,
    voice_path: Option<String>,
    voice_lossless_path: Option<String>,
    voice_log_path: Option<String>,
    screen_log_path: Option<String>,
    detail_when_voice: bool,
}

struct RecorderChild {
    name: &'static str,
    child: Child,
    output: PathBuf,
}

// LCOV_EXCL_START - macOS app bundle resolution and agent path setup

/// Resolve a user-provided path to an executable.
///
/// Handles:
/// - `/path/to/App.app` -> `/path/to/App.app/Contents/MacOS/<CFBundleExecutable>`
/// - `/path/to/binary` -> `/path/to/binary` (unchanged)
fn resolve_executable_path(path: &str) -> anyhow::Result<String> {
    let p = Path::new(path);

    // Check if path ends with .app
    if p.extension().map(|e| e == "app").unwrap_or(false) {
        // It's an app bundle - resolve to internal executable
        let info_plist = p.join("Contents/Info.plist");

        if !info_plist.exists() {
            bail!(
                "App bundle missing Info.plist: {}\n\
                 Expected: {}/Contents/Info.plist",
                path,
                path
            );
        }

        // Read CFBundleExecutable from Info.plist
        let executable_name = read_plist_key(&info_plist, "CFBundleExecutable")
            .with_context(|| format!("Failed to read executable name from {}", info_plist.display()))?;

        let executable_path = p.join("Contents/MacOS").join(&executable_name);

        if !executable_path.exists() {
            bail!(
                "Executable not found in app bundle: {}\n\
                 Expected: {}",
                path,
                executable_path.display()
            );
        }

        Ok(executable_path.to_string_lossy().to_string())
    } else {
        // Not an app bundle - use as-is
        Ok(path.to_string())
    }
}

/// Read a key from Info.plist using PlistBuddy
fn read_plist_key(plist_path: &Path, key: &str) -> anyhow::Result<String> {
    let output = Command::new("/usr/libexec/PlistBuddy")
        .arg("-c")
        .arg(format!("Print :{}", key))
        .arg(plist_path)
        .output()
        .context("Failed to execute PlistBuddy")?;

    if !output.status.success() {
        bail!("PlistBuddy failed: {}", String::from_utf8_lossy(&output.stderr));
    }

    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

/// Ensure ADA_AGENT_RPATH_SEARCH_PATHS is set so the tracer can find libfrida_agent.dylib
fn ensure_agent_rpath() -> anyhow::Result<()> {
    if let Ok(existing) = std::env::var("ADA_AGENT_RPATH_SEARCH_PATHS") {
        if !existing.trim().is_empty() {
            return Ok(());
        }
    }

    let mut candidates = Vec::new();

    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            candidates.push(dir.to_path_buf());
            if let Some(target_root) = dir.parent() {
                candidates.push(target_root.join("tracer_backend/lib"));
                candidates.push(target_root.join("build"));
            }
        }
    }

    let target_dir = std::env::var("CARGO_TARGET_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("target"));
    let profile = if cfg!(debug_assertions) { "debug" } else { "release" };
    candidates.push(target_dir.join(profile).join("tracer_backend/lib"));

    let mut search_paths = Vec::new();
    #[cfg(target_os = "macos")]
    let lib_name = "libfrida_agent.dylib";
    #[cfg(not(target_os = "macos"))]
    let lib_name = "libfrida_agent.so";

    for candidate in candidates {
        let lib_path = candidate.join(lib_name);
        if lib_path.exists() {
            search_paths.push(candidate);
        }
    }

    if search_paths.is_empty() {
        bail!("libfrida_agent.dylib not found; set ADA_AGENT_RPATH_SEARCH_PATHS");
    }

    let joined = search_paths
        .iter()
        .map(|path| path.to_string_lossy())
        .collect::<Vec<_>>()
        .join(":");
    std::env::set_var("ADA_AGENT_RPATH_SEARCH_PATHS", joined);
    Ok(())
}

// LCOV_EXCL_STOP

// LCOV_EXCL_START - Integration code requires live tracer and capture hardware

fn start_capture(
    binary: &str,
    screen: bool,
    voice: bool,
    screen_audio: bool,
    audio_device: Option<&str>,
    pre_roll_ms: u32,
    post_roll_ms: u32,
    args: &[String],
) -> anyhow::Result<()> {
    // Clean up any orphaned sessions first
    if let Err(e) = session_state::cleanup_orphaned() {
        tracing::warn!("Failed to cleanup orphaned sessions: {}", e);
    }

    // Ensure agent library can be found
    ensure_agent_rpath()?;

    // Resolve .app bundle to executable path
    let binary = resolve_executable_path(binary)?;
    let binary = binary.as_str();

    let now_ms = current_time_ms();
    let app_info = session_state::extract_app_info(binary);
    let session_id = session_state::generate_session_id(&app_info.name);

    // Session directory IS the bundle directory
    let bundle_dir = session_state::session_dir(&session_id)?;
    let trace_root = bundle_dir.join("trace");
    let session_name = session_id.clone();

    fs::create_dir_all(&trace_root)
        .with_context(|| format!("Failed to create trace directory at {}", trace_root.display()))?;

    // Register session state
    let session = SessionState {
        session_id: session_id.clone(),
        session_path: bundle_dir.clone(),
        start_time: chrono::Utc::now().to_rfc3339(),
        end_time: None,
        app_info: app_info.clone(),
        status: SessionStatus::Running,
        pid: None, // Will be set after spawn
        capture_pid: Some(std::process::id()),
    };

    if let Err(e) = session_state::register(&session) {
        tracing::warn!("Failed to register session state: {}", e);
    }

    // Output session info for Claude context
    println!("ADA Session Started:");
    println!("  ID: {}", session_id);
    println!(
        "  App: {} ({})",
        app_info.name,
        app_info.bundle_id.as_deref().unwrap_or("no bundle id")
    );
    println!("  Binary: {}", binary);
    println!("  Bundle: {}", bundle_dir.display());
    println!("  Time: {}", session.start_time);
    let mut controller = map_tracer_result(TracerController::new(&trace_root))?;

    let mut spawn_args = vec![binary.to_string()];
    spawn_args.extend_from_slice(args);
    let pid = map_tracer_result(controller.spawn_suspended(binary, &spawn_args))?;

    // Update session with target PID
    if let Ok(Some(mut session)) = session_state::get(&session_id) {
        session.pid = Some(pid);
        let _ = session_state::update(&session_id, &session);
    }

    map_tracer_result(controller.attach(pid))?;
    map_tracer_result(controller.install_hooks())?;

    if voice {
        map_tracer_result(controller.arm_trigger(pre_roll_ms, post_roll_ms))?;
        map_tracer_result(controller.fire_trigger())?;
    }

    map_tracer_result(controller.set_detail_enabled(voice))?;
    map_tracer_result(controller.resume())?;

    let mut screen_recorder = None;
    if screen {
        screen_recorder = Some(start_screen_recording(&bundle_dir, screen_audio)?);
    }

    let mut voice_recorder = None;
    if voice {
        voice_recorder = Some(start_voice_recording(&bundle_dir, audio_device)?);
    }

    println!("Capture running. Press Ctrl+C to stop.");

    let running = Arc::new(AtomicBool::new(true));
    let running_flag = running.clone();
    ctrlc::set_handler(move || {
        running_flag.store(false, Ordering::SeqCst);
    })?;

    while running.load(Ordering::SeqCst) {
        thread::sleep(Duration::from_millis(200));
    }

    if let Some(recorder) = voice_recorder.as_mut() {
        stop_recorder(recorder)?;
        let _ = map_tracer_result(controller.disarm_trigger());
        let _ = map_tracer_result(controller.set_detail_enabled(false));
        let _ = encode_voice_to_aac(&bundle_dir)?;
    }

    if let Some(recorder) = screen_recorder.as_mut() {
        stop_recorder(recorder)?;
    }

    if let Err(err) = controller.detach() {
        eprintln!("Warning: failed to detach tracer ({err})");
    }
    drop(controller);

    let finished_at_ms = current_time_ms();
    let trace_session = find_latest_trace_session(&trace_root);

    fs::create_dir_all(&bundle_dir)
        .with_context(|| format!("Failed to create bundle directory at {}", bundle_dir.display()))?;

    let manifest = BundleManifest {
        version: 1,
        created_at_ms: now_ms,
        finished_at_ms,
        session_name,
        trace_root: path_as_string(&bundle_dir, &trace_root),
        trace_session: trace_session
            .as_ref()
            .map(|path| path_as_string(&bundle_dir, path)),
        screen_path: screen_recorder
            .as_ref()
            .map(|recorder| path_as_string(&bundle_dir, &recorder.output)),
        voice_path: if voice_recorder.is_some() {
            Some(path_as_string(&bundle_dir, &bundle_dir.join("voice.m4a")))
        } else {
            None
        },
        voice_lossless_path: if voice_recorder.is_some() {
            Some(path_as_string(&bundle_dir, &bundle_dir.join("voice.wav")))
        } else {
            None
        },
        voice_log_path: if voice_recorder.is_some() {
            Some(path_as_string(&bundle_dir, &bundle_dir.join("voice_ffmpeg.log")))
        } else {
            None
        },
        screen_log_path: if screen_recorder.is_some() {
            Some(path_as_string(&bundle_dir, &bundle_dir.join("screen_ffmpeg.log")))
        } else {
            None
        },
        detail_when_voice: voice,
    };

    let manifest_path = bundle_dir.join("manifest.json");
    let manifest_json = serde_json::to_string_pretty(&manifest)?;
    fs::write(&manifest_path, manifest_json)
        .with_context(|| format!("Failed to write manifest at {}", manifest_path.display()))?;

    notify_ready(&bundle_dir);

    // Mark session as complete
    if let Ok(Some(mut session)) = session_state::get(&session_id) {
        session.status = SessionStatus::Complete;
        session.end_time = Some(chrono::Utc::now().to_rfc3339());
        let _ = session_state::update(&session_id, &session);
    }

    println!("\nADA Session Complete:");
    println!("  ID: {}", session_id);
    println!("  Bundle: {}", bundle_dir.display());
    println!("  Manifest: {}", manifest_path.display());
    Ok(())
}

// LCOV_EXCL_STOP

fn map_tracer_result<T, E>(result: Result<T, E>) -> anyhow::Result<T>
where
    E: std::fmt::Display,
{
    result.map_err(|err| anyhow::anyhow!(err.to_string()))
}

#[cfg(test)]
mod tests {
    use super::{map_tracer_result, resolve_executable_path};

    #[test]
    fn map_tracer_result_ok() {
        let value = map_tracer_result::<_, &str>(Ok(42)).expect("ok result");
        assert_eq!(value, 42);
    }

    #[test]
    fn map_tracer_result_err() {
        let err = map_tracer_result::<(), &str>(Err("boom")).expect_err("err result");
        assert!(err.to_string().contains("boom"));
    }

    #[test]
    fn resolve_executable_path__direct_binary__then_unchanged() {
        let result = resolve_executable_path("/usr/bin/ls").unwrap();
        assert_eq!(result, "/usr/bin/ls");
    }

    #[test]
    fn resolve_executable_path__app_bundle__then_resolved() {
        // Use Calculator.app as it exists on all macOS systems
        let result = resolve_executable_path("/System/Applications/Calculator.app").unwrap();
        assert_eq!(
            result,
            "/System/Applications/Calculator.app/Contents/MacOS/Calculator"
        );
    }

    #[test]
    fn resolve_executable_path__nonexistent_app__then_error() {
        let result = resolve_executable_path("/nonexistent/Fake.app");
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Info.plist"));
    }
}

fn start_screen_recording(bundle_dir: &Path, screen_audio: bool) -> anyhow::Result<RecorderChild> {
    let output = bundle_dir.join("screen.mp4");
    let log_path = bundle_dir.join("screen_ffmpeg.log");

    let mut cmd = Command::new("screencapture");
    cmd.arg("-v").arg("-J").arg("video").arg("-U");
    if screen_audio {
        cmd.arg("-g");
    }
    cmd.arg(&output);

    let child = cmd
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(open_log_file(&log_path)?)
        .spawn()
        .context("Failed to start screencapture")?;

    Ok(RecorderChild {
        name: "screen",
        child,
        output,
    })
}

fn start_voice_recording(
    bundle_dir: &Path,
    audio_device: Option<&str>,
) -> anyhow::Result<RecorderChild> {
    let ffmpeg = which::which("ffmpeg").context("ffmpeg not found in PATH")?;
    let output = bundle_dir.join("voice.wav");
    let log_path = bundle_dir.join("voice_ffmpeg.log");
    let device = audio_device.unwrap_or(":0");

    let mut cmd = Command::new(ffmpeg);
    cmd.args([
        "-f",
        "avfoundation",
        "-loglevel",
        "info",
        "-thread_queue_size",
        "1024",
        "-rtbufsize",
        "256M",
        "-i",
        device,
        "-ac",
        "1",
        "-c:a",
        "pcm_s16le",
        "-y",
    ]);
    cmd.arg(&output);

    let child = cmd
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(open_log_file(&log_path)?)
        .spawn()
        .context("Failed to start voice recorder (ffmpeg)")?;

    Ok(RecorderChild {
        name: "voice",
        child,
        output,
    })
}

fn stop_recorder(recorder: &mut RecorderChild) -> anyhow::Result<()> {
    if recorder.child.try_wait()?.is_some() {
        return Ok(());
    }

    send_signal(recorder.child.id(), libc::SIGINT)?;

    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline {
        if recorder.child.try_wait()?.is_some() {
            return Ok(());
        }
        thread::sleep(Duration::from_millis(100));
    }

    recorder.child.kill()?;
    let _ = recorder.child.wait();
    Ok(())
}

fn send_signal(pid: u32, signal: i32) -> std::io::Result<()> {
    let result = unsafe { libc::kill(pid as i32, signal) };
    if result == 0 {
        Ok(())
    } else {
        Err(std::io::Error::last_os_error())
    }
}

fn open_log_file(path: &Path) -> anyhow::Result<std::fs::File> {
    std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)
        .with_context(|| format!("Failed to open log file {}", path.display()))
}

fn encode_voice_to_aac(bundle_dir: &Path) -> anyhow::Result<PathBuf> {
    let ffmpeg = which::which("ffmpeg").context("ffmpeg not found in PATH")?;
    let input = bundle_dir.join("voice.wav");
    let output = bundle_dir.join("voice.m4a");

    if !input.exists() {
        anyhow::bail!("voice.wav not found at {}", input.display());
    }

    let status = Command::new(ffmpeg)
        .args([
            "-y",
            "-i",
            input.to_str().unwrap_or_default(),
            "-c:a",
            "aac",
            "-b:a",
            "128k",
        ])
        .arg(&output)
        .status()
        .context("Failed to encode voice.m4a")?;

    if !status.success() {
        anyhow::bail!("ffmpeg failed while encoding voice.m4a");
    }

    Ok(output)
}

fn find_latest_trace_session(trace_root: &Path) -> Option<PathBuf> {
    let mut sessions: Vec<PathBuf> = fs::read_dir(trace_root)
        .ok()?
        .filter_map(|entry| entry.ok())
        .map(|entry| entry.path())
        .filter(|path| {
            path.file_name()
                .and_then(|name| name.to_str())
                .map(|name| name.starts_with("session_"))
                .unwrap_or(false)
        })
        .collect();

    sessions.sort();
    let session_dir = sessions.pop()?;

    let mut pid_dirs: Vec<PathBuf> = fs::read_dir(&session_dir)
        .ok()?
        .filter_map(|entry| entry.ok())
        .map(|entry| entry.path())
        .filter(|path| path.is_dir())
        .collect();

    pid_dirs.sort();
    pid_dirs.pop().or(Some(session_dir))
}

fn notify_ready(bundle_dir: &Path) {
    let message = format!("Bundle ready: {}", bundle_dir.display());
    let script = format!(
        "display notification \"{}\" with title \"ADA Capture\"",
        message.replace('"', "'")
    );

    let _ = Command::new("osascript")
        .arg("-e")
        .arg(script)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
}

fn path_as_string(bundle_dir: &Path, path: &Path) -> String {
    path.strip_prefix(bundle_dir)
        .unwrap_or(path)
        .to_string_lossy()
        .to_string()
}

fn current_time_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}
