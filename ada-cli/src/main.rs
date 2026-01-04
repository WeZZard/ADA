//! ADA Command Line Interface
//!
//! Provides commands for tracing, symbol resolution, and trace analysis.
//!
//! # Commands
//!
//! - `ada trace` - Manage tracing sessions
//! - `ada symbols` - Symbol resolution and dSYM management
//! - `ada query` - Query trace data

mod ffi;
mod symbols;
mod trace;

use clap::{Parser, Subcommand};
use tracing_subscriber::{fmt, EnvFilter};

/// ADA - Application Dynamic Analysis
///
/// A performance tracing and analysis toolkit for macOS applications.
#[derive(Parser)]
#[command(name = "ada")]
#[command(author, version, about, long_about = None)]
#[command(propagate_version = true)]
struct Cli {
    /// Enable verbose output
    #[arg(short, long, global = true)]
    verbose: bool,

    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Manage tracing sessions
    #[command(subcommand)]
    Trace(trace::TraceCommands),

    /// Symbol resolution and dSYM management
    #[command(subcommand)]
    Symbols(symbols::SymbolsCommands),

    /// Query trace data (coming soon)
    Query {
        /// Path to session directory
        session: String,

        /// Query string (natural language or filter expression)
        query: String,
    },
}

fn main() -> anyhow::Result<()> {
    // Initialize logging
    let filter = EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info"));
    fmt::Subscriber::builder()
        .with_env_filter(filter)
        .with_target(false)
        .init();

    let cli = Cli::parse();

    if cli.verbose {
        tracing::info!("Verbose mode enabled");
    }

    match cli.command {
        Commands::Trace(cmd) => trace::run(cmd),
        Commands::Symbols(cmd) => symbols::run(cmd),
        Commands::Query { session, query } => {
            println!("Query feature coming soon!");
            println!("Session: {}", session);
            println!("Query: {}", query);
            Ok(())
        }
    }
}
