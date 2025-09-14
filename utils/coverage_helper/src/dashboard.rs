//! HTML Dashboard generator for coverage reports

use anyhow::{Context, Result};
use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::process::Command;

/// Coverage metrics for a component
#[derive(Debug, Default, Clone)]
pub struct ComponentMetrics {
    pub line_coverage: f64,
    pub function_coverage: f64,
    pub branch_coverage: f64,
    pub lines_covered: usize,
    pub lines_total: usize,
    pub functions_covered: usize,
    pub functions_total: usize,
    pub branches_covered: usize,
    pub branches_total: usize,
}

/// Generate the HTML dashboard
pub fn generate_dashboard(
    workspace: &Path,
    report_dir: &Path,
    merged_lcov: &Path,
) -> Result<()> {
    println!("\nGenerating HTML dashboard...");
    
    // Ensure report directory exists
    fs::create_dir_all(report_dir)?;
    
    // Parse LCOV file for metrics
    let metrics = parse_lcov_metrics(merged_lcov)?;
    
    // Get git information
    let commit = get_git_commit()?;
    let branch = get_git_branch()?;
    let timestamp = chrono::Local::now().format("%Y-%m-%d %H:%M:%S").to_string();
    
    // Check diff-cover results if available
    let changed_lines_metrics = get_diff_cover_metrics(report_dir)?;
    
    // Read template
    let template_path = workspace.join("utils/coverage_helper/dashboard_template.html");
    let template = if template_path.exists() {
        fs::read_to_string(&template_path)?
    } else {
        // Use embedded template if file doesn't exist
        include_str!("../dashboard_template.html").to_string()
    };
    
    // Replace placeholders
    let html = replace_placeholders(
        template,
        &metrics,
        &changed_lines_metrics,
        &commit,
        &branch,
        &timestamp,
    );
    
    // Write dashboard
    let dashboard_path = report_dir.join("index.html");
    fs::write(&dashboard_path, html)?;
    println!("  Dashboard generated: {}", dashboard_path.display());
    
    // Generate full HTML report using genhtml if available
    if which::which("genhtml").is_ok() {
        generate_full_html_report(merged_lcov, report_dir)?;
    }
    
    // Generate diff-coverage HTML report
    generate_diff_coverage_report(workspace, merged_lcov, report_dir)?;
    
    // Generate uncovered lines text file
    generate_uncovered_lines_report(merged_lcov, report_dir)?;
    
    // Generate coverage history HTML page
    generate_history_report(workspace, report_dir)?;
    
    Ok(())
}

