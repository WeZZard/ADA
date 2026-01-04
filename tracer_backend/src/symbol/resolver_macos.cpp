// Symbol Resolver - macOS Implementation
//
// Implements the SymbolResolver class for macOS.
// Parses manifest.json and resolves function_ids to symbols.

#include "symbol_resolver_internal.h"

#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>

// =============================================================================
// Simple JSON Parsing Helpers
// =============================================================================

namespace {

// Skip whitespace
const char* skip_ws(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    return p;
}

// Parse a JSON string value (assumes p points to opening quote)
// Returns pointer past closing quote, or nullptr on error
const char* parse_string(const char* p, std::string& out) {
    if (*p != '"') return nullptr;
    ++p;
    out.clear();
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) {
            ++p;
            switch (*p) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                default: out += *p; break;
            }
        } else {
            out += *p;
        }
        ++p;
    }
    if (*p != '"') return nullptr;
    return p + 1;
}

// Parse a number (integer or hex with 0x prefix)
const char* parse_number(const char* p, uint64_t& out) {
    out = 0;
    bool is_hex = false;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        is_hex = true;
        p += 2;
    }

    if (is_hex) {
        while ((*p >= '0' && *p <= '9') ||
               (*p >= 'a' && *p <= 'f') ||
               (*p >= 'A' && *p <= 'F')) {
            out *= 16;
            if (*p >= '0' && *p <= '9') out += static_cast<uint64_t>(*p - '0');
            else if (*p >= 'a' && *p <= 'f') out += static_cast<uint64_t>(*p - 'a' + 10);
            else out += static_cast<uint64_t>(*p - 'A' + 10);
            ++p;
        }
    } else {
        while (*p >= '0' && *p <= '9') {
            out = out * 10 + static_cast<uint64_t>(*p - '0');
            ++p;
        }
    }
    return p;
}

// Find a key in JSON object (very simple, assumes well-formed JSON)
const char* find_key(const char* json, const char* key) {
    std::string search = "\"";
    search += key;
    search += "\"";

    const char* p = strstr(json, search.c_str());
    if (!p) return nullptr;

    p += search.length();
    p = skip_ws(p);
    if (*p != ':') return nullptr;
    return skip_ws(p + 1);
}

} // anonymous namespace

// =============================================================================
// SymbolResolver Implementation
// =============================================================================

SymbolResolver::SymbolResolver() = default;
SymbolResolver::~SymbolResolver() = default;

