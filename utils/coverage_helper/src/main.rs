//! # Coverage Helper for ADA Project
//! 
//! This tool provides unified coverage collection for Rust, C/C++, and Python code
//! using LLVM coverage tools that come with Rust or cargo-llvm-cov.
//! 
//! ## How It Works
//! 
//! Both Rust and C/C++ use the same LLVM coverage infrastructure:
//! - **Compile flags**: `-fprofile-instr-generate -fcoverage-mapping`
//! - **Output format**: `.profraw` files (raw profile data)
//! - **Processing**: `llvm-profdata` merges `.profraw` → `.profdata`
//! - **Reporting**: `llvm-cov` generates reports from `.profdata`
//! 
//! ## Usage Examples
//! 
//! ```bash
//! # Clean old coverage data
//! coverage_helper clean
//! 
//! # Run full coverage workflow
//! coverage_helper full --format lcov
//! 
//! # Or step by step:
//! coverage_helper clean
//! # ... run tests with LLVM_PROFILE_FILE set ...
//! coverage_helper collect
//! coverage_helper report --format html
//! ```
//! 
//! ## C/C++ Coverage Setup
//! 
//! In CMakeLists.txt:
//! ```cmake
//! if(ENABLE_COVERAGE)
//!     add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
//!     add_link_options(-fprofile-instr-generate -fcoverage-mapping)
//! endif()
//! ```
//! 
//! Then build with: `cmake -B build -DENABLE_COVERAGE=ON`
//! 
//! ## Environment Variables
//! 
//! - `LLVM_PROFILE_FILE`: Where to write .profraw files (e.g., "target/coverage/%p-%m.profraw")
//! - `CARGO_FEATURE_COVERAGE`: Set to "1" to enable coverage in Cargo builds
//! - `RUSTFLAGS`: Should include "-C instrument-coverage" for Rust coverage
//! 
//! ## Tool Discovery
//! 
//! The helper automatically finds LLVM tools in these locations:
//! 1. System PATH
//! 2. Homebrew LLVM: `/opt/homebrew/opt/llvm/bin/` (macOS ARM64)
//! 3. cargo-llvm-cov bundled tools (via `cargo llvm-cov show-env`)
//! 
//! ## Troubleshooting
//! 
//! If "llvm-profdata not found":
//! - Install LLVM: `brew install llvm` (macOS) or `apt install llvm` (Linux)
//! - Or install cargo-llvm-cov: `cargo install cargo-llvm-cov`
//! 
//! If no coverage data generated:
//! - Ensure code was compiled with coverage flags
//! - Check LLVM_PROFILE_FILE is set correctly
//! - Verify tests actually executed the instrumented code

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use walkdir::WalkDir;

mod toolchains;
mod dashboard;

#[derive(Parser)]
#[command(name = "coverage_helper")]
#[command(about = "Coverage collection helper for ADA project")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Clean coverage data
    Clean,
    /// Collect coverage data from all components
    Collect,
    /// Generate coverage report
    Report {
        /// Output format (lcov, html, text)
        #[arg(short, long, default_value = "lcov")]
        format: String,
    },
    /// Run full coverage workflow (clean, test with coverage, collect, report)
    Full {
        /// Output format for final report
        #[arg(short, long, default_value = "lcov")]
        format: String,
    },
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    
    match cli.command {
        Commands::Clean => clean_coverage(),
        Commands::Collect => collect_coverage(),
        Commands::Report { format } => generate_report(&format),
        Commands::Full { format } => {
            clean_coverage()?;
            run_tests_with_coverage()?;
            collect_coverage()?;
            generate_report(&format)
        }
    }
}

fn get_workspace_root() -> Result<PathBuf> {
    let output = Command::new("cargo")
        .args(&["locate-project", "--workspace", "--message-format=plain"])
        .output()
        .context("Failed to locate workspace root")?;
    
    let cargo_toml = PathBuf::from(String::from_utf8_lossy(&output.stdout).trim());
    Ok(cargo_toml.parent().unwrap().to_path_buf())
}