/// Parse LCOV file for coverage metrics
fn parse_lcov_metrics(lcov_path: &Path) -> Result<HashMap<String, ComponentMetrics>> {
    let metrics = HashMap::new();
    
    if !lcov_path.exists() {
        return Ok(metrics);
    }
    
    let content = fs::read_to_string(lcov_path)?;
    let mut current_file = String::new();
    let mut component_data: HashMap<String, ComponentMetrics> = HashMap::new();
    
    // Initialize component metrics
    component_data.insert("tracer".to_string(), ComponentMetrics::default());
    component_data.insert("tracer_backend".to_string(), ComponentMetrics::default());
    component_data.insert("query_engine".to_string(), ComponentMetrics::default());
    component_data.insert("mcp_server".to_string(), ComponentMetrics::default());
    component_data.insert("total".to_string(), ComponentMetrics::default());
    
    for line in content.lines() {
        if line.starts_with("SF:") {
            current_file = line[3..].to_string();
        } else if line.starts_with("DA:") {
            // Line coverage data
            let parts: Vec<&str> = line[3..].split(',').collect();
            if parts.len() == 2 {
                let component = detect_component(&current_file);

                // Skip dependencies and test files from coverage metrics
                if component == "dependencies" || component == "test_files" {
                    continue;
                }

                let is_covered = parts[1] != "0";

                // Update component metrics
                component_data.entry(component.clone())
                    .and_modify(|m| {
                        m.lines_total += 1;
                        if is_covered {
                            m.lines_covered += 1;
                        }
                    });

                // Update total metrics (only for actual project files)
                if component != "other" {
                    component_data.entry("total".to_string())
                        .and_modify(|m| {
                            m.lines_total += 1;
                            if is_covered {
                                m.lines_covered += 1;
                            }
                        });
                }
            }
        } else if line.starts_with("FNDA:") {
            // Function coverage data
            let parts: Vec<&str> = line[5..].split(',').collect();
            if parts.len() >= 2 {
                let component = detect_component(&current_file);

                // Skip dependencies and test files from coverage metrics
                if component == "dependencies" || component == "test_files" {
                    continue;
                }

                if let Ok(hits) = parts[0].parse::<i32>() {
                    let is_covered = hits > 0;

                    // Update component metrics
                    component_data.entry(component.clone())
                        .and_modify(|m| {
                            m.functions_total += 1;
                            if is_covered {
                                m.functions_covered += 1;
                            }
                        });

                    // Update total metrics (only for actual project files)
                    if component != "other" {
                        component_data.entry("total".to_string())
                            .and_modify(|m| {
                                m.functions_total += 1;
                                if is_covered {
                                    m.functions_covered += 1;
                                }
                            });
                    }
                }
            }
        } else if line.starts_with("BRDA:") {
            // Branch coverage data
            let parts: Vec<&str> = line[5..].split(',').collect();
            if parts.len() >= 4 {
                let component = detect_component(&current_file);
                let is_covered = parts[3] != "-" && parts[3] != "0";

                // Update component metrics
                component_data.entry(component.clone())
                    .and_modify(|m| {
                        m.branches_total += 1;
                        if is_covered {
                            m.branches_covered += 1;
                        }
                    });

                // Update total metrics
                component_data.entry("total".to_string())
                    .and_modify(|m| {
                        m.branches_total += 1;
                        if is_covered {
                            m.branches_covered += 1;
                        }
                    });
            }
        } else if line.starts_with("LF:") {
            // Summary line for lines found (use if more accurate than counting DA lines)
            if let Ok(total) = line[3..].parse::<usize>() {
                let component = detect_component(&current_file);
                component_data.entry(component.clone())
                    .and_modify(|m| {
                        // Only update if we haven't counted DA lines yet or this is more accurate
                        if m.lines_total == 0 {
                            m.lines_total = total;
                        }
                    });
            }
        } else if line.starts_with("LH:") {
            // Summary line for lines hit
            if let Ok(hit) = line[3..].parse::<usize>() {
                let component = detect_component(&current_file);
                component_data.entry(component.clone())
                    .and_modify(|m| {
                        if m.lines_covered == 0 {
                            m.lines_covered = hit;
                        }
                    });
            }
        } else if line.starts_with("FNF:") {
            // Summary line for functions found
            if let Ok(total) = line[4..].parse::<usize>() {
                let component = detect_component(&current_file);

                // Skip dependencies and test files
                if component == "dependencies" || component == "test_files" {
                    continue;
                }

                // Add to component totals (don't replace, accumulate from multiple files)
                component_data.entry(component.clone())
                    .and_modify(|m| {
                        m.functions_total += total;
                    });

                // Also add to overall total (only for actual project files)
                if component != "other" {
                    component_data.entry("total".to_string())
                        .and_modify(|m| {
                            m.functions_total += total;
                        });
                }
            }
        } else if line.starts_with("FNH:") {
            // Summary line for functions hit
            if let Ok(hit) = line[4..].parse::<usize>() {
                let component = detect_component(&current_file);

                // Skip dependencies and test files
                if component == "dependencies" || component == "test_files" {
                    continue;
                }

                // Add to component coverage (accumulate from multiple files)
                component_data.entry(component.clone())
                    .and_modify(|m| {
                        m.functions_covered += hit;
                    });

                // Also add to overall coverage (only for actual project files)
                if component != "other" {
                    component_data.entry("total".to_string())
                        .and_modify(|m| {
                            m.functions_covered += hit;
                        });
                }
            }
        } else if line.starts_with("BRF:") {
            // Summary line for branches found
            if let Ok(total) = line[4..].parse::<usize>() {
                let component = detect_component(&current_file);

                // Skip dependencies and test files
                if component == "dependencies" || component == "test_files" {
                    continue;
                }

                // Add to component totals (accumulate from multiple files)
                component_data.entry(component.clone())
                    .and_modify(|m| {
                        m.branches_total += total;
                    });

                // Also add to overall total (only for actual project files)
                if component != "other" {
                    component_data.entry("total".to_string())
                        .and_modify(|m| {
                            m.branches_total += total;
                        });
                }
            }
        } else if line.starts_with("BRH:") {
            // Summary line for branches hit
            if let Ok(hit) = line[4..].parse::<usize>() {
                let component = detect_component(&current_file);

                // Skip dependencies and test files
                if component == "dependencies" || component == "test_files" {
                    continue;
                }

                // Add to component coverage (accumulate from multiple files)
                component_data.entry(component.clone())
                    .and_modify(|m| {
                        m.branches_covered += hit;
                    });

                // Also add to overall coverage (only for actual project files)
                if component != "other" {
                    component_data.entry("total".to_string())
                        .and_modify(|m| {
                            m.branches_covered += hit;
                        });
                }
            }
        }
    }

    // Calculate percentages for all coverage types
    for (_, metrics) in component_data.iter_mut() {
        if metrics.lines_total > 0 {
            metrics.line_coverage =
                (metrics.lines_covered as f64 / metrics.lines_total as f64) * 100.0;
        }
        if metrics.functions_total > 0 {
            metrics.function_coverage =
                (metrics.functions_covered as f64 / metrics.functions_total as f64) * 100.0;
        }
        if metrics.branches_total > 0 {
            metrics.branch_coverage =
                (metrics.branches_covered as f64 / metrics.branches_total as f64) * 100.0;
        }
    }
    
    Ok(component_data)
}

