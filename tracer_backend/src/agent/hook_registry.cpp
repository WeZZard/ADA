#include <tracer_backend/agent/hook_registry.h>
#include <sstream>
#include <iomanip>

namespace ada {
namespace agent {

namespace {

// Helper to format UUID as string (e.g., "550E8400-E29B-41D4-A716-446655440000")
std::string format_uuid(const uint8_t uuid[16]) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) oss << '-';
        oss << std::setw(2) << static_cast<unsigned>(uuid[i]);
    }
    return oss.str();
}

// Helper to escape JSON string
std::string json_escape(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c; break;
        }
    }
    return oss.str();
}

} // anonymous namespace

// FNV-1a 32-bit (case-insensitive ASCII)
uint32_t fnv1a32_ci(const std::string& s) {
    const uint32_t FNV_OFFSET = 2166136261u;
    const uint32_t FNV_PRIME  = 16777619u;
    uint32_t h = FNV_OFFSET;
    for (unsigned char c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + 32);
        h ^= static_cast<uint32_t>(c);
        h *= FNV_PRIME;
    }
    // avoid 0 as module id to keep debugging simpler
    if (h == 0) h = 0x9e3779b9u;
    return h;
}

HookRegistry::HookRegistry() : modules_() {}

uint64_t HookRegistry::register_symbol(const std::string& module_path, const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& me = modules_[module_path];
    if (me.module_id == 0) {
        me.module_id = fnv1a32_ci(module_path);
        me.next_index = 1u;
    }
    return register_symbol_locked(me, symbol);
}

uint64_t HookRegistry::register_symbol_locked(ModuleEntry& me, const std::string& symbol) {
    auto it = me.name_to_index.find(symbol);
    if (it != me.name_to_index.end()) {
        return make_function_id(me.module_id, it->second);
    }
    uint32_t idx = me.next_index++;
    me.name_to_index.emplace(symbol, idx);
    return make_function_id(me.module_id, idx);
}

bool HookRegistry::get_id(const std::string& module_path, const std::string& symbol, uint64_t* out_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = modules_.find(module_path);
    if (it == modules_.end()) return false;
    const auto& me = it->second;
    auto it2 = me.name_to_index.find(symbol);
    if (it2 == me.name_to_index.end()) return false;
    if (out_id) *out_id = make_function_id(me.module_id, it2->second);
    return true;
}

uint32_t HookRegistry::get_module_id(const std::string& module_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = modules_.find(module_path);
    if (it == modules_.end()) return 0u;
    return it->second.module_id;
}

uint32_t HookRegistry::get_symbol_count(const std::string& module_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = modules_.find(module_path);
    if (it == modules_.end()) return 0u;
    return static_cast<uint32_t>(it->second.name_to_index.size());
}

void HookRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    modules_.clear();
}

uint32_t HookRegistry::get_or_create_module_id_locked(const std::string& module_path) {
    auto& me = modules_[module_path];
    if (me.module_id == 0) {
        me.module_id = fnv1a32_ci(module_path);
        me.next_index = 1u;
    }
    return me.module_id;
}

void HookRegistry::set_module_metadata(const std::string& module_path,
                                       uint64_t base_address,
                                       uint64_t size,
                                       const uint8_t uuid[16]) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = modules_.find(module_path);
    if (it == modules_.end()) {
        // Module not registered yet, create entry
        auto& me = modules_[module_path];
        me.module_id = fnv1a32_ci(module_path);
        me.next_index = 1u;
        me.base_address = base_address;
        me.size = size;
        std::memcpy(me.uuid, uuid, 16);
        me.metadata_set = true;
    } else {
        it->second.base_address = base_address;
        it->second.size = size;
        std::memcpy(it->second.uuid, uuid, 16);
        it->second.metadata_set = true;
    }
}

size_t HookRegistry::module_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return modules_.size();
}

std::string HookRegistry::export_to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;

    // Modules array
    oss << "\"modules\": [\n";
    bool first_module = true;
    for (const auto& kv : modules_) {
        const std::string& path = kv.first;
        const ModuleEntry& me = kv.second;

        if (!first_module) oss << ",\n";
        first_module = false;

        oss << "    {\n";
        oss << "      \"module_id\": " << me.module_id << ",\n";
        oss << "      \"path\": \"" << json_escape(path) << "\"";

        if (me.metadata_set) {
            oss << ",\n";
            oss << "      \"base_address\": \"0x" << std::hex << me.base_address << std::dec << "\",\n";
            oss << "      \"size\": " << me.size << ",\n";
            oss << "      \"uuid\": \"" << format_uuid(me.uuid) << "\"";
        }
        oss << "\n    }";
    }
    oss << "\n  ],\n";

    // Symbols array
    oss << "  \"symbols\": [\n";
    bool first_symbol = true;
    for (const auto& kv : modules_) {
        const ModuleEntry& me = kv.second;
        for (const auto& sym_kv : me.name_to_index) {
            const std::string& symbol_name = sym_kv.first;
            uint32_t symbol_index = sym_kv.second;
            uint64_t function_id = make_function_id(me.module_id, symbol_index);

            if (!first_symbol) oss << ",\n";
            first_symbol = false;

            oss << "    {\n";
            oss << "      \"function_id\": \"0x" << std::hex << std::setw(16) << std::setfill('0') << function_id << std::dec << "\",\n";
            oss << "      \"module_id\": " << me.module_id << ",\n";
            oss << "      \"symbol_index\": " << symbol_index << ",\n";
            oss << "      \"name\": \"" << json_escape(symbol_name) << "\"\n";
            oss << "    }";
        }
    }
    oss << "\n  ]";

    return oss.str();
}

} // namespace agent
} // namespace ada