fn clean_coverage() -> Result<()> {
    println!("Cleaning coverage data...");
    
    let workspace = get_workspace_root()?;
    let coverage_dir = workspace.join("target").join("coverage");
    
    if coverage_dir.exists() {
        fs::remove_dir_all(&coverage_dir)
            .context("Failed to remove coverage directory")?;
    }
    
    fs::create_dir_all(&coverage_dir)
        .context("Failed to create coverage directory")?;
    
    // Clean C/C++ coverage files
    for entry in WalkDir::new(workspace.join("target"))
        .into_iter()
        .filter_map(|e| e.ok())
    {
        let path = entry.path();
        if path.extension().map_or(false, |ext| ext == "profraw" || ext == "profdata" || ext == "gcda" || ext == "gcno") {
            fs::remove_file(path).ok();
        }
    }
    
    println!("Coverage data cleaned.");
    Ok(())
}

fn run_tests_with_coverage() -> Result<()> {
    println!("Running tests with coverage enabled...");
    
    let workspace = get_workspace_root()?;
    
    // Set environment variables for coverage
    env::set_var("CARGO_FEATURE_COVERAGE", "1");
    env::set_var("RUSTFLAGS", "-C instrument-coverage");
    env::set_var("LLVM_PROFILE_FILE", workspace.join("target/coverage/prof-%p-%m.profraw").to_str().unwrap());
    
    // Run Rust and C/C++ tests with coverage
    let status = Command::new("cargo")
        .args(&["test", "--all", "--features", "tracer_backend/coverage,query_engine/coverage"])
        .env("CARGO_FEATURE_COVERAGE", "1")
        .status()
        .context("Failed to run tests with coverage")?;
    
    if !status.success() {
        anyhow::bail!("Tests failed");
    }
    
    // Run Python tests with coverage if query_engine has Python tests
    let query_engine_dir = workspace.join("query_engine");
    if query_engine_dir.join("tests").exists() {
        println!("Running Python tests with coverage...");
        
        let status = Command::new("python")
            .args(&["-m", "pytest", "--cov=query_engine", "--cov-report=lcov:target/coverage/python.lcov"])
            .current_dir(&query_engine_dir)
            .status();
        
        if let Err(e) = status {
            eprintln!("Warning: Failed to run Python tests: {}", e);
        }
    }
    
    println!("Tests completed.");
    Ok(())
}

fn collect_coverage() -> Result<()> {
    println!("Collecting coverage data...");

    let workspace = get_workspace_root()?;
    let coverage_dir = workspace.join("target").join("coverage");
    let report_dir = workspace.join("target").join("coverage_report");

    // Ensure directories exist
    std::fs::create_dir_all(&coverage_dir)?;
    std::fs::create_dir_all(&report_dir)?;

    let mut lcov_files = Vec::new();

    // Collect unified Rust + C++ coverage (they run together via cargo test)
    let unified_lcov = report_dir.join("unified.lcov");
    match collect_unified_coverage(&workspace, &coverage_dir, &unified_lcov) {
        Ok(_) => {
            lcov_files.push(unified_lcov);
        }
        Err(e) => {
            eprintln!("Warning: Failed to collect unified coverage: {}", e);
        }
    }

    // Collect Python coverage (separate ecosystem)
    let python_lcov = report_dir.join("python.lcov");
    if let Ok(_) = collect_python_coverage(&workspace, &python_lcov) {
        lcov_files.push(python_lcov);
    }

    // Always create merged.lcov for consistency (even if just one file)
    let merged_lcov = report_dir.join("merged.lcov");
    if lcov_files.len() > 1 {
        toolchains::merge_lcov_files(&lcov_files, &merged_lcov)?;
        println!("\nMerged coverage data to: {}", merged_lcov.display());
    } else if lcov_files.len() == 1 {
        // Copy the single file as merged.lcov for consistency
        std::fs::copy(&lcov_files[0], &merged_lcov)?;
        println!("\nCoverage data saved to: {}", merged_lcov.display());
    } else {
        println!("\nNo coverage data collected.");
    }

    Ok(())
}