/// Check if a file should be excluded from coverage metrics
/// Returns true if the file is a dependency, test file, or other non-production code
pub fn should_exclude_from_coverage(file_path: &str) -> bool {
    // Dependencies
    file_path.contains(".cargo/registry") ||
    file_path.contains(".cargo/git") ||
    file_path.contains("/rustc-") ||
    file_path.contains("/private/tmp/") ||
    file_path.contains("/third_parties/") ||
    file_path.contains("third_parties/") ||
    file_path.contains("/.rustup/") ||
    file_path.contains("/target/") ||
    // Test files
    file_path.contains("/test/") ||
    file_path.contains("/tests/") ||
    file_path.contains("/benches/") ||
    file_path.contains("/benchmarks/") ||
    file_path.contains("/examples/") ||
    file_path.contains("_test.") ||
    file_path.contains("test_") ||
    file_path.contains("_tests.") ||
    file_path.contains("tests.rs") ||
    file_path.contains("bench.rs") ||
    file_path.contains("googletest") ||
    file_path.contains("googlemock") ||
    file_path.contains("gtest") ||
    file_path.contains("gmock")
}

/// Detect which component a file belongs to
fn detect_component(file_path: &str) -> String {
    // Use centralized exclusion logic
    if should_exclude_from_coverage(file_path) {
        // Determine the specific type of exclusion for categorization
        if file_path.contains(".cargo/registry") ||
           file_path.contains(".cargo/git") ||
           file_path.contains("/rustc-") ||
           file_path.contains("/private/tmp/") ||
           file_path.contains("/third_parties/") ||
           file_path.contains("third_parties/") {
            return "dependencies".to_string();
        } else {
            return "test_files".to_string();
        }
    }

    // Detect actual project components
    // Check tracer_backend first since it's more specific than tracer
    if file_path.contains("tracer_backend") {
        "tracer_backend".to_string()
    } else if file_path.contains("/tracer/") || file_path.ends_with("/tracer") {
        "tracer".to_string()
    } else if file_path.contains("query_engine") {
        "query_engine".to_string()
    } else if file_path.contains("mcp_server") {
        "mcp_server".to_string()
    } else if file_path.contains("coverage_helper") {
        "coverage_helper".to_string()
    } else {
        // For debugging - log unexpected files
        eprintln!("Warning: Uncategorized file in coverage: {}", file_path);
        "other".to_string()
    }
}

