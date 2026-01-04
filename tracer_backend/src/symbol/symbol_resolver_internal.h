// Symbol Resolver - Internal header
//
// Defines the SymbolResolver class and platform-specific helper declarations.
// This header is not part of the public API.

#ifndef ADA_SYMBOL_RESOLVER_INTERNAL_H
#define ADA_SYMBOL_RESOLVER_INTERNAL_H

#include <tracer_backend/symbol/symbol_resolver.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

// =============================================================================
// Internal Data Structures
// =============================================================================

namespace ada {
namespace symbol {

// Module entry from manifest.json
struct ModuleInfo {
    uint32_t module_id;
    std::string path;
    uint64_t base_address;
    uint64_t size;
    std::string uuid;  // Formatted UUID string
};

// Symbol entry from manifest.json
struct SymbolInfo {
    uint64_t function_id;
    uint32_t module_id;
    uint32_t symbol_index;
    std::string name;           // Mangled name
    std::string demangled_name; // Cached demangled name
};

// Platform-specific helper declarations
char* locate_dsym_by_uuid(const char* uuid);
char* demangle(const char* mangled);

} // namespace symbol
} // namespace ada

// =============================================================================
// SymbolResolver Class
// =============================================================================

// The opaque SymbolResolver handle is actually this C++ class
struct SymbolResolver {
public:
    SymbolResolver();
    ~SymbolResolver();

    // Load manifest.json from session directory
    bool load_manifest(const char* session_path);

    // Resolve a function_id to symbol information
    int resolve(uint64_t function_id, ResolvedSymbol* out);

    // Query methods
    size_t module_count() const { return modules_.size(); }
    size_t symbol_count() const { return symbols_.size(); }
    const char* format_version() const {
        return format_version_.empty() ? nullptr : format_version_.c_str();
    }

private:
    // Parsed manifest data
    std::unordered_map<uint32_t, ada::symbol::ModuleInfo> modules_;  // module_id -> ModuleInfo
    std::unordered_map<uint64_t, ada::symbol::SymbolInfo> symbols_;  // function_id -> SymbolInfo
    std::string format_version_;
    std::string session_path_;

    // Cached strings for ResolvedSymbol output (valid until next resolve call)
    mutable std::string cached_demangled_;
    mutable std::string cached_module_path_;
    mutable std::string cached_source_file_;

    // dSYM cache: module_id -> dSYM path (or empty if not found)
    mutable std::unordered_map<uint32_t, std::string> dsym_cache_;

    // Helper methods
    bool parse_manifest_json(const std::string& json_content);
    const ada::symbol::ModuleInfo* find_module(uint32_t module_id) const;
    std::string lookup_dsym(uint32_t module_id) const;
};

#endif // ADA_SYMBOL_RESOLVER_INTERNAL_H