/// Collect Python coverage using pytest-cov
fn collect_python_coverage(workspace: &Path, output_lcov: &Path) -> Result<()> {
    println!("\nCollecting Python coverage...");
    
    // Check which Python components exist
    let mut components_to_test = vec![];
    
    let query_engine_dir = workspace.join("query_engine");
    if query_engine_dir.exists() && query_engine_dir.join("pyproject.toml").exists() {
        components_to_test.push(("query_engine", query_engine_dir));
    }
    
    let mcp_server_dir = workspace.join("mcp_server");
    if mcp_server_dir.exists() && mcp_server_dir.join("pyproject.toml").exists() {
        components_to_test.push(("mcp_server", mcp_server_dir));
    }
    
    if components_to_test.is_empty() {
        println!("  No Python components found");
        return Ok(());
    }
    
    let mut all_lcov_data = String::new();
    
    for (name, dir) in components_to_test {
        println!("  Testing Python component: {}", name);
        
        // Create a temporary directory for this component's coverage
        let temp_coverage_file = workspace.join("target").join("coverage").join(format!("{}_coverage.xml", name));
        
        // Run pytest with coverage for this component
        let output = Command::new("python3")
            .args(&[
                "-m", "pytest",
                "--cov", name,
                "--cov-report", &format!("xml:{}", temp_coverage_file.display()),
                "--cov-report", "term",
            ])
            .current_dir(&dir)
            .output();
        
        match output {
            Ok(result) if result.status.success() => {
                println!("    Tests passed for {}", name);
                
                // Convert XML to LCOV if the coverage file was created
                if temp_coverage_file.exists() {
                    if let Ok(lcov_data) = convert_xml_to_lcov(&temp_coverage_file, &dir) {
                        all_lcov_data.push_str(&lcov_data);
                    }
                }
            }
            Ok(result) => {
                let stderr = String::from_utf8_lossy(&result.stderr);
                let stdout = String::from_utf8_lossy(&result.stdout);
                println!("    No tests found or tests failed for {}", name);
                if !stderr.is_empty() {
                    println!("    stderr: {}", stderr);
                }
                if !stdout.is_empty() {
                    println!("    stdout: {}", stdout);
                }
            }
            Err(e) => {
                println!("    pytest not available or failed for {}: {}", name, e);
            }
        }
    }
    
    // Write combined LCOV data
    if !all_lcov_data.is_empty() {
        fs::write(output_lcov, all_lcov_data)?;
        println!("  Python coverage saved to: {}", output_lcov.display());
    } else {
        println!("  No Python coverage data collected");
    }
    
    Ok(())
}

/// Convert Python coverage XML to LCOV format
fn convert_xml_to_lcov(xml_file: &Path, base_dir: &Path) -> Result<String> {
    // For now, use a simple Python script to convert
    let python_script = r#"
import xml.etree.ElementTree as ET
import sys
import os

def convert_xml_to_lcov(xml_path, base_dir):
    tree = ET.parse(xml_path)
    root = tree.getroot()
    
    lcov_output = []
    
    for package in root.findall('.//package'):
        for class_elem in package.findall('classes/class'):
            filename = class_elem.get('filename')
            if filename:
                # Make path relative to workspace root
                full_path = os.path.join(base_dir, filename)
                lcov_output.append(f'SF:{full_path}')
                
                # Process lines
                for line in class_elem.findall('lines/line'):
                    line_number = line.get('number')
                    hits = line.get('hits', '0')
                    lcov_output.append(f'DA:{line_number},{hits}')
                
                lcov_output.append('end_of_record')
    
    return '\n'.join(lcov_output)

if __name__ == '__main__':
    xml_path = sys.argv[1]
    base_dir = sys.argv[2]
    print(convert_xml_to_lcov(xml_path, base_dir))
"#;
    
    let output = Command::new("python3")
        .arg("-c")
        .arg(python_script)
        .arg(xml_file)
        .arg(base_dir)
        .output()?;
    
    if output.status.success() {
        Ok(String::from_utf8_lossy(&output.stdout).to_string())
    } else {
        // Fallback: return empty string if conversion fails
        Ok(String::new())
    }
}