bool SymbolResolver::load_manifest(const char* session_path) {
    session_path_ = session_path;

    // Build manifest path
    std::string manifest_path = session_path_;
    if (!manifest_path.empty() && manifest_path.back() != '/') {
        manifest_path += '/';
    }
    manifest_path += "manifest.json";

    // Read file content
    std::ifstream file(manifest_path);
    if (!file) {
        errno = ENOENT;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    return parse_manifest_json(content);
}

bool SymbolResolver::parse_manifest_json(const std::string& json_content) {
    const char* json = json_content.c_str();

    // Parse format_version
    const char* p = find_key(json, "format_version");
    if (p) {
        parse_string(p, format_version_);
    }

    // Parse modules array
    p = find_key(json, "modules");
    if (p && *p == '[') {
        ++p;
        while (*p) {
            p = skip_ws(p);
            if (*p == ']') break;
            if (*p == ',') { ++p; continue; }
            if (*p != '{') break;

            // Parse module object
            const char* obj_end = strchr(p, '}');
            if (!obj_end) break;

            std::string obj(p, static_cast<size_t>(obj_end - p + 1));
            ada::symbol::ModuleInfo mod;

            // Parse module_id
            const char* val = find_key(obj.c_str(), "module_id");
            if (val) {
                uint64_t id;
                parse_number(val, id);
                mod.module_id = static_cast<uint32_t>(id);
            }

            // Parse path
            val = find_key(obj.c_str(), "path");
            if (val) {
                parse_string(val, mod.path);
            }

            // Parse base_address (may be hex string or number)
            val = find_key(obj.c_str(), "base_address");
            if (val) {
                if (*val == '"') {
                    std::string addr_str;
                    parse_string(val, addr_str);
                    parse_number(addr_str.c_str(), mod.base_address);
                } else {
                    parse_number(val, mod.base_address);
                }
            }

            // Parse size
            val = find_key(obj.c_str(), "size");
            if (val) {
                parse_number(val, mod.size);
            }

            // Parse uuid
            val = find_key(obj.c_str(), "uuid");
            if (val) {
                parse_string(val, mod.uuid);
            }

            if (mod.module_id != 0) {
                modules_[mod.module_id] = std::move(mod);
            }

            p = obj_end + 1;
        }
    }

    // Parse symbols array
    p = find_key(json, "symbols");
    if (p && *p == '[') {
        ++p;
        while (*p) {
            p = skip_ws(p);
            if (*p == ']') break;
            if (*p == ',') { ++p; continue; }
            if (*p != '{') break;

            // Parse symbol object
            const char* obj_end = strchr(p, '}');
            if (!obj_end) break;

            std::string obj(p, static_cast<size_t>(obj_end - p + 1));
            ada::symbol::SymbolInfo sym;

            // Parse function_id (may be hex string)
            const char* val = find_key(obj.c_str(), "function_id");
            if (val) {
                if (*val == '"') {
                    std::string fid_str;
                    parse_string(val, fid_str);
                    parse_number(fid_str.c_str(), sym.function_id);
                } else {
                    parse_number(val, sym.function_id);
                }
            }

            // Parse module_id
            val = find_key(obj.c_str(), "module_id");
            if (val) {
                uint64_t id;
                parse_number(val, id);
                sym.module_id = static_cast<uint32_t>(id);
            }

            // Parse symbol_index
            val = find_key(obj.c_str(), "symbol_index");
            if (val) {
                uint64_t idx;
                parse_number(val, idx);
                sym.symbol_index = static_cast<uint32_t>(idx);
            }

            // Parse name
            val = find_key(obj.c_str(), "name");
            if (val) {
                parse_string(val, sym.name);
            }

            if (sym.function_id != 0) {
                symbols_[sym.function_id] = std::move(sym);
            }

            p = obj_end + 1;
        }
    }

    return true;
}

int SymbolResolver::resolve(uint64_t function_id, ResolvedSymbol* out) {
    // Clear output
    std::memset(out, 0, sizeof(ResolvedSymbol));
    out->function_id = function_id;

    // Look up symbol
    auto sym_it = symbols_.find(function_id);
    if (sym_it == symbols_.end()) {
        return SYMBOL_RESOLVE_NOT_FOUND;
    }

    const ada::symbol::SymbolInfo& sym = sym_it->second;

    // Set mangled name
    out->name_mangled = sym.name.c_str();

    // Demangle if not already cached
    if (sym.demangled_name.empty()) {
        char* demangled = ada::symbol::demangle(sym.name.c_str());
        if (demangled) {
            const_cast<ada::symbol::SymbolInfo&>(sym).demangled_name = demangled;
            free(demangled);
        } else {
            const_cast<ada::symbol::SymbolInfo&>(sym).demangled_name = sym.name;
        }
    }
    out->name_demangled = sym.demangled_name.c_str();

    // Look up module
    auto mod_it = modules_.find(sym.module_id);
    if (mod_it != modules_.end()) {
        out->module_path = mod_it->second.path.c_str();
    }

    // TODO: DWARF source location lookup (Phase 3)
    // For now, source info is unavailable
    out->source_file = nullptr;
    out->source_line = 0;
    out->source_column = 0;

    return SYMBOL_RESOLVE_OK;
}

const ada::symbol::ModuleInfo* SymbolResolver::find_module(uint32_t module_id) const {
    auto it = modules_.find(module_id);
    return (it != modules_.end()) ? &it->second : nullptr;
}

std::string SymbolResolver::lookup_dsym(uint32_t module_id) const {
    // Check cache first
    auto cache_it = dsym_cache_.find(module_id);
    if (cache_it != dsym_cache_.end()) {
        return cache_it->second;
    }

    // Look up module
    const ada::symbol::ModuleInfo* mod = find_module(module_id);
    if (!mod || mod->uuid.empty()) {
        dsym_cache_[module_id] = "";
        return "";
    }

    // Try to locate dSYM
    char* dsym_path = ada::symbol::locate_dsym_by_uuid(mod->uuid.c_str());
    std::string result;
    if (dsym_path) {
        result = dsym_path;
        free(dsym_path);
    }

    dsym_cache_[module_id] = result;
    return result;
}
