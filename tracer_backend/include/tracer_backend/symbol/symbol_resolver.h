// Symbol Resolver - C ABI for cross-language symbol resolution
//
// Platform-agnostic API for resolving function_id to human-readable symbols.
// Implementations are platform-specific:
//   - macOS: libdwarf + CoreSymbolication + Spotlight dSYM discovery
//   - Linux: TODO (libdw)
//   - Windows: TODO (Rust pdb crate)
//
// This header defines the C ABI shell that platform implementations must fulfill.

#ifndef ADA_SYMBOL_RESOLVER_H
#define ADA_SYMBOL_RESOLVER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Opaque Handle
// =============================================================================

// Opaque symbol resolver handle.
// Created from a session directory containing manifest.json with symbol table.
typedef struct SymbolResolver SymbolResolver;

// =============================================================================
// Data Structures
// =============================================================================

// Resolved symbol information.
// All string pointers are owned by the resolver and valid until:
//   - The next call to symbol_resolver_resolve[_batch] with the same resolver
//   - The resolver is destroyed
typedef struct {
    uint64_t function_id;          // The function_id that was resolved

    const char* name_mangled;      // Mangled symbol name (from manifest)
    const char* name_demangled;    // Demangled name (C++/Swift), or same as mangled
    const char* module_path;       // Full path to the module containing this symbol

    // DWARF source location (requires dSYM)
    const char* source_file;       // Source file path, or NULL if unavailable
    uint32_t source_line;          // Source line number, or 0 if unavailable
    uint32_t source_column;        // Source column, or 0 if unavailable
} ResolvedSymbol;

// Result codes for resolution operations
typedef enum {
    SYMBOL_RESOLVE_OK           =  0,   // Successfully resolved
    SYMBOL_RESOLVE_NOT_FOUND    = -1,   // function_id not in symbol table
    SYMBOL_RESOLVE_NO_DSYM      = -2,   // Symbol found but no dSYM for source info
    SYMBOL_RESOLVE_ERROR        = -3,   // Internal error (check errno)
    SYMBOL_RESOLVE_INVALID_ARG  = -4    // NULL or invalid argument
} SymbolResolveResult;

// =============================================================================
// Lifecycle
// =============================================================================

// Create a symbol resolver from a session directory.
// The session directory must contain a manifest.json with symbol table.
//
// Parameters:
//   session_path: Path to the session directory (e.g., "/tmp/ada_session/pid_1234")
//
// Returns:
//   New resolver handle on success, NULL on error (check errno)
SymbolResolver* symbol_resolver_create(const char* session_path);

// Destroy a symbol resolver and free all resources.
// Safe to call with NULL.
void symbol_resolver_destroy(SymbolResolver* resolver);

// =============================================================================
// Symbol Resolution
// =============================================================================

// Resolve a single function_id to symbol information.
//
// Parameters:
//   resolver: The resolver handle
//   function_id: The function_id from trace events (module_id << 32 | symbol_index)
//   out: Output structure to receive resolved symbol info
//
// Returns:
//   SYMBOL_RESOLVE_OK on success
//   SYMBOL_RESOLVE_NOT_FOUND if function_id is not in the symbol table
//   SYMBOL_RESOLVE_NO_DSYM if symbol found but source info unavailable
//   SYMBOL_RESOLVE_ERROR on internal error
//   SYMBOL_RESOLVE_INVALID_ARG if resolver or out is NULL
int symbol_resolver_resolve(
    SymbolResolver* resolver,
    uint64_t function_id,
    ResolvedSymbol* out
);

// Resolve multiple function_ids in batch for efficiency.
// Batch resolution amortizes dSYM lookup overhead.
//
// Parameters:
//   resolver: The resolver handle
//   function_ids: Array of function_ids to resolve
//   count: Number of function_ids in the array
//   out: Output array of ResolvedSymbol (must have space for 'count' entries)
//
// Returns:
//   Number of successfully resolved symbols (0 to count)
//   -1 on invalid arguments
//
// Note: Check individual out[i].function_id to determine which succeeded.
//       Failed resolutions will have function_id = 0.
int symbol_resolver_resolve_batch(
    SymbolResolver* resolver,
    const uint64_t* function_ids,
    size_t count,
    ResolvedSymbol* out
);

// =============================================================================
// dSYM Discovery (macOS only)
// =============================================================================

// Locate the dSYM bundle for a binary by its UUID.
// Uses platform-specific discovery:
//   - macOS: Spotlight (mdfind), adjacent paths, DerivedData
//   - Linux: N/A (uses .debug sections or separate .debug files)
//   - Windows: N/A (uses .pdb files)
//
// Parameters:
//   uuid: UUID string in format "550E8400-E29B-41D4-A716-446655440000"
//
// Returns:
//   Newly allocated path to dSYM bundle (caller must free with free())
//   NULL if not found or not applicable to platform
char* symbol_resolver_locate_dsym(const char* uuid);

// =============================================================================
// Demangling
// =============================================================================

// Demangle a symbol name.
// Automatically detects and handles:
//   - C++ mangled names (_Z...)
//   - Swift mangled names (_$s... or $s...)
//   - Objective-C names (passed through unchanged)
//
// Parameters:
//   mangled: The mangled symbol name
//
// Returns:
//   Newly allocated demangled string (caller must free with free())
//   Copy of input if not a mangled name or demangling fails
char* symbol_resolver_demangle(const char* mangled);

// =============================================================================
// Query Helpers
// =============================================================================

// Get the number of modules in the symbol table.
size_t symbol_resolver_module_count(const SymbolResolver* resolver);

// Get the total number of symbols across all modules.
size_t symbol_resolver_symbol_count(const SymbolResolver* resolver);

// Get session metadata.
// Returns the format version string (e.g., "2.1") or NULL if unavailable.
const char* symbol_resolver_get_format_version(const SymbolResolver* resolver);

#ifdef __cplusplus
}
#endif

#endif // ADA_SYMBOL_RESOLVER_H