/// Get diff-cover metrics if available
fn get_diff_cover_metrics(_report_dir: &Path) -> Result<HashMap<String, String>> {
    let mut metrics = HashMap::new();
    
    // Set defaults
    metrics.insert("CHANGED_LINES_COVERAGE".to_string(), "N/A".to_string());
    metrics.insert("CHANGED_LINES_STATUS".to_string(), "warning".to_string());
    metrics.insert("CHANGED_LINES_COVERED".to_string(), "0".to_string());
    metrics.insert("CHANGED_LINES_TOTAL".to_string(), "0".to_string());
    metrics.insert("CHANGED_LINES_RESULT".to_string(), "No Changes".to_string());
    
    // TODO: Parse diff-cover output if available
    
    Ok(metrics)
}

/// Get current git commit
fn get_git_commit() -> Result<String> {
    let output = Command::new("git")
        .args(&["rev-parse", "--short", "HEAD"])
        .output()
        .context("Failed to get git commit")?;
    
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

/// Get current git branch
fn get_git_branch() -> Result<String> {
    let output = Command::new("git")
        .args(&["rev-parse", "--abbrev-ref", "HEAD"])
        .output()
        .context("Failed to get git branch")?;
    
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

/// Replace template placeholders with actual values
fn replace_placeholders(
    mut template: String,
    metrics: &HashMap<String, ComponentMetrics>,
    changed_lines: &HashMap<String, String>,
    commit: &str,
    branch: &str,
    timestamp: &str,
) -> String {
    // Git info
    template = template.replace("{{TIMESTAMP}}", timestamp);
    template = template.replace("{{COMMIT}}", commit);
    template = template.replace("{{BRANCH}}", branch);
    
    // Changed lines metrics
    for (key, value) in changed_lines {
        template = template.replace(&format!("{{{{{}}}}}", key), value);
    }
    
    // Total coverage with all metrics
    if let Some(total) = metrics.get("total") {
        // Calculate overall coverage (weighted average of all available metrics)
        let mut total_items = 0;
        let mut total_covered = 0;

        if total.lines_total > 0 {
            total_items += total.lines_total;
            total_covered += total.lines_covered;
        }
        if total.functions_total > 0 {
            total_items += total.functions_total;
            total_covered += total.functions_covered;
        }
        if total.branches_total > 0 {
            total_items += total.branches_total;
            total_covered += total.branches_covered;
        }

        let overall_coverage = if total_items > 0 {
            (total_covered as f64 / total_items as f64) * 100.0
        } else {
            0.0
        };

        let status = get_status(overall_coverage);
        template = template.replace("{{TOTAL_COVERAGE}}", &format!("{:.1}", overall_coverage));
        template = template.replace("{{TOTAL_STATUS}}", &status);
        template = template.replace("{{TOTAL_LINES_COVERED}}", &total.lines_covered.to_string());
        template = template.replace("{{TOTAL_LINES}}", &total.lines_total.to_string());

        // Function coverage
        if total.functions_total > 0 {
            template = template.replace("{{FUNC_COVERAGE}}", &format!("{:.1}", total.function_coverage));
            template = template.replace("{{FUNC_STATUS}}", &get_status(total.function_coverage));
            template = template.replace("{{FUNC_COVERED}}", &total.functions_covered.to_string());
            template = template.replace("{{FUNC_TOTAL}}", &total.functions_total.to_string());
        } else {
            template = template.replace("{{FUNC_COVERAGE}}", "N/A");
            template = template.replace("{{FUNC_STATUS}}", "warning");
            template = template.replace("{{FUNC_COVERED}}", "0");
            template = template.replace("{{FUNC_TOTAL}}", "0");
        }

        // Branch coverage
        if total.branches_total > 0 {
            template = template.replace("{{BRANCH_COVERAGE}}", &format!("{:.1}", total.branch_coverage));
            template = template.replace("{{BRANCH_STATUS}}", &get_status(total.branch_coverage));
            template = template.replace("{{BRANCH_COVERED}}", &total.branches_covered.to_string());
            template = template.replace("{{BRANCH_TOTAL}}", &total.branches_total.to_string());
        } else {
            template = template.replace("{{BRANCH_COVERAGE}}", "N/A");
            template = template.replace("{{BRANCH_STATUS}}", "warning");
            template = template.replace("{{BRANCH_COVERED}}", "0");
            template = template.replace("{{BRANCH_TOTAL}}", "0");
        }
    }
    
    // Component metrics
    for component in &["tracer", "tracer_backend", "query_engine", "mcp_server"] {
        let prefix = component.to_uppercase().replace("_", "_");
        let comp_metrics = metrics.get(*component).cloned().unwrap_or_default();
        let status = get_status(comp_metrics.line_coverage);
        let health = get_health(&comp_metrics);
        
        // Special handling for component prefixes
        let template_prefix = match *component {
            "tracer_backend" => "BACKEND",
            "query_engine" => "QUERY",
            "mcp_server" => "MCP",
            _ => &prefix,
        };
        
        template = template.replace(
            &format!("{{{{{}_LINE_COV}}}}", template_prefix),
            &format!("{:.1}", comp_metrics.line_coverage),
        );
        template = template.replace(
            &format!("{{{{{}_LINE_STATUS}}}}", template_prefix),
            &status,
        );

        // Function coverage for component
        if comp_metrics.functions_total > 0 {
            template = template.replace(
                &format!("{{{{{}_FUNC_COV}}}}", template_prefix),
                &format!("{:.1}", comp_metrics.function_coverage),
            );
        } else {
            template = template.replace(
                &format!("{{{{{}_FUNC_COV}}}}", template_prefix),
                "N/A",
            );
        }

        // Branch coverage for component
        if comp_metrics.branches_total > 0 {
            template = template.replace(
                &format!("{{{{{}_BRANCH_COV}}}}", template_prefix),
                &format!("{:.1}", comp_metrics.branch_coverage),
            );
        } else {
            template = template.replace(
                &format!("{{{{{}_BRANCH_COV}}}}", template_prefix),
                "N/A",
            );
        }

        template = template.replace(
            &format!("{{{{{}_STATUS}}}}", template_prefix),
            &status,
        );
        template = template.replace(
            &format!("{{{{{}_HEALTH}}}}", template_prefix),
            &health,
        );
    }
    
    template
}

/// Get status class based on coverage percentage
fn get_status(coverage: f64) -> String {
    if coverage >= 80.0 {
        "pass".to_string()
    } else if coverage >= 60.0 {
        "warning".to_string()
    } else {
        "fail".to_string()
    }
}

/// Get health status for a component
fn get_health(metrics: &ComponentMetrics) -> String {
    if metrics.line_coverage >= 80.0 {
        "Healthy".to_string()
    } else if metrics.line_coverage >= 60.0 {
        "Needs Work".to_string()
    } else {
        "Critical".to_string()
    }
}

/// Generate full HTML report using genhtml
fn generate_full_html_report(lcov_path: &Path, report_dir: &Path) -> Result<()> {
    println!("  Generating full HTML report with genhtml...");
    
    let full_report_dir = report_dir.join("full");
    fs::create_dir_all(&full_report_dir)?;
    
    let output = Command::new("genhtml")
        .args(&[
            lcov_path.to_str().unwrap(),
            "--output-directory",
            full_report_dir.to_str().unwrap(),
            "--title",
            "ADA Coverage Report",
            "--legend",
            "--show-details",
            "--demangle-cpp",
            "--ignore-errors",
            "deprecated",
        ])
        .output()
        .context("Failed to run genhtml")?;
    
    if output.status.success() {
        println!("  Full report generated: {}/index.html", full_report_dir.display());
    } else {
        let stderr = String::from_utf8_lossy(&output.stderr);
        println!("  Warning: genhtml failed: {}", stderr);
    }
    
    Ok(())
}

/// Generate diff-coverage HTML report
fn generate_diff_coverage_report(workspace: &Path, lcov_path: &Path, report_dir: &Path) -> Result<()> {
    println!("  Generating diff-coverage HTML report...");
    
    // Check if diff-cover is available
    if which::which("diff-cover").is_err() {
        println!("    diff-cover not found, skipping diff-coverage report");
        return Ok(());
    }
    
    let diff_report_path = report_dir.join("diff-coverage.html");
    
    // Run diff-cover with HTML output
    let output = Command::new("diff-cover")
        .args(&[
            lcov_path.to_str().unwrap(),
            "--html-report",
            diff_report_path.to_str().unwrap(),
            "--compare-branch=main",
            "--ignore-errors",
        ])
        .current_dir(workspace)
        .output()
        .context("Failed to run diff-cover")?;
    
    if output.status.success() {
        println!("    Diff-coverage report generated: {}", diff_report_path.display());
    } else {
        // Even if it fails (e.g., no changes), create a simple placeholder
        let placeholder_html = r#"<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Diff Coverage Report</title>
    <style>
        body { font-family: sans-serif; padding: 40px; background: #f5f5f5; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 30px; border-radius: 8px; }
        h1 { color: #333; }
        p { color: #666; line-height: 1.6; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📊 Diff Coverage Report</h1>
        <p>No coverage changes detected in this commit.</p>
        <p>This report shows coverage for lines that have changed compared to the main branch.</p>
        <p>When you modify source code, this report will show whether those specific lines are covered by tests.</p>
        <hr style="margin: 30px 0; border: none; border-top: 1px solid #eee;">
        <p><a href="index.html">← Back to Dashboard</a></p>
    </div>
</body>
</html>"#;
        fs::write(&diff_report_path, placeholder_html)?;
        println!("    Diff-coverage placeholder created");
    }
    
    Ok(())
}

/// Generate uncovered lines text file
fn generate_uncovered_lines_report(lcov_path: &Path, report_dir: &Path) -> Result<()> {
    println!("  Generating uncovered lines report...");
    
    let uncovered_path = report_dir.join("uncovered.txt");
    let mut uncovered_lines = Vec::new();
    
    if lcov_path.exists() {
        let content = fs::read_to_string(lcov_path)?;
        let mut current_file = String::new();
        
        for line in content.lines() {
            if line.starts_with("SF:") {
                current_file = line[3..].to_string();
            } else if line.starts_with("DA:") {
                // DA:line_number,hit_count
                let parts: Vec<&str> = line[3..].split(',').collect();
                if parts.len() == 2 {
                    if let (Ok(line_num), Ok(hits)) = (parts[0].parse::<u32>(), parts[1].parse::<u32>()) {
                        if hits == 0 && !current_file.is_empty() {
                            // Use centralized exclusion logic
                            if !should_exclude_from_coverage(&current_file) && current_file.contains("/Projects/ADA/") {
                                // Make path relative to workspace for readability
                                let relative_path = current_file
                                    .strip_prefix("/Users/wezzard/Projects/ADA/")
                                    .unwrap_or(&current_file);
                                uncovered_lines.push(format!("{}:{}", relative_path, line_num));
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Sort uncovered lines for better readability
    uncovered_lines.sort();
    
    // Write uncovered lines report
    let mut report = String::from("UNCOVERED LINES REPORT\n");
    report.push_str("======================\n\n");
    report.push_str(&format!("Total uncovered lines: {}\n\n", uncovered_lines.len()));
    
    if uncovered_lines.is_empty() {
        report.push_str("🎉 All lines are covered!\n");
    } else {
        report.push_str("File:Line\n");
        report.push_str("---------\n");
        for line in &uncovered_lines {
            report.push_str(line);
            report.push('\n');
        }
    }
    
    fs::write(&uncovered_path, report)?;
    println!("    Uncovered lines report generated: {}", uncovered_path.display());
    
    Ok(())
}

/// Generate coverage history HTML page
fn generate_history_report(workspace: &Path, report_dir: &Path) -> Result<()> {
    println!("  Generating coverage history report...");
    
    let history_path = report_dir.join("history.html");
    let trend_csv = workspace.join("target/coverage/coverage_trend.csv");
    
    let mut timestamps = Vec::new();
    let mut coverages = Vec::new();
    let mut commits = Vec::new();
    
    // Read trend data if available
    if trend_csv.exists() {
        let content = fs::read_to_string(&trend_csv)?;
        for (i, line) in content.lines().enumerate() {
            if i == 0 { continue; } // Skip header
            
            let parts: Vec<&str> = line.split(',').collect();
            if parts.len() >= 3 {
                timestamps.push(parts[0].to_string());
                commits.push(parts[1].to_string());
                
                // Parse coverage percentage
                if let Ok(cov) = parts[2].parse::<f64>() {
                    coverages.push(cov);
                } else {
                    coverages.push(0.0);
                }
            }
        }
    }
    
    // Generate history HTML with embedded chart
    let history_html = format!(r#"<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Coverage History</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body {{
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            padding: 20px;
            margin: 0;
        }}
        .container {{
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            border-radius: 12px;
            padding: 30px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.1);
        }}
        h1 {{
            color: #333;
            margin-bottom: 30px;
        }}
        .chart-container {{
            position: relative;
            height: 400px;
            margin: 30px 0;
        }}
        .stats {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            margin: 30px 0;
        }}
        .stat-card {{
            background: #f8f9fa;
            padding: 20px;
            border-radius: 8px;
            text-align: center;
        }}
        .stat-value {{
            font-size: 2em;
            font-weight: bold;
            color: #667eea;
        }}
        .stat-label {{
            color: #666;
            margin-top: 5px;
        }}
        .back-link {{
            display: inline-block;
            margin-top: 20px;
            color: #667eea;
            text-decoration: none;
        }}
        .back-link:hover {{
            text-decoration: underline;
        }}
    </style>
</head>
<body>
    <div class="container">
        <h1>📈 Coverage History</h1>
        
        <div class="stats">
            <div class="stat-card">
                <div class="stat-value">{}</div>
                <div class="stat-label">Data Points</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">{:.1}%</div>
                <div class="stat-label">Current Coverage</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">{:.1}%</div>
                <div class="stat-label">Average Coverage</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">{:.1}%</div>
                <div class="stat-label">Peak Coverage</div>
            </div>
        </div>
        
        <div class="chart-container">
            <canvas id="coverageChart"></canvas>
        </div>
        
        <script>
            const ctx = document.getElementById('coverageChart').getContext('2d');
            const chart = new Chart(ctx, {{
                type: 'line',
                data: {{
                    labels: {:?},
                    datasets: [{{
                        label: 'Coverage %',
                        data: {:?},
                        borderColor: '#667eea',
                        backgroundColor: 'rgba(102, 126, 234, 0.1)',
                        borderWidth: 2,
                        fill: true,
                        tension: 0.4
                    }}]
                }},
                options: {{
                    responsive: true,
                    maintainAspectRatio: false,
                    plugins: {{
                        legend: {{
                            display: false
                        }},
                        tooltip: {{
                            callbacks: {{
                                afterLabel: function(context) {{
                                    const commits = {:?};
                                    return 'Commit: ' + commits[context.dataIndex];
                                }}
                            }}
                        }}
                    }},
                    scales: {{
                        y: {{
                            beginAtZero: true,
                            max: 100,
                            ticks: {{
                                callback: function(value) {{
                                    return value + '%';
                                }}
                            }}
                        }}
                    }}
                }}
            }});
        </script>
        
        <a href="index.html" class="back-link">← Back to Dashboard</a>
    </div>
</body>
</html>"#,
        coverages.len(),
        coverages.last().unwrap_or(&0.0),
        if coverages.is_empty() { 0.0 } else { coverages.iter().sum::<f64>() / coverages.len() as f64 },
        coverages.iter().fold(0.0f64, |a, &b| a.max(b)),
        timestamps,
        coverages,
        commits
    );
    
    fs::write(&history_path, history_html)?;
    println!("    Coverage history report generated: {}", history_path.display());
    
    Ok(())
}