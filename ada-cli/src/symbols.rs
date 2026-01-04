//! Symbol resolution commands.
//!
//! Provides CLI commands for:
//! - Resolving function_ids to symbol names
//! - Locating dSYM bundles
//! - Dumping symbol tables

use crate::ffi::{self, SymbolResolver};
use clap::Subcommand;
use std::path::Path;

#[derive(Subcommand)]
pub enum SymbolsCommands {
    /// Resolve a function_id to symbol information
    Resolve {
        /// Path to session directory
        session: String,

        /// Function ID to resolve (hex, e.g., 0x0000001c00000001)
        #[arg(value_parser = parse_function_id)]
        function_id: u64,
    },

    /// Locate the dSYM bundle for a binary by UUID
    LocateDsym {
        /// UUID string (e.g., 550E8400-E29B-41D4-A716-446655440000)
        uuid: String,
    },

    /// Demangle a symbol name
    Demangle {
        /// Mangled symbol name
        name: String,
    },

    /// Dump all symbols from a session
    Dump {
        /// Path to session directory
        session: String,

        /// Output format (text, json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },

    /// Show session information
    Info {
        /// Path to session directory
        session: String,
    },
}

/// Parse a function_id from hex string (with or without 0x prefix)
fn parse_function_id(s: &str) -> Result<u64, String> {
    let s = s.trim();
    let s = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")).unwrap_or(s);
    u64::from_str_radix(s, 16).map_err(|e| format!("Invalid function_id: {}", e))
}

pub fn run(cmd: SymbolsCommands) -> anyhow::Result<()> {
    match cmd {
        SymbolsCommands::Resolve { session, function_id } => {
            resolve_symbol(&session, function_id)
        }
        SymbolsCommands::LocateDsym { uuid } => {
            locate_dsym(&uuid)
        }
        SymbolsCommands::Demangle { name } => {
            demangle_symbol(&name)
        }
        SymbolsCommands::Dump { session, format } => {
            dump_symbols(&session, &format)
        }
        SymbolsCommands::Info { session } => {
            show_info(&session)
        }
    }
}

fn resolve_symbol(session: &str, function_id: u64) -> anyhow::Result<()> {
    let resolver = SymbolResolver::new(session)
        .ok_or_else(|| anyhow::anyhow!("Failed to open session: {}", session))?;

    match resolver.resolve(function_id) {
        Ok(symbol) => {
            println!("Function ID: 0x{:016x}", symbol.function_id);
            println!("Name:        {}", symbol.name_demangled);
            if symbol.name_mangled != symbol.name_demangled {
                println!("Mangled:     {}", symbol.name_mangled);
            }
            if let Some(module) = &symbol.module_path {
                println!("Module:      {}", module);
            }
            if let Some(file) = &symbol.source_file {
                print!("Source:      {}", file);
                if symbol.source_line > 0 {
                    print!(":{}", symbol.source_line);
                    if symbol.source_column > 0 {
                        print!(":{}", symbol.source_column);
                    }
                }
                println!();
            }
        }
        Err(ffi::SymbolResolveResult::NotFound) => {
            eprintln!("Symbol not found for function_id: 0x{:016x}", function_id);
            std::process::exit(1);
        }
        Err(e) => {
            anyhow::bail!("Resolution failed: {:?}", e);
        }
    }

    Ok(())
}

fn locate_dsym(uuid: &str) -> anyhow::Result<()> {
    match ffi::locate_dsym(uuid) {
        Some(path) => {
            println!("{}", path);
        }
        None => {
            eprintln!("dSYM not found for UUID: {}", uuid);
            std::process::exit(1);
        }
    }
    Ok(())
}

fn demangle_symbol(name: &str) -> anyhow::Result<()> {
    let demangled = ffi::demangle(name);
    println!("{}", demangled);
    Ok(())
}

fn dump_symbols(session: &str, format: &str) -> anyhow::Result<()> {
    // Read the manifest.json directly for full dump
    let manifest_path = Path::new(session).join("manifest.json");
    let content = std::fs::read_to_string(&manifest_path)
        .map_err(|e| anyhow::anyhow!("Failed to read manifest: {}", e))?;

    if format == "json" {
        // Just output the relevant parts of the manifest
        let json: serde_json::Value = serde_json::from_str(&content)?;

        let output = serde_json::json!({
            "modules": json.get("modules"),
            "symbols": json.get("symbols"),
            "format_version": json.get("format_version"),
        });

        println!("{}", serde_json::to_string_pretty(&output)?);
    } else {
        // Text format
        let json: serde_json::Value = serde_json::from_str(&content)?;

        // Print modules
        if let Some(modules) = json.get("modules").and_then(|m| m.as_array()) {
            println!("=== Modules ({}) ===\n", modules.len());
            for module in modules {
                let id = module.get("module_id").and_then(|v| v.as_u64()).unwrap_or(0);
                let path = module.get("path").and_then(|v| v.as_str()).unwrap_or("?");
                let uuid = module.get("uuid").and_then(|v| v.as_str()).unwrap_or("");
                println!("  [{:08x}] {}", id, path);
                if !uuid.is_empty() {
                    println!("             UUID: {}", uuid);
                }
            }
            println!();
        }

        // Print symbols
        if let Some(symbols) = json.get("symbols").and_then(|s| s.as_array()) {
            println!("=== Symbols ({}) ===\n", symbols.len());
            for symbol in symbols {
                let fid = symbol.get("function_id").and_then(|v| v.as_str()).unwrap_or("0");
                let name = symbol.get("name").and_then(|v| v.as_str()).unwrap_or("?");
                let demangled = ffi::demangle(name);
                println!("  {} {}", fid, demangled);
            }
        }
    }

    Ok(())
}

fn show_info(session: &str) -> anyhow::Result<()> {
    let resolver = SymbolResolver::new(session)
        .ok_or_else(|| anyhow::anyhow!("Failed to open session: {}", session))?;

    println!("Session: {}", session);
    println!("Format:  {}", resolver.format_version().unwrap_or_else(|| "unknown".to_string()));
    println!("Modules: {}", resolver.module_count());
    println!("Symbols: {}", resolver.symbol_count());

    Ok(())
}