/// Collect unified Rust and C++ coverage using LLVM tools.
///
/// Since C++ tests are wrapped as Rust tests via build.rs, they all run
/// together during 'cargo test' and generate profraw files in the same session.
/// This function:
/// 1. Finds all .profraw files from the unified test run
/// 2. Merges them into a single .profdata file using llvm-profdata
/// 3. Exports to LCOV format using llvm-cov with both Rust and C++ binaries
///
/// This unified approach eliminates duplicate test execution and provides
/// consistent coverage metrics across all native code.
fn collect_unified_coverage(workspace: &Path, coverage_dir: &Path, output_lcov: &Path) -> Result<()> {
    println!("\nCollecting unified Rust + C++ coverage...");

    // Find ALL .profraw files from the unified test run
    let profraw_files: Vec<PathBuf> = WalkDir::new(coverage_dir)
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| {
            let path = e.path();
            path.extension().map_or(false, |ext| ext == "profraw")
        })
        .map(|e| e.path().to_path_buf())
        .collect();

    if profraw_files.is_empty() {
        println!("  No coverage data found. Make sure tests were run with coverage enabled.");
        return Ok(());
    }

    println!("  Found {} profraw files from unified test run", profraw_files.len());

    // Use Rust toolchain (works for both Rust and C++ with LLVM coverage)
    let toolchain = toolchains::detect_rust_toolchain()?;

    // Merge all profraw files into a single profdata
    let profdata_path = coverage_dir.join("unified.profdata");
    toolchains::merge_profdata(&toolchain, &profraw_files, &profdata_path)?;

    // Collect all test binaries (both Rust and C++)
    let mut test_binaries = Vec::new();

    // Find Rust test binaries in deps
    for entry in WalkDir::new(workspace.join("target"))
        .into_iter()
        .filter_map(|e| e.ok())
    {
        let path = entry.path();
        if path.is_file() && path.to_str().map_or(false, |s| s.contains("/deps/") && !s.contains(".")) {
            test_binaries.push(path.to_path_buf());
        }
    }

    // Find C++ test binaries in the predictable location
    let profile = if workspace.join("target/release/tracer_backend/test").exists() {
        "release"
    } else {
        "debug"
    };

    let cpp_test_dir = workspace.join("target").join(profile).join("tracer_backend").join("test");
    if cpp_test_dir.exists() {
        if let Ok(entries) = fs::read_dir(&cpp_test_dir) {
            for entry in entries.filter_map(|e| e.ok()) {
                let path = entry.path();
                if path.is_file() && path.file_name()
                    .and_then(|n| n.to_str())
                    .map_or(false, |n| n.starts_with("test_")) {
                    test_binaries.push(path);
                }
            }
        }
    }

    // CRITICAL: Include C++ static libraries for source mapping
    // The test binaries are statically linked, but llvm-cov needs the original
    // libraries to map coverage data back to C++ source files
    // Use the predictable path created by tracer_backend's build.rs
    let mut cpp_artifacts = Vec::new();

    // The tracer_backend build.rs copies all libraries to a predictable location:
    // target/{profile}/tracer_backend/lib/
    let cpp_lib_dir = workspace.join("target").join(profile).join("tracer_backend").join("lib");
    if cpp_lib_dir.exists() {
        if let Ok(entries) = fs::read_dir(&cpp_lib_dir) {
            for entry in entries.filter_map(|e| e.ok()) {
                let path = entry.path();
                // Include static libraries (.a files) which contain the compiled C++ code
                if path.is_file() && path.extension().map_or(false, |ext| ext == "a") {
                    cpp_artifacts.push(path);
                }
            }
        }
    }

    // Also find the actual build directory to include object files
    // These provide more granular coverage mapping
    let build_pattern = workspace.join("target").join(profile).join("build");
    if build_pattern.exists() {
        // Find the most recent tracer_backend build directory
        let mut tracer_backend_dirs: Vec<_> = fs::read_dir(&build_pattern)?
            .filter_map(|entry| entry.ok())
            .filter(|entry| {
                entry.file_name().to_str()
                    .map_or(false, |name| name.starts_with("tracer_backend-"))
            })
            .collect();

        // Sort by modification time to get the most recent build
        tracer_backend_dirs.sort_by_key(|dir| {
            dir.metadata()
                .and_then(|m| m.modified())
                .unwrap_or(std::time::SystemTime::UNIX_EPOCH)
        });

        if let Some(latest_build) = tracer_backend_dirs.last() {
            let out_dir = latest_build.path().join("out");

            // Collect object files from the build directory
            let build_subdir = out_dir.join("build");
            if build_subdir.exists() {
                for entry in WalkDir::new(&build_subdir)
                    .into_iter()
                    .filter_map(|e| e.ok())
                    .filter(|e| e.path().extension().map_or(false, |ext| ext == "o"))
                {
                    cpp_artifacts.push(entry.path().to_path_buf());
                }
            }
        }
    }

    if !cpp_artifacts.is_empty() {
        println!("  Found {} C++ artifacts (libraries/objects) for source mapping", cpp_artifacts.len());
        // Add C++ artifacts to the binaries list for llvm-cov
        test_binaries.extend(cpp_artifacts);
    }

    if !test_binaries.is_empty() {
        println!("  Total {} binaries/artifacts for coverage mapping", test_binaries.len());
        // Export to LCOV format with all binaries and artifacts
        toolchains::export_lcov(&toolchain, &test_binaries, &profdata_path, output_lcov)?;
        println!("  Unified coverage saved to: {}", output_lcov.display());
    } else {
        println!("  No test binaries found");
    }

    Ok(())
}

