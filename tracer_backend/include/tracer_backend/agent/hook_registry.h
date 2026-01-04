// Hook registry assigns stable function IDs for (module, symbol) pairs and
// maintains a mapping for quick lookup. It is independent from Frida.
//
// Extended with module metadata (base_address, size, UUID) for symbol
// resolution at query time. See BH-011 Symbol Resolution spec.

#ifndef ADA_HOOK_REGISTRY_H
#define ADA_HOOK_REGISTRY_H

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace ada {
namespace agent {

// Compose a 64-bit function id from module id and per-module symbol index.
static inline uint64_t make_function_id(uint32_t module_id, uint32_t symbol_index) {
    return (static_cast<uint64_t>(module_id) << 32) | static_cast<uint64_t>(symbol_index);
}

// 32-bit FNV-1a hash for module ids (case-insensitive on ASCII letters).
uint32_t fnv1a32_ci(const std::string& s);

class HookRegistry {
public:
    HookRegistry();

    // Register a symbol for a module path and return its 64-bit function id.
    // The module id is derived from the path via fnv1a32_ci(). Each new
    // symbol in the module receives a monotonically increasing index starting
    // at 1. Re-registering the same symbol returns the previous id.
    uint64_t register_symbol(const std::string& module_path, const std::string& symbol);

    // Query helpers
    bool get_id(const std::string& module_path, const std::string& symbol, uint64_t* out_id) const;
    uint32_t get_module_id(const std::string& module_path) const;
    uint32_t get_symbol_count(const std::string& module_path) const;

    // Module metadata for symbol resolution (BH-011)
    // Call this after registering symbols to associate runtime metadata.
    void set_module_metadata(const std::string& module_path,
                            uint64_t base_address,
                            uint64_t size,
                            const uint8_t uuid[16]);

    // Export all modules and symbols to JSON for manifest persistence.
    // Returns JSON object with "modules" and "symbols" arrays.
    std::string export_to_json() const;

    // Get module count (for testing/debugging)
    size_t module_count() const;

    // Reset (tests only)
    void clear();

private:
    struct ModuleEntry {
        uint32_t module_id;
        uint32_t next_index;
        std::unordered_map<std::string, uint32_t> name_to_index;

        // Runtime metadata for symbol resolution (optional, set via set_module_metadata)
        uint64_t base_address = 0;
        uint64_t size = 0;
        uint8_t uuid[16] = {0};
        bool metadata_set = false;
    };

    uint64_t register_symbol_locked(ModuleEntry& me, const std::string& symbol);

    uint32_t get_or_create_module_id_locked(const std::string& module_path);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, ModuleEntry> modules_;
};

} // namespace agent
} // namespace ada

#endif // ADA_HOOK_REGISTRY_H

