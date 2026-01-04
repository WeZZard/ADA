//! Trace management commands.
//!
//! Provides CLI commands for:
//! - Starting trace sessions
//! - Stopping trace sessions
//! - Listing sessions

use clap::Subcommand;
use std::path::PathBuf;
use std::process::Command;

#[derive(Subcommand)]
pub enum TraceCommands {
    /// Start a new trace session
    Start {
        /// Path to the binary to trace
        binary: String,

        /// Output directory for trace files
        #[arg(short, long, default_value = "./traces")]
        output: PathBuf,

        /// Arguments to pass to the binary
        #[arg(trailing_var_arg = true)]
        args: Vec<String>,
    },

    /// Start tracing an Xcode project
    #[command(name = "start-xcode")]
    StartXcode {
        /// Path to .xcodeproj or .xcworkspace
        project: String,

        /// Scheme to build and run
        #[arg(short, long)]
        scheme: String,

        /// Output directory for trace files
        #[arg(short, long, default_value = "./traces")]
        output: PathBuf,
    },

    /// Attach to a running process
    Attach {
        /// Process ID to attach to
        pid: u32,

        /// Output directory for trace files
        #[arg(short, long, default_value = "./traces")]
        output: PathBuf,
    },

    /// Stop the current trace session
    Stop,

    /// List trace sessions
    List {
        /// Directory containing trace sessions
        #[arg(default_value = "./traces")]
        directory: PathBuf,
    },
}

pub fn run(cmd: TraceCommands) -> anyhow::Result<()> {
    match cmd {
        TraceCommands::Start { binary, output, args } => {
            start_trace(&binary, &output, &args)
        }
        TraceCommands::StartXcode { project, scheme, output } => {
            start_xcode_trace(&project, &scheme, &output)
        }
        TraceCommands::Attach { pid, output } => {
            attach_trace(pid, &output)
        }
        TraceCommands::Stop => {
            stop_trace()
        }
        TraceCommands::List { directory } => {
            list_sessions(&directory)
        }
    }
}

fn start_trace(binary: &str, output: &PathBuf, args: &[String]) -> anyhow::Result<()> {
    // Use the existing tracer binary
    let tracer_path = find_tracer()?;

    let session_name = format!(
        "session_{}_{}",
        chrono_lite_timestamp(),
        std::path::Path::new(binary)
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("unknown")
    );
    let session_dir = output.join(&session_name);

    println!("Starting trace session: {}", session_dir.display());
    println!("Binary: {}", binary);
    if !args.is_empty() {
        println!("Args: {:?}", args);
    }

    // Ensure output directory exists
    std::fs::create_dir_all(&session_dir)?;

    // Build tracer command - tracer uses "spawn <binary>" mode
    let mut cmd = Command::new(&tracer_path);
    cmd.arg("spawn");
    cmd.arg(binary);
    cmd.arg("--output").arg(&session_dir);
    cmd.args(args);

    // Run tracer
    let status = cmd.status()?;
    if !status.success() {
        anyhow::bail!("Tracer exited with status: {}", status);
    }

    println!("\nTrace complete. Session saved to: {}", session_dir.display());
    Ok(())
}