// Note: C++ coverage is now collected as part of unified coverage since
// C++ tests run via Rust wrappers during 'cargo test'

fn generate_report(format: &str) -> Result<()> {
    println!("Generating {} coverage report...", format);
    
    let workspace = get_workspace_root()?;
    let coverage_dir = workspace.join("target").join("coverage");
    
    match format {
        "lcov" => {
            // Merge all lcov files
            let mut lcov_files = vec![];
            
            for entry in ["rust.lcov", "cpp.lcov", "python.lcov"] {
                let path = coverage_dir.join(entry);
                if path.exists() {
                    lcov_files.push(path);
                }
            }
            
            if lcov_files.is_empty() {
                println!("No coverage data to report.");
                return Ok(());
            }
            
            let merged_lcov = coverage_dir.join("coverage.lcov");
            
            // Merge lcov files
            let mut merge_cmd = Command::new("lcov");
            for file in &lcov_files {
                merge_cmd.arg("-a").arg(file);
            }
            merge_cmd.arg("-o").arg(&merged_lcov);
            
            let status = merge_cmd.status();
            if status.is_err() {
                // Fallback: just concatenate files
                let mut merged_content = String::new();
                for file in &lcov_files {
                    merged_content.push_str(&fs::read_to_string(file)?);
                }
                fs::write(&merged_lcov, merged_content)?;
            }
            
            println!("Coverage report saved to: {}", merged_lcov.display());
            
            // Calculate total coverage
            calculate_coverage_percentage(&merged_lcov)?;
        }
        "html" => {
            let report_dir = workspace.join("target").join("coverage_report");
            let merged_lcov = report_dir.join("merged.lcov");
            
            // First ensure we have merged LCOV
            if !merged_lcov.exists() {
                println!("No merged LCOV found. Running collection first...");
                collect_coverage()?;
            }
            
            // Generate dashboard
            if merged_lcov.exists() {
                dashboard::generate_dashboard(&workspace, &report_dir, &merged_lcov)?;
                println!("HTML dashboard saved to: {}/index.html", report_dir.display());
            } else {
                println!("No coverage data available for HTML report");
            }
        }
        "text" => {
            // Generate text summary from LCOV data
            // First try merged.lcov in coverage_report directory
            let report_dir = workspace.join("target").join("coverage_report");
            let merged_lcov = report_dir.join("merged.lcov");

            if merged_lcov.exists() {
                calculate_coverage_percentage(&merged_lcov)?;
            } else {
                // Try to find any LCOV file
                let coverage_lcov = coverage_dir.join("coverage.lcov");
                let unified_lcov = report_dir.join("unified.lcov");

                if coverage_lcov.exists() {
                    calculate_coverage_percentage(&coverage_lcov)?;
                } else if unified_lcov.exists() {
                    calculate_coverage_percentage(&unified_lcov)?;
                } else {
                    println!("No LCOV data found for text report");
                    println!("Run 'coverage_helper collect' first to generate coverage data");
                }
            }
        }
        _ => {
            anyhow::bail!("Unsupported format: {}", format);
        }
    }
    
    Ok(())
}