fn start_xcode_trace(project: &str, scheme: &str, output: &PathBuf) -> anyhow::Result<()> {
    println!("Building Xcode project: {}", project);
    println!("Scheme: {}", scheme);

    // Build the project with xcodebuild
    let build_status = Command::new("xcodebuild")
        .arg("-project")
        .arg(project)
        .arg("-scheme")
        .arg(scheme)
        .arg("-configuration")
        .arg("Debug")
        .arg("build")
        .status()?;

    if !build_status.success() {
        anyhow::bail!("xcodebuild failed");
    }

    // Find the built binary
    let build_settings = Command::new("xcodebuild")
        .arg("-project")
        .arg(project)
        .arg("-scheme")
        .arg(scheme)
        .arg("-showBuildSettings")
        .output()?;

    let settings = String::from_utf8_lossy(&build_settings.stdout);

    // Parse BUILT_PRODUCTS_DIR and EXECUTABLE_NAME
    let mut products_dir = None;
    let mut executable_name = None;

    for line in settings.lines() {
        let line = line.trim();
        if line.starts_with("BUILT_PRODUCTS_DIR = ") {
            products_dir = Some(line.strip_prefix("BUILT_PRODUCTS_DIR = ").unwrap().to_string());
        } else if line.starts_with("EXECUTABLE_NAME = ") {
            executable_name = Some(line.strip_prefix("EXECUTABLE_NAME = ").unwrap().to_string());
        }
    }

    let products_dir = products_dir.ok_or_else(|| anyhow::anyhow!("Could not find BUILT_PRODUCTS_DIR"))?;
    let executable_name = executable_name.ok_or_else(|| anyhow::anyhow!("Could not find EXECUTABLE_NAME"))?;

    let binary_path = format!("{}/{}", products_dir, executable_name);
    println!("Built binary: {}", binary_path);

    // Start trace with the built binary
    start_trace(&binary_path, output, &[])
}

fn attach_trace(pid: u32, output: &PathBuf) -> anyhow::Result<()> {
    let tracer_path = find_tracer()?;

    let session_name = format!("session_{}_pid_{}", chrono_lite_timestamp(), pid);
    let session_dir = output.join(&session_name);

    println!("Attaching to PID: {}", pid);
    println!("Session: {}", session_dir.display());

    std::fs::create_dir_all(&session_dir)?;

    let status = Command::new(&tracer_path)
        .arg("attach")
        .arg(pid.to_string())
        .arg("--output")
        .arg(&session_dir)
        .status()?;

    if !status.success() {
        anyhow::bail!("Tracer exited with status: {}", status);
    }

    println!("\nTrace complete. Session saved to: {}", session_dir.display());
    Ok(())
}

fn stop_trace() -> anyhow::Result<()> {
    // Signal the running tracer to stop
    // For now, just print instructions
    println!("To stop a running trace, press Ctrl+C in the tracer terminal.");
    println!("Or send SIGINT to the tracer process.");
    Ok(())
}

fn list_sessions(directory: &PathBuf) -> anyhow::Result<()> {
    if !directory.exists() {
        println!("No sessions found in: {}", directory.display());
        return Ok(());
    }

    let mut sessions = Vec::new();

    for entry in std::fs::read_dir(directory)? {
        let entry = entry?;
        let path = entry.path();

        if path.is_dir() {
            let manifest = path.join("manifest.json");
            if manifest.exists() {
                sessions.push(path);
            }
        }
    }

    if sessions.is_empty() {
        println!("No trace sessions found in: {}", directory.display());
        return Ok(());
    }

    sessions.sort();
    println!("Trace sessions in {}:\n", directory.display());

    for session in sessions {
        let name = session.file_name().and_then(|n| n.to_str()).unwrap_or("?");
        println!("  {}", name);
    }

    Ok(())
}

/// Find the tracer binary
fn find_tracer() -> anyhow::Result<PathBuf> {
    // Try common locations
    let candidates = [
        // Relative to ada binary
        std::env::current_exe()
            .ok()
            .and_then(|p| p.parent().map(|p| p.join("tracer"))),
        // In PATH
        which::which("tracer").ok(),
        // Development location
        Some(PathBuf::from("./target/release/tracer")),
        Some(PathBuf::from("./target/debug/tracer")),
    ];

    for candidate in candidates.iter().flatten() {
        if candidate.exists() {
            return Ok(candidate.clone());
        }
    }

    anyhow::bail!(
        "Could not find tracer binary. Please ensure it's built and in PATH, \
         or run from the project root."
    )
}

/// Simple timestamp without chrono dependency
fn chrono_lite_timestamp() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let duration = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default();
    format!("{}", duration.as_secs())
}