fn calculate_coverage_percentage(lcov_file: &Path) -> Result<()> {
    let content = fs::read_to_string(lcov_file)?;

    let mut lines_hit = 0;
    let mut lines_total = 0;
    let mut functions_hit = 0;
    let mut functions_total = 0;
    let mut branches_hit = 0;
    let mut branches_total = 0;

    // Track per-file statistics
    let mut current_file: String;
    let mut file_lines_hit = 0;
    let mut file_lines_total = 0;
    let mut file_functions_hit = 0;
    let mut file_functions_total = 0;
    let mut file_branches_hit = 0;
    let mut file_branches_total = 0;
    let mut skip_current_file = false;

    for line in content.lines() {
        // Track source file changes
        if let Some(file) = line.strip_prefix("SF:") {
            current_file = file.to_string();

            // Use centralized exclusion logic from dashboard module
            skip_current_file = dashboard::should_exclude_from_coverage(&current_file);

            file_lines_hit = 0;
            file_lines_total = 0;
            file_functions_hit = 0;
            file_functions_total = 0;
            file_branches_hit = 0;
            file_branches_total = 0;
        }
        // Line coverage data
        else if line.starts_with("DA:") && !skip_current_file {
            let parts: Vec<&str> = line[3..].split(',').collect();
            if parts.len() == 2 {
                file_lines_total += 1;
                if let Ok(hits) = parts[1].parse::<i32>() {
                    if hits > 0 {
                        file_lines_hit += 1;
                    }
                }
            }
        }
        // Function coverage data
        else if line.starts_with("FNDA:") && !skip_current_file {
            let parts: Vec<&str> = line[5..].split(',').collect();
            if parts.len() >= 2 {
                if let Ok(hits) = parts[0].parse::<i32>() {
                    file_functions_total += 1;
                    if hits > 0 {
                        file_functions_hit += 1;
                    }
                }
            }
        }
        // Branch coverage data
        else if line.starts_with("BRDA:") && !skip_current_file {
            let parts: Vec<&str> = line[5..].split(',').collect();
            if parts.len() >= 4 {
                file_branches_total += 1;
                // Check if branch was taken (not "-" or "0")
                if parts[3] != "-" {
                    if let Ok(taken) = parts[3].parse::<i32>() {
                        if taken > 0 {
                            file_branches_hit += 1;
                        }
                    }
                }
            }
        }
        // Summary lines - use these if available for more accurate counts
        else if line.starts_with("LF:") && !skip_current_file {
            if let Ok(total) = line[3..].parse::<u64>() {
                file_lines_total = total as i32;
            }
        }
        else if line.starts_with("LH:") && !skip_current_file {
            if let Ok(hit) = line[3..].parse::<u64>() {
                file_lines_hit = hit as i32;
            }
        }
        else if line.starts_with("FNF:") && !skip_current_file {
            if let Ok(total) = line[4..].parse::<u64>() {
                file_functions_total = total as i32;
            }
        }
        else if line.starts_with("FNH:") && !skip_current_file {
            if let Ok(hit) = line[4..].parse::<u64>() {
                file_functions_hit = hit as i32;
            }
        }
        else if line.starts_with("BRF:") && !skip_current_file {
            if let Ok(total) = line[4..].parse::<u64>() {
                file_branches_total = total as i32;
            }
        }
        else if line.starts_with("BRH:") && !skip_current_file {
            if let Ok(hit) = line[4..].parse::<u64>() {
                file_branches_hit = hit as i32;
            }
        }
        // End of record - accumulate file stats
        else if line == "end_of_record" {
            lines_hit += file_lines_hit;
            lines_total += file_lines_total;
            functions_hit += file_functions_hit;
            functions_total += file_functions_total;
            branches_hit += file_branches_hit;
            branches_total += file_branches_total;
        }
    }

    // Display comprehensive coverage statistics
    println!("\n📊 Coverage Summary:");
    println!("═══════════════════════════════════════════════════");

    // Line coverage
    if lines_total > 0 {
        let line_percentage = (lines_hit as f64 / lines_total as f64) * 100.0;
        println!("  Lines:     {:.2}% ({}/{} lines)", line_percentage, lines_hit, lines_total);
    } else {
        println!("  Lines:     No line coverage data");
    }

    // Function coverage
    if functions_total > 0 {
        let function_percentage = (functions_hit as f64 / functions_total as f64) * 100.0;
        println!("  Functions: {:.2}% ({}/{} functions)", function_percentage, functions_hit, functions_total);
    } else {
        println!("  Functions: No function coverage data");
    }

    // Branch coverage
    if branches_total > 0 {
        let branch_percentage = (branches_hit as f64 / branches_total as f64) * 100.0;
        println!("  Branches:  {:.2}% ({}/{} branches)", branch_percentage, branches_hit, branches_total);
    } else {
        println!("  Branches:  No branch coverage data");
    }

    // Overall coverage (weighted average if all metrics available)
    if lines_total > 0 || functions_total > 0 || branches_total > 0 {
        let total_items = lines_total + functions_total + branches_total;
        let total_hits = lines_hit + functions_hit + branches_hit;
        let overall_percentage = (total_hits as f64 / total_items as f64) * 100.0;
        println!("═══════════════════════════════════════════════════");
        println!("  Overall:   {:.2}% coverage", overall_percentage);
    }

    println!();
    Ok(())
}