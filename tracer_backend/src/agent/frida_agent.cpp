#include "agent_internal.h"

// C++ headers
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>


// System headers
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#include <mach/mach_init.h>
#include <mach/thread_info.h>
#include <pthread/pthread.h>
#endif

// C headers - must be in extern "C" block
extern "C" {
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/utils/shared_memory.h>
// Thread registry API for per-thread lanes
#include <tracer_backend/utils/thread_registry.h>
// Ring pool for swap-on-overflow
#include <tracer_backend/utils/ring_pool.h>
// SHM directory mapping helpers (M1_E1_I8)
#include <tracer_backend/utils/shm_directory.h>
#include <tracer_backend/metrics/thread_metrics.h>
}

// Include C++ implementation headers
#include "../utils/ring_buffer_private.h"
#include <tracer_backend/agent/exclude_list.h>
#include <tracer_backend/agent/hook_registry.h>
#include <tracer_backend/agent/comprehensive_hooks.h>
#include <tracer_backend/agent/dso_management.h>
#include <tracer_backend/agent/module_uuid.h>

// #define ADA_MINIMAL_HOOKS 1  // Disabled to enable full event capture

// Forward declare the C callbacks
extern "C" {
void on_enter_callback(GumInvocationContext* ic, gpointer user_data);
void on_leave_callback(GumInvocationContext* ic, gpointer user_data);

__attribute__((visibility("default")))
void agent_init(const gchar* data, gint data_size);

__attribute__((destructor))
static void agent_deinit();
}

namespace ada {
namespace internal {

// ============================================================================
// Static Variables
// ============================================================================

static std::unique_ptr<AgentContext> g_agent_context;
static std::mutex g_context_mutex;
static pthread_key_t g_tls_key;
static pthread_once_t g_tls_once = PTHREAD_ONCE_INIT;

// Verbose logging gate (default: off). Enable with ADA_AGENT_VERBOSE=1
static bool g_agent_verbose = [](){
    const char* e = getenv("ADA_AGENT_VERBOSE");
    return e && e[0] != '\0' && e[0] != '0';
}();

// Global flag to prevent hooks from running during shutdown
static std::atomic<bool> g_agent_shutting_down{false};

// ============================================================================
// Logging System
// ============================================================================

enum LogCategory {
    LOG_NONE          = 0,
    LOG_LIFECYCLE     = 1 << 0,  // 0x01 - Agent init, deinit, major state changes
    LOG_HOOK_INSTALL  = 1 << 1,  // 0x02 - Hook installation, planning, attachment
    LOG_HOOK_SUMMARY  = 1 << 2,  // 0x04 - Hook summary (success/fail counts)
    LOG_CALLBACKS     = 1 << 3,  // 0x08 - on_enter, on_leave function calls
    LOG_EVENTS        = 1 << 4,  // 0x10 - Event capture, ring buffer writes
    LOG_ALL           = 0xFFFFFFFF
};

// Log level control - bitmask of enabled categories
// Default: lifecycle and hooks enabled, callbacks and events disabled
static uint32_t g_log_enabled = LOG_LIFECYCLE | LOG_HOOK_INSTALL | LOG_HOOK_SUMMARY;

[[maybe_unused]]
static void agent_log_cat(uint32_t category, const char* format, ...) {
    // Check if this category is enabled
    if (!(g_log_enabled & category)) {
        return;
    }
    
    // Prevent recursion
    static thread_local bool in_log = false;
    if (in_log) return;
    in_log = true;

    char buffer[1024];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0) {
        if ((size_t)len > sizeof(buffer)) len = sizeof(buffer);
        write(STDERR_FILENO, buffer, len);
    }
    
    in_log = false;
}

// Convenience macros for each category
#ifndef NDEBUG
#define LOG_LIFECYCLE(...) agent_log_cat(ada::internal::LOG_LIFECYCLE, __VA_ARGS__)
#define LOG_HOOK_INSTALL(...) agent_log_cat(ada::internal::LOG_HOOK_INSTALL, __VA_ARGS__)
#define LOG_HOOK_SUMMARY(...) agent_log_cat(ada::internal::LOG_HOOK_SUMMARY, __VA_ARGS__)
#define LOG_CALLBACKS(...) agent_log_cat(ada::internal::LOG_CALLBACKS, __VA_ARGS__)
#define LOG_EVENTS(...) agent_log_cat(ada::internal::LOG_EVENTS, __VA_ARGS__)
#else
#define LOG_LIFECYCLE(...) do {} while(0)
#define LOG_HOOK_INSTALL(...) do {} while(0)
#define LOG_HOOK_SUMMARY(...) do {} while(0)
#define LOG_CALLBACKS(...) do {} while(0)
#define LOG_EVENTS(...) do {} while(0)
#endif

// For initialization parsing
static uint32_t g_host_pid = UINT32_MAX;
static uint32_t g_session_id = UINT32_MAX;
static char g_exclude_csv[256] = {0};


static volatile sig_atomic_t g_segfault_occurred = 0;

// ============================================================================
// ThreadLocalData Implementation
// ============================================================================

ThreadLocalData::ThreadLocalData() 
    : call_depth_(0)
    , in_handler_(false)
    , reentrancy_attempts_(0) {
#ifdef __APPLE__
    thread_id_ = pthread_mach_thread_np(pthread_self());
#else
    thread_id_ = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pthread_self()));
#endif
}

// ============================================================================
// HookData Implementation
// ============================================================================

HookData::~HookData() {
    if (listener) {
        g_object_unref(listener);
        listener = nullptr;
    }
}

// ============================================================================
// SharedMemoryRef Implementation  
// ============================================================================

SharedMemoryRef::~SharedMemoryRef() {
    cleanup();
}

void SharedMemoryRef::cleanup() {
    const char* msg_start = "[Agent] SharedMemoryRef::cleanup called\n";
    write(2, msg_start, strlen(msg_start));
    
    if (ref_) {
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "[Agent] SharedMemoryRef::cleanup destroying %p\n", ref_);
        write(2, buf, len);
        
        shared_memory_destroy(reinterpret_cast<::SharedMemoryRef>(ref_));
        
        const char* msg_done = "[Agent] SharedMemoryRef::cleanup destroyed\n";
        write(2, msg_done, strlen(msg_done));
        
        ref_ = nullptr;
    } else {
        const char* msg_null = "[Agent] SharedMemoryRef::cleanup skipped (ref is null)\n";
        write(2, msg_null, strlen(msg_null));
    }
}

void* SharedMemoryRef::get_address() const {
    if (!ref_) return nullptr;
    return shared_memory_get_address(reinterpret_cast<::SharedMemoryRef>(ref_));
}

SharedMemoryRef SharedMemoryRef::open_unique(uint32_t role, uint32_t host_pid, 
                                            uint32_t session_id, size_t size) {
    // Convert role number to string
    const char* role_str = nullptr;
    switch(role) {
        case 0: role_str = "control"; break;
        case 1: role_str = "index"; break;
        case 2: role_str = "detail"; break;
        default: return SharedMemoryRef();
    }
    auto ref = shared_memory_open_unique(role_str, host_pid, session_id, size);
    return SharedMemoryRef(reinterpret_cast<SharedMemoryRef*>(ref));
}

// ============================================================================
// AgentContext Implementation
// ============================================================================

// Custom deleter for RingBuffer
static void ring_buffer_deleter(ada::internal::RingBuffer* rb) {
    const char* msg_start = "[Agent] ring_buffer_deleter called\n";
    write(2, msg_start, strlen(msg_start));

    if (rb) {
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "[Agent] ring_buffer_deleter destroying %p\n", rb);
        write(2, buf, len);
        
        ring_buffer_destroy(reinterpret_cast<::RingBuffer*>(rb));
        
        const char* msg_done = "[Agent] ring_buffer_deleter destroyed\n";
        write(2, msg_done, strlen(msg_done));
    } else {
        const char* msg_null = "[Agent] ring_buffer_deleter skipped (rb is null)\n";
        write(2, msg_null, strlen(msg_null));
    }
}

AgentContext::AgentContext()
    : index_ring_(nullptr, ring_buffer_deleter)
    , detail_ring_(nullptr, ring_buffer_deleter)
    , control_block_(nullptr)
    , interceptor_(nullptr)
    , num_hooks_attempted_(0)
    , num_hooks_successful_(0)
    , host_pid_(0)
    , session_id_(0)
    , module_base_(0)
    , events_emitted_(0)
    , reentrancy_blocked_(0)
    , stack_capture_failures_(0)
    , agent_mode_state_{} {
    // Initialize agent mode to GLOBAL_ONLY by default
    agent_mode_state_.mode = REGISTRY_MODE_GLOBAL_ONLY;
    agent_mode_state_.transitions = 0;
    agent_mode_state_.fallbacks = 0;
    agent_mode_state_.last_seen_epoch = 0;
}

// Global flag to prevent reentrancy during hook installation
static std::atomic<bool> g_is_installing_hooks{false};

// ============================================================================
// Categorized Logging System
// ============================================================================

// Helper to pause process for debugger attachment
static void wait_for_debugger() {
    if (getenv("ADA_WAIT_FOR_DEBUGGER")) {
        LOG_LIFECYCLE("[Agent] Waiting for debugger... (PID: %d)\n", getpid());
        LOG_LIFECYCLE("[Agent] Run: lldb -p %d\n", getpid());
        LOG_LIFECYCLE("[Agent] Or use VS Code 'Attach to Process'\n");
        
        // On macOS/Linux, SIGSTOP pauses the process. 
        // A debugger can attach and then send SIGCONT (or just 'continue').
        raise(SIGSTOP);
        
        LOG_LIFECYCLE("[Agent] Debugger attached! Resuming...\n");
    }
}

AgentContext::~AgentContext() {
    g_agent_shutting_down = true;
    
    LOG_LIFECYCLE("[Agent] Shutting down (emitted=%llu events, blocked=%llu reentrancy)\n",
            static_cast<unsigned long long>(events_emitted_.load()),
            static_cast<unsigned long long>(reentrancy_blocked_.load()));
    
    // Print final statistics
    LOG_LIFECYCLE("[Agent] Final stats: events_emitted=%llu, reentrancy_blocked=%llu, "
            "stack_failures=%llu\n",
            static_cast<unsigned long long>(events_emitted_.load()),
            static_cast<unsigned long long>(reentrancy_blocked_.load()),
            static_cast<unsigned long long>(stack_capture_failures_.load()));
    
    // Interceptor is cleaned up by unique_ptr
    // Ring buffers and shared memory are cleaned up by destructors
    LOG_LIFECYCLE("[Agent] AgentContext did destruct\n");
}

bool AgentContext::initialize(uint32_t host_pid, uint32_t session_id) {
    host_pid_ = host_pid;
    session_id_ = session_id;
    
    LOG_LIFECYCLE("[Agent] Initializing with host_pid=%u, session_id=%u\n", 
            host_pid_, session_id_);
    
    if (!open_shared_memory()) {
        LOG_LIFECYCLE("[Agent] Failed to open shared memory\n");
        return false;
    }
    
    if (!attach_ring_buffers()) {
        LOG_LIFECYCLE("[Agent] Failed to attach ring buffers\n");
        return false;
    }
    
    // Get Frida interceptor
    interceptor_.reset(gum_interceptor_obtain());
    LOG_LIFECYCLE("[Agent] Got interceptor: %p\n", interceptor_.get());

    // Build exclude list (defaults + custom)
    // Note: exclude list is used by hook installation to skip hot paths
    // Read from init payload if provided or ADA_EXCLUDE env
    const char* env_ex = getenv("ADA_EXCLUDE");
    AdaExcludeList* xs = ada_exclude_create(128);
    if (xs) {
        ada_exclude_add_defaults(xs);
        if (g_exclude_csv[0] != '\0') {
            ada_exclude_add_from_csv(xs, g_exclude_csv);
        }
        if (env_ex && *env_ex) {
            ada_exclude_add_from_csv(xs, env_ex);
        }
    }

    return true;
}

bool AgentContext::open_shared_memory() {
    if (ada::internal::g_agent_verbose) LOG_LIFECYCLE("[Agent] Opening shared memory segments...\n");

    LOG_LIFECYCLE("[Agent] Attempting to open shared memory with host_pid=%u, session_id=0x%08x\n",
            host_pid_, session_id_);

    shm_control_ = SharedMemoryRef::open_unique(0, host_pid_,
                                               session_id_, 4096);
    shm_index_ = SharedMemoryRef::open_unique(1, host_pid_,
                                             session_id_, 32 * 1024 * 1024);
    shm_detail_ = SharedMemoryRef::open_unique(2, host_pid_,
                                              session_id_, 32 * 1024 * 1024);

    if (!shm_control_.is_valid() || !shm_index_.is_valid() ||
        !shm_detail_.is_valid()) {
        LOG_LIFECYCLE("[Agent] Failed to open shared memory (control=%d, index=%d, detail=%d)\n",
                shm_control_.is_valid() ? 1 : 0,
                shm_index_.is_valid() ? 1 : 0,
                shm_detail_.is_valid() ? 1 : 0);
        return false;
    }
    
    if (ada::internal::g_agent_verbose) LOG_LIFECYCLE("[Agent] Successfully opened all shared memory segments\n");
    
    // Map control block
    control_block_ = static_cast<ControlBlock*>(shm_control_.get_address());
    if (ada::internal::g_agent_verbose) LOG_LIFECYCLE("[Agent] Control block mapped at %p\n", control_block_);
    // Map SHM directory bases (if published)
    if (control_block_) {
        (void)shm_dir_map_local_bases(&control_block_->shm_directory);
    }

    // Read registry_mode from controller (do NOT overwrite it)
    // The controller sets this to DUAL_WRITE when registry is enabled
    if (control_block_) {
        agent_mode_state_.mode = __atomic_load_n(&control_block_->registry_mode, __ATOMIC_ACQUIRE);
        LOG_LIFECYCLE("[Agent] Read registry_mode from controller: %u\n", agent_mode_state_.mode);
    }

    // Try to open and attach to thread registry (optional, can be disabled by env)
    bool disable_registry = false;
    if (const char* env = getenv("ADA_DISABLE_REGISTRY")) {
        if (env[0] != '\0' && env[0] != '0') disable_registry = true;
    }
    if (!disable_registry) {
        size_t registry_size = thread_registry_calculate_memory_size_with_capacity(MAX_THREADS);
        auto reg_c = shared_memory_open_unique("registry", host_pid_, session_id_, registry_size);

        // Unconditional diagnostic logging for registry status
        LOG_LIFECYCLE("[Agent] Registry shm open: %s (host_pid=%u, session_id=0x%08x)\n",
                      reg_c ? "SUCCESS" : "FAILED", host_pid_, session_id_);

        if (reg_c) {
            // Wrap in RAII holder and keep as member to keep mapping alive
            SharedMemoryRef shm_reg(reinterpret_cast<SharedMemoryRef*>(reg_c));
            void* addr = shm_reg.get_address();
            ::ThreadRegistry* reg_handle = thread_registry_attach(addr);

            LOG_LIFECYCLE("[Agent] Registry attach: %s (addr=%p)\n",
                          reg_handle ? "SUCCESS" : "FAILED", addr);

            if (reg_handle) {
                ada_set_global_registry(reg_handle);
                LOG_LIFECYCLE("[Agent] Global registry set: %p\n", (void*)reg_handle);
                shm_registry_ = std::move(shm_reg);
            } else {
                LOG_LIFECYCLE("[Agent] Failed to attach thread registry at %p\n", addr);
            }
        } else {
            LOG_LIFECYCLE("[Agent] Registry segment not found; using global rings\n");
        }
    } else {
        LOG_LIFECYCLE("[Agent] Registry disabled by ADA_DISABLE_REGISTRY\n");
    }

    return true;
}

bool AgentContext::attach_ring_buffers() {
    void* index_addr = shm_index_.get_address();
    void* detail_addr = shm_detail_.get_address();
    
    LOG_LIFECYCLE("[Agent] Ring buffer addresses: index=%p, detail=%p\n", 
            index_addr, detail_addr);
    
    // Attach to existing ring buffers
    auto* index_rb = ring_buffer_attach(index_addr, 32 * 1024 * 1024, 
                                        sizeof(IndexEvent));
    index_ring_.reset(reinterpret_cast<ada::internal::RingBuffer*>(index_rb));
    LOG_LIFECYCLE("[Agent] Index ring attached: %p\n", index_ring_.get());
    
    auto* detail_rb = ring_buffer_attach(detail_addr, 32 * 1024 * 1024, 
                                         sizeof(DetailEvent));
    detail_ring_.reset(reinterpret_cast<ada::internal::RingBuffer*>(detail_rb));
    LOG_LIFECYCLE("[Agent] Detail ring attached: %p\n", detail_ring_.get());
    
    return index_ring_ && detail_ring_;
}

void AgentContext::hook_function(const char* name) {
    if (ada::internal::g_agent_verbose) LOG_HOOK_INSTALL("[Agent] Finding symbol: %s\n", name);
    num_hooks_attempted_++;
    
    // Generate stable function ID from name
    uint64_t function_id = static_cast<uint64_t>(hash_string(name));
    
    // Find function address
    GumModule* main_module = gum_process_get_main_module();
    GumAddress func_addr = gum_module_find_symbol_by_name(main_module, name);
    
    // Record result
    hook_results_.emplace_back(name, func_addr, function_id, func_addr != 0);
    
    if (func_addr != 0) {
        LOG_HOOK_INSTALL("[Agent] Found symbol: %s at 0x%llx\n", name, func_addr);
        
        // Create hook data
        auto hook = std::make_unique<HookData>(this, function_id, name, func_addr);
        hooks_.push_back(std::move(hook));
        HookData* hook_ptr = hooks_.back().get();  // Get pointer after moving into vector

        // Create listener with C callbacks (defined in extern "C" block below)
        GumInvocationListener* listener = gum_make_call_listener(
            on_enter_callback,
            on_leave_callback,
            hook_ptr,
            nullptr  // No destructor needed, we manage lifetime
        );
        hook_ptr->listener = listener;  // Store listener to keep it alive

        gum_interceptor_attach(interceptor_.get(),
                              GSIZE_TO_POINTER(func_addr),
                              listener,
                              nullptr,
                              GUM_ATTACH_FLAGS_NONE);

        num_hooks_successful_++;
        
        if (ada::internal::g_agent_verbose) LOG_HOOK_INSTALL("[Agent] Hooked: %s at 0x%llx (id=%llu)\n", name,
                static_cast<unsigned long long>(func_addr),
                static_cast<unsigned long long>(function_id));
    } else {
        if (ada::internal::g_agent_verbose) LOG_HOOK_INSTALL("[Agent] Failed to find: %s\n", name);
    }
}

// Helper: export collection container (only names; resolve addresses precisely later)
struct ExportEntry { std::string name; };

static gboolean collect_exports_cb(const GumExportDetails* details, gpointer user_data) {
    auto* vec = reinterpret_cast<std::vector<ExportEntry>*>(user_data);
    if (details != nullptr && details->name != nullptr && details->name[0] != '\0') {
        if (details->type == GUM_EXPORT_FUNCTION) {
            vec->push_back(ExportEntry{details->name});
        }
    }
    return TRUE;
}

// Resolve an export's actual runtime address with platform quirks handled
static GumAddress resolve_export_address(GumModule* mod, const std::string& sym) {
    if (mod == nullptr || sym.empty()) return 0;

    // Try exact export name first
    GumAddress addr = gum_module_find_export_by_name(mod, sym.c_str());
    if (addr != 0) return addr;

#ifdef __APPLE__
    // On macOS, C symbols are typically prefixed with an underscore in Mach-O exports.
    // Try toggling the underscore prefix if the initial lookup failed.
    if (!sym.empty() && sym[0] != '_') {
        std::string with_us = "_" + sym;
        addr = gum_module_find_export_by_name(mod, with_us.c_str());
        if (addr != 0) return addr;
    } else if (sym.size() > 1 && sym[0] == '_') {
        std::string without_us = sym.substr(1);
        addr = gum_module_find_export_by_name(mod, without_us.c_str());
        if (addr != 0) return addr;
    }
#endif

    // Fallback to symbol lookup which may resolve the implementation instead of a stub
    addr = gum_module_find_symbol_by_name(mod, sym.c_str());
    if (addr != 0) return addr;

#ifdef __APPLE__
    if (!sym.empty() && sym[0] != '_') {
        std::string with_us = "_" + sym;
        addr = gum_module_find_symbol_by_name(mod, with_us.c_str());
        if (addr != 0) return addr;
    } else if (sym.size() > 1 && sym[0] == '_') {
        std::string without_us = sym.substr(1);
        addr = gum_module_find_symbol_by_name(mod, without_us.c_str());
        if (addr != 0) return addr;
    }
#endif

    return 0;
}

void AgentContext::install_hooks() {
    LOG_HOOK_INSTALL("[Agent] install_hooks() entered\n");

    // CRITICAL: Always signal hooks_ready at the end, even if no hooks installed
    // This prevents controller timeout when ADA_SKIP_DSO_HOOKS=1 or no hookable functions
    auto set_hooks_ready_guard = [this]() {
        if (control_block_) {
            __atomic_store_n(&control_block_->hooks_ready, 1, __ATOMIC_RELEASE);
            LOG_HOOK_INSTALL("[Agent] Set hooks_ready flag in control block\n");
        }
    };

    // Use RAII to ensure hooks_ready is always set
    struct HooksReadyGuard {
        std::function<void()> cleanup;
        ~HooksReadyGuard() { if (cleanup) cleanup(); }
    } guard{set_hooks_ready_guard};

    // Build exclude list (defaults + overrides) BEFORE transaction
    LOG_HOOK_INSTALL("[Agent] Creating exclude list...\n");
    AdaExcludeList* xs = ada_exclude_create(256);
    if (xs) {
        ada_exclude_add_defaults(xs);
        if (g_exclude_csv[0] != '\0') ada_exclude_add_from_csv(xs, g_exclude_csv);
        const char* env_ex = getenv("ADA_EXCLUDE");
        if (env_ex && *env_ex) ada_exclude_add_from_csv(xs, env_ex);
        
        // Capture agent path for self-exclusion
        GumModuleMap* map = gum_module_map_new();
        GumModule* agent_mod = gum_module_map_find(map, GUM_ADDRESS(agent_init));
        if (agent_mod) {
            const char* path = gum_module_get_path(agent_mod);
            if (path) {
                agent_path_ = path;
                LOG_HOOK_INSTALL("[Agent] Identified agent module path: %s\n", agent_path_.c_str());
            }
        }
        g_object_unref(map);
    }

    gum_interceptor_begin_transaction(interceptor_.get());
    g_is_installing_hooks.store(true);
    LOG_HOOK_INSTALL("[Agent] Beginning comprehensive hook installation...\n");

    // Registry assigns per-module stable IDs (using member hook_registry_)
    LOG_HOOK_INSTALL("[Agent] Using hook registry for symbol table persistence...\n");

    // Enumerate main module first
    LOG_HOOK_INSTALL("[Agent] Getting main module...\n");
    GumModule* main_mod = gum_process_get_main_module();
    std::vector<ExportEntry> main_exports_entries;
    if (main_mod) {
        gum_module_enumerate_exports(main_mod, collect_exports_cb, &main_exports_entries);

        // Capture module metadata for symbol resolution (Phase 1 - symbol table persistence)
        const GumMemoryRange* range = gum_module_get_range(main_mod);
        if (range) {
            uint8_t uuid[16] = {0};
            ada::agent::extract_module_uuid(static_cast<uintptr_t>(range->base_address), uuid);
            hook_registry_.set_module_metadata("<main>", range->base_address, range->size, uuid);
            LOG_HOOK_INSTALL("[Agent] Captured main module metadata: base=0x%llx, size=%zu\n",
                    (unsigned long long)range->base_address, (size_t)range->size);
        }
    }
    std::vector<std::string> main_export_names;
    main_export_names.reserve(main_exports_entries.size());
    for (auto& e : main_exports_entries) main_export_names.push_back(e.name);

    // Plan main hooks
    auto main_plan = ada::agent::plan_module_hooks("<main>", main_export_names, xs, hook_registry_);
    // Build precise address lookup for main module using symbol/export resolution
    std::unordered_map<std::string, GumAddress> main_addr;
    for (auto& e : main_exports_entries) {
        GumAddress a = resolve_export_address(main_mod, e.name);
        if (ada::internal::g_agent_verbose) {
            LOG_HOOK_INSTALL("[Agent] Resolved main export %s -> 0x%llx\n", e.name.c_str(), (unsigned long long)a);
        }
        main_addr.emplace(e.name, a);
    }

    // Hook planned symbols in main module
    LOG_HOOK_INSTALL("[Agent] Main module has %zu planned hooks\n", main_plan.size());
    for (const auto& entry : main_plan) {
        const auto it = main_addr.find(entry.symbol);
        num_hooks_attempted_++;
        if (it != main_addr.end() && it->second != 0) {
            LOG_HOOK_INSTALL("[Agent] Hooking main symbol: %s at 0x%lx\n", entry.symbol.c_str(), (unsigned long)it->second);

            // Verify the address looks valid
            LOG_HOOK_INSTALL("[Agent] Creating hook for %s, function_id=%llu\n", entry.symbol.c_str(), (unsigned long long)entry.function_id);

            auto hook = std::make_unique<HookData>(this, entry.function_id, entry.symbol, it->second);
            hooks_.push_back(std::move(hook));
            HookData* hook_ptr = hooks_.back().get();  // Get pointer after moving into vector

            LOG_HOOK_INSTALL("[Agent] Creating listener with callbacks: on_enter=%p, on_leave=%p, data=%p\n",
                    (void*)on_enter_callback, (void*)on_leave_callback, (void*)hook_ptr);

            GumInvocationListener* listener = gum_make_call_listener(on_enter_callback, on_leave_callback, hook_ptr, nullptr);

            LOG_HOOK_INSTALL("[Agent] Created listener: %p\n", listener);

            hook_ptr->listener = listener;  // Store listener to keep it alive

            GumAttachReturn ret = gum_interceptor_attach(interceptor_.get(), GSIZE_TO_POINTER(it->second), listener, nullptr, GUM_ATTACH_FLAGS_NONE);
            if (ret == GUM_ATTACH_OK) {
                LOG_HOOK_INSTALL("[Agent] Successfully attached hook to %s\n", entry.symbol.c_str());
                hook_results_.emplace_back(entry.symbol, it->second, entry.function_id, true);
                num_hooks_successful_++;
            } else {
                LOG_HOOK_INSTALL("[Agent] Failed to attach hook to %s (error: %d)\n", entry.symbol.c_str(), ret);
                hook_results_.emplace_back(entry.symbol, it->second, entry.function_id, false);
            }
        } else {
            hook_results_.emplace_back(entry.symbol, 0, entry.function_id, false);
        }
    }

    // Enumerate all modules and hook DSOs (excluding main module)
    // Check if we should skip DSO hooking for testing
    bool skip_dso_hooks = false;
    const char* skip_env = getenv("ADA_SKIP_DSO_HOOKS");
    if (skip_env && skip_env[0] == '1') {
        skip_dso_hooks = true;
        LOG_HOOK_INSTALL("[Agent] Skipping DSO hooks as requested by ADA_SKIP_DSO_HOOKS=1\n");
    }

    GumModuleMap* map = skip_dso_hooks ? nullptr : gum_module_map_new();
    if (map) {
        GPtrArray* mods = gum_module_map_get_values(map);
        for (guint i = 0; mods && i < mods->len; i++) {
            LOG_HOOK_INSTALL("[Agent] Install hooks to module at index %u/%u\n", i, mods->len);
            GumModule* mod = static_cast<GumModule*>(g_ptr_array_index(mods, i));
            
            const char* path = gum_module_get_path(mod);
            
            if (mod != main_mod) {
                // Yes. We only install hooks to the main module. Maybe we need to install hooks
                // to the functions offer system semantics later.
                LOG_HOOK_INSTALL("[Agent] Skipping install hooks to non-main module: %s\n", path);
                continue;
            }
            
            LOG_HOOK_INSTALL("[Agent] Install hooks to module %s\n", path ? path : "(unknown)");
            if (!path || path[0] == '\0') continue;
            std::vector<ExportEntry> exps;
            gum_module_enumerate_exports(mod, collect_exports_cb, &exps);
            if (exps.empty()) continue;

            // Capture DSO module metadata for symbol resolution
            const GumMemoryRange* dso_range = gum_module_get_range(mod);
            if (dso_range) {
                uint8_t dso_uuid[16] = {0};
                ada::agent::extract_module_uuid(static_cast<uintptr_t>(dso_range->base_address), dso_uuid);
                hook_registry_.set_module_metadata(path, dso_range->base_address, dso_range->size, dso_uuid);
                LOG_HOOK_INSTALL("[Agent] Captured DSO module metadata: %s base=0x%llx, size=%zu\n",
                        path, (unsigned long long)dso_range->base_address, (size_t)dso_range->size);
            }

            std::vector<std::string> names; names.reserve(exps.size());
            std::unordered_map<std::string, GumAddress> addr;
            for (auto& e : exps) {
                names.push_back(e.name);
                GumAddress a = resolve_export_address(mod, e.name);
                if (ada::internal::g_agent_verbose) {
                    // LOG_HOOK_INSTALL("[Agent] Resolved DSO export %s -> 0x%llx\n", e.name.c_str(), (unsigned long long)a); // Moved into resolve_export_address
                }
                addr.emplace(e.name, a);
            }
            auto plan = ada::agent::plan_module_hooks(path, names, xs, hook_registry_);
            [[maybe_unused]] int32_t plan_index = 0;
            for (const auto& pe : plan) {
                LOG_HOOK_INSTALL("[Agent] (%d/%zu) Will attach DSO hook to %s\n", plan_index, plan.size(), pe.symbol.c_str());
                num_hooks_attempted_++;
                auto it = addr.find(pe.symbol);
                if (it != addr.end() && it->second != 0) {
                    auto hook = std::make_unique<HookData>(this, pe.function_id, pe.symbol, it->second);
                    hooks_.push_back(std::move(hook));
                    HookData* hook_ptr = hooks_.back().get();  // Get pointer after moving into vector
                    LOG_HOOK_INSTALL("[Agent] (%d/%zu) Will make call listener for %s\n", plan_index, plan.size(), pe.symbol.c_str());
                    GumInvocationListener* listener = gum_make_call_listener(on_enter_callback, on_leave_callback, hook_ptr, nullptr);
                    hook_ptr->listener = listener;  // Store listener to keep it alive
                    LOG_HOOK_INSTALL("[Agent] (%d/%zu) Will attach interceptor for %s\n", plan_index, plan.size(), pe.symbol.c_str());
                    GumAttachReturn ret = gum_interceptor_attach(interceptor_.get(), GSIZE_TO_POINTER(it->second), listener, nullptr, GUM_ATTACH_FLAGS_NONE);
                    if (ret == GUM_ATTACH_OK) {
                        hook_results_.emplace_back(pe.symbol, it->second, pe.function_id, true);
                        num_hooks_successful_++;
                        LOG_HOOK_INSTALL("[Agent] (%d/%zu) Attached DSO hook to %s\n", plan_index, plan.size(), pe.symbol.c_str());
                    } else {
                        hook_results_.emplace_back(pe.symbol, it->second, pe.function_id, false);
                        LOG_HOOK_INSTALL("[Agent] (%d/%zu) Failed to attach DSO hook to %s (error: %d)\n", plan_index, plan.size(), pe.symbol.c_str(), ret);
                    }
                } else {
                    LOG_HOOK_INSTALL("[Agent] (%d/%zu) Skipped DSO hook to %s\n", plan_index, plan.size(), pe.symbol.c_str());
                    hook_results_.emplace_back(pe.symbol, 0, pe.function_id, false);
                }
                plan_index += 1;
            }
            LOG_HOOK_INSTALL("[Agent] Processes hook plan for module: %s\n", gum_module_get_path(static_cast<GumModule*>(mod)));
        }
        LOG_HOOK_INSTALL("[Agent] Processes hook plan for all modules.\n");
        g_object_unref(map);
    }

    LOG_HOOK_INSTALL("[Agent] hooks installation complete: %u/%u hooks installed\n",
            num_hooks_successful_, num_hooks_attempted_);

    // End transaction
    LOG_HOOK_INSTALL("[Agent] Ending transaction...\n");
    gum_interceptor_end_transaction(interceptor_.get());
    LOG_HOOK_INSTALL("[Agent] Transaction ended.\n");
    
    g_is_installing_hooks.store(false);
    
    // Clean up exclude list
    if (xs) {
        LOG_HOOK_INSTALL("[Agent] Destroying exclude list at %p...\n", xs);
        ada_exclude_destroy(xs);
        LOG_HOOK_INSTALL("[Agent] Exclude list destroyed.\n");
    }

    // Send hook summary
    LOG_HOOK_SUMMARY("[Agent] Sending hook summary...\n");
    send_hook_summary();
    LOG_HOOK_SUMMARY("[Agent] Hook summary sent.\n");

    // Write symbol table to file for manifest generation (Phase 1: symbol resolution)
    {
        char symbols_path[256];
        snprintf(symbols_path, sizeof(symbols_path), "/tmp/ada_symbols_%u_%08x.json",
                 host_pid_, session_id_);
        FILE* symbols_file = fopen(symbols_path, "w");
        if (symbols_file) {
            std::string json = hook_registry_.export_to_json();
            fwrite(json.data(), 1, json.size(), symbols_file);
            fclose(symbols_file);
            LOG_HOOK_INSTALL("[Agent] Wrote symbol table to %s (%zu bytes)\n",
                    symbols_path, json.size());
        } else {
            LOG_HOOK_INSTALL("[Agent] Failed to write symbol table to %s\n", symbols_path);
        }
    }

    LOG_HOOK_INSTALL("[Agent] Initialization complete: %u/%u hooks installed\n",
            num_hooks_successful_, num_hooks_attempted_);

    // hooks_ready flag is set by the RAII guard at function exit
}

void AgentContext::send_hook_summary() {
    // For now, just print the summary
    // TODO: Implement proper Frida messaging when API is available
    if (ada::internal::g_agent_verbose) LOG_HOOK_SUMMARY("[Agent] Hook Summary: attempted=%u, successful=%u, failed=%u\n",
                num_hooks_attempted_, num_hooks_successful_,
                num_hooks_attempted_ - num_hooks_successful_);
    
    for ([[maybe_unused]] const auto& result : hook_results_) {
        LOG_HOOK_SUMMARY("[Agent]   %s: address=0x%llx, id=%llu, %s\n", 
                result.name.c_str(),
                static_cast<unsigned long long>(result.address), 
                static_cast<unsigned long long>(result.id),
                result.success ? "SUCCESS" : "FAILED");
    }
}

// Update registry_mode via AgentModeState state machine
void AgentContext::update_registry_mode(uint64_t now_ns, uint64_t hb_timeout_ns) {
    if (!control_block_) return;

    // Diagnostic: log health check values (one-time)
    static bool logged_health_check = false;
    if (!logged_health_check) {
        uint32_t ready = __atomic_load_n(&control_block_->registry_ready, __ATOMIC_ACQUIRE);
        uint32_t epoch = __atomic_load_n(&control_block_->registry_epoch, __ATOMIC_ACQUIRE);
        uint64_t hb = __atomic_load_n(&control_block_->drain_heartbeat_ns, __ATOMIC_ACQUIRE);
        bool cond1 = (ready != 0);
        bool cond2 = (epoch > 0);
        bool cond3 = (hb != 0);
        bool cond4 = (now_ns >= hb);
        bool cond5 = ((now_ns - hb) <= hb_timeout_ns);
        bool healthy = cond1 && cond2 && cond3 && cond4 && cond5;
        LOG_LIFECYCLE("[Agent] Health check: ready=%u(%s), epoch=%u(%s), hb=%llu(%s), now_ns=%llu, hb_timeout=%llu\n",
                      ready, cond1 ? "OK" : "FAIL",
                      epoch, cond2 ? "OK" : "FAIL",
                      (unsigned long long)hb, cond3 ? "OK" : "FAIL",
                      (unsigned long long)now_ns,
                      (unsigned long long)hb_timeout_ns);
        LOG_LIFECYCLE("[Agent] Health check: now>=hb=%s, (now-hb)<=timeout=%s, healthy=%s\n",
                      cond4 ? "OK" : "FAIL",
                      cond5 ? "OK" : "FAIL",
                      healthy ? "YES" : "NO");
        if (!cond5 && hb != 0) {
            LOG_LIFECYCLE("[Agent] Health check: delta=%llu vs timeout=%llu\n",
                          (unsigned long long)(now_ns - hb),
                          (unsigned long long)hb_timeout_ns);
        }
        logged_health_check = true;
    }

    AgentModeState before = agent_mode_state_;
    agent_mode_tick(&agent_mode_state_, control_block_, now_ns, hb_timeout_ns);
    if (before.mode != agent_mode_state_.mode) {
        __atomic_store_n(&control_block_->registry_mode, agent_mode_state_.mode, __ATOMIC_RELEASE);
        // Best-effort visibility for transitions (optional)
        __atomic_fetch_add(&control_block_->mode_transitions, (uint64_t)1, __ATOMIC_RELAXED);
    }
}

// ============================================================================
// TLS Management
// ============================================================================

static void tls_destructor(void* data) {
    if (data) {
        delete static_cast<ThreadLocalData*>(data);
    }
    // Also cleanup ADA TLS / unregister from registry
    ada_tls_thread_cleanup();
}

static void init_tls_key() {
    pthread_key_create(&g_tls_key, tls_destructor);
}

ThreadLocalData* get_thread_local() {
    pthread_once(&g_tls_once, init_tls_key);
    
    auto* tls = static_cast<ThreadLocalData*>(pthread_getspecific(g_tls_key));
    if (!tls) {
        tls = new ThreadLocalData();
        if (tls) {
            pthread_setspecific(g_tls_key, tls);
        }
    }
    return tls;
}

// ============================================================================
// Global Context Management
// ============================================================================

AgentContext* get_agent_context() {
    std::lock_guard<std::mutex> lock(g_context_mutex);
    
    if (!g_agent_context) {
        // Try to get session info from globals or environment
        uint32_t session_id = g_session_id;
        uint32_t host_pid = g_host_pid;
        
        if (session_id == UINT32_MAX) {
            const char* sid_env = getenv("ADA_SHM_SESSION_ID");
            if (sid_env && sid_env[0] != '\0') {
                session_id = static_cast<uint32_t>(strtoul(sid_env, nullptr, 16));
            }
        }
        
        if (host_pid == UINT32_MAX) {
            const char* host_env = getenv("ADA_SHM_HOST_PID");
            if (host_env && host_env[0] != '\0') {
                host_pid = static_cast<uint32_t>(strtoul(host_env, nullptr, 10));
            }
        }
        
        if (session_id != UINT32_MAX && host_pid != UINT32_MAX) {
            g_agent_context = std::make_unique<AgentContext>();
            if (!g_agent_context->initialize(host_pid, session_id)) {
                g_agent_context.reset();
            }
        }
    }
    
    return g_agent_context.get();
}

// ============================================================================
// Utility Functions  
// ============================================================================

uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

uint64_t platform_get_timestamp() {
#ifdef __APPLE__
    // mach_absolute_time() returns ticks, not nanoseconds
    // Convert to nanoseconds using mach_timebase_info
    static mach_timebase_info_data_t timebase_info = {0, 0};
    if (timebase_info.denom == 0) {
        mach_timebase_info(&timebase_info);
    }
    uint64_t mach_time = mach_absolute_time();
    // Convert to nanoseconds: mach_time * numer / denom
    return mach_time * timebase_info.numer / timebase_info.denom;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
#endif
}

static void segfault_handler(int sig) {
    g_segfault_occurred = 1;
}

size_t safe_stack_capture(void* dest, void* stack_ptr, size_t max_size) {
    if (!dest || !stack_ptr) return 0;
    
    struct sigaction old_sa, new_sa;
    
    new_sa.sa_handler = segfault_handler;
    sigemptyset(&new_sa.sa_mask);
    new_sa.sa_flags = 0;
    
    // Install temporary signal handler
    sigaction(SIGSEGV, &new_sa, &old_sa);
    g_segfault_occurred = 0;
    
    size_t copied = 0;
    
    // Try to copy in chunks to detect boundaries
    const size_t chunk_size = 16;
    for (size_t offset = 0; offset < max_size && !g_segfault_occurred;
         offset += chunk_size) {
        size_t to_copy = (offset + chunk_size <= max_size) ? 
                        chunk_size : (max_size - offset);
        
        // Use volatile to prevent optimization
        volatile char test_read = *((char*)stack_ptr + offset);
        (void)test_read;
        
        if (!g_segfault_occurred) {
            memcpy((char*)dest + offset, (char*)stack_ptr + offset, to_copy);
            copied += to_copy;
        }
    }
    
    // Restore original handler
    sigaction(SIGSEGV, &old_sa, nullptr);
    
    return copied;
}

void parse_init_payload(const char* data, int data_size,
                       uint32_t* out_host_pid, uint32_t* out_session_id) {
    if (!data || data_size <= 0) return;

    // Initialize outputs
    *out_host_pid = 0;
    *out_session_id = 0;
    
    // Copy to a null-terminated buffer
    char buf[256];
    size_t copy_len = static_cast<size_t>(data_size) < sizeof(buf) - 1 ? 
                     static_cast<size_t>(data_size) : sizeof(buf) - 1;
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\0';
    
    // Normalize separators to spaces
    for (size_t i = 0; i < copy_len; i++) {
        if (buf[i] == ';' || buf[i] == ',' || buf[i] == '\n' || 
            buf[i] == '\r' || buf[i] == '\t') {
            buf[i] = ' ';
        }
    }
    
    // Tokenize by spaces
    char* saveptr = nullptr;
    for (char* tok = strtok_r(buf, " ", &saveptr); tok != nullptr;
         tok = strtok_r(nullptr, " ", &saveptr)) {
        char* eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = tok;
        const char* val = eq + 1;
        if (val[0] == '\0') continue;
        
        if (strcmp(key, "host_pid") == 0 || strcmp(key, "pid") == 0) {
            *out_host_pid = static_cast<uint32_t>(strtoul(val, nullptr, 0));
        } else if (strcmp(key, "session_id") == 0 || strcmp(key, "sid") == 0) {
            // Session ID is ALWAYS passed as hex (from snprintf with %08x)
            // Parse as hex regardless of prefix
            unsigned int parsed = UINT32_MAX;
            if (strncmp(val, "0x", 2) == 0 || strncmp(val, "0X", 2) == 0) {
                // Has 0x prefix, skip it
                parsed = static_cast<unsigned int>(strtoul(val + 2, nullptr, 16));
            } else {
                // No prefix, parse as hex (controller sends it with %08x)
                parsed = static_cast<unsigned int>(strtoul(val, nullptr, 16));
            }
            *out_session_id = static_cast<uint32_t>(parsed);
        } else if (strcmp(key, "exclude") == 0) {
            // Copy exclude CSV to global buffer
            size_t n = strlen(val);
            if (n >= sizeof(g_exclude_csv)) n = sizeof(g_exclude_csv) - 1;
            memcpy(g_exclude_csv, val, n);
            g_exclude_csv[n] = '\0';
        }
    }

    // Debug output for parsing results
    LOG_LIFECYCLE("[Agent] Parsed init payload: host_pid=%u, session_id=0x%08x\n",
            *out_host_pid, *out_session_id);
}

// ============================================================================
// Hook Callbacks (C++ implementation)
// ============================================================================

static void capture_index_event(AgentContext* ctx, HookData* hook,
                               ThreadLocalData* tls, EventKind kind) {
    if (!ctx->control_block()) {
        LOG_EVENTS("[Agent] Control block is NULL!\n");
        return;
    }

    // Debug: Always log the actual value
    LOG_EVENTS("[Agent] Index lane enabled flag: %u\n", ctx->control_block()->index_lane_enabled);

    if (!ctx->control_block()->index_lane_enabled) {
        LOG_EVENTS("[Agent] Index lane disabled (flag=%u)\n", ctx->control_block()->index_lane_enabled);
        return;
    }

    LOG_EVENTS("[Agent] Capturing index event for %s (kind=%d)\n", hook->function_name.c_str(), kind);
    
    IndexEvent event = {};
    event.timestamp = platform_get_timestamp();
    event.function_id = hook->function_id;
    event.thread_id = tls->thread_id();
    event.event_kind = kind;
    event.call_depth = tls->call_depth();
    event._padding = 0;
    
    // Determine operating mode
    uint32_t mode = __atomic_load_n(&ctx->control_block()->registry_mode, __ATOMIC_ACQUIRE);
    bool wrote = false;
    bool wrote_pt = false;

    // One-time diagnostic log per thread to show lane status
    static thread_local bool logged_lane_status = false;
    if (!logged_lane_status) {
        ::ThreadRegistry* diag_reg = ada_get_global_registry();
        ThreadLaneSet* diag_lanes = ada_get_thread_lane();
        LOG_LIFECYCLE("[Agent] First hook on thread: registry=%p, lanes=%p, mode=%u\n",
                      (void*)diag_reg, (void*)diag_lanes, mode);
        logged_lane_status = true;
    }

    // Attempt per-thread path if allowed by mode
    if (mode == REGISTRY_MODE_DUAL_WRITE || mode == REGISTRY_MODE_PER_THREAD_ONLY) {
        ada_tls_state_t* ada_tls = ada_get_tls_state();
        ThreadLaneSet* lanes = ada_get_thread_lane();
        ada_thread_metrics_t* metrics = ada_tls ? ada_tls->metrics : nullptr;
        if (!metrics && lanes) {
            metrics = thread_lanes_get_metrics(lanes);
            if (ada_tls) ada_tls->metrics = metrics;
        }
        if (lanes && ada_tls) {
            // Use ring pool for swap-on-overflow support
            ::RingPool* index_pool = ada_tls->index_pool;

            // One-time diagnostic for per-thread write path
            static thread_local bool logged_pt_path = false;
            if (!logged_pt_path) {
                LOG_LIFECYCLE("[Agent] Per-thread write path: lanes=%p, index_pool=%p\n",
                              (void*)lanes, (void*)index_pool);
                logged_pt_path = true;
            }

            if (index_pool) {
                RingBufferHeader* hdr = ring_pool_get_active_header(index_pool);

                // One-time diagnostic for ring buffer header
                static thread_local bool logged_hdr = false;
                if (!logged_hdr) {
                    LOG_LIFECYCLE("[Agent] Ring buffer header: hdr=%p\n", (void*)hdr);
                    if (hdr) {
                        LOG_LIFECYCLE("[Agent] Ring buffer hdr: capacity=%u, read_pos=%u, write_pos=%u\n",
                                      hdr->capacity, hdr->read_pos, hdr->write_pos);
                    }
                    logged_hdr = true;
                }

                if (hdr) {
                    wrote_pt = ring_buffer_write_raw(hdr, sizeof(IndexEvent), &event);
                    if (!wrote_pt) {
                        // Ring is full - swap to a new ring and retry
                        uint32_t old_ring_idx = UINT32_MAX;
                        if (ring_pool_swap_active(index_pool, &old_ring_idx)) {
                            // Successfully swapped, get new active header and retry write
                            hdr = ring_pool_get_active_header(index_pool);
                            if (hdr) {
                                wrote_pt = ring_buffer_write_raw(hdr, sizeof(IndexEvent), &event);
                            }
                        } else {
                            // Pool exhaustion - try to recover
                            if (ring_pool_handle_exhaustion(index_pool)) {
                                // Recovered, try swap again
                                if (ring_pool_swap_active(index_pool, &old_ring_idx)) {
                                    hdr = ring_pool_get_active_header(index_pool);
                                    if (hdr) {
                                        wrote_pt = ring_buffer_write_raw(hdr, sizeof(IndexEvent), &event);
                                    }
                                }
                            }
                        }
                    }

                    if (wrote_pt) {
                        LOG_EVENTS("[Agent] Wrote index event (per-thread)\n");
                        ctx->increment_events_emitted();
                        if (metrics) {
                            ada_thread_metrics_record_event_written(metrics, sizeof(IndexEvent));
                        }
                    } else if (metrics) {
                        ada_thread_metrics_record_ring_full(metrics);
                    }
                } else {
                    // Log that hdr is NULL - this would explain missing events
                    static thread_local bool logged_null_hdr = false;
                    if (!logged_null_hdr) {
                        LOG_LIFECYCLE("[Agent] WARN: Ring buffer header is NULL for index_pool=%p\n",
                                      (void*)index_pool);
                        logged_null_hdr = true;
                    }
                }
            } else {
                // Fallback: no ring pool, use direct registry access
                Lane* idx_lane = thread_lanes_get_index_lane(lanes);
                ::ThreadRegistry* reg = ada_get_global_registry();
                if (reg) {
                    RingBufferHeader* hdr = thread_registry_get_active_ring_header(reg, idx_lane);
                    if (hdr) {
                        wrote_pt = ring_buffer_write_raw(hdr, sizeof(IndexEvent), &event);
                        if (wrote_pt) {
                            ctx->increment_events_emitted();
                            if (metrics) {
                                ada_thread_metrics_record_event_written(metrics, sizeof(IndexEvent));
                            }
                        } else if (metrics) {
                            ada_thread_metrics_record_ring_full(metrics);
                        }
                    }
                }
            }
        }
    }

    // If dual-write, always mirror to process-global
    if (mode == REGISTRY_MODE_DUAL_WRITE) {
        ::RingBuffer* grb = reinterpret_cast<::RingBuffer*>(ctx->index_ring());
        wrote = ring_buffer_write(grb, &event);
    } else if (mode == REGISTRY_MODE_GLOBAL_ONLY || (mode == REGISTRY_MODE_PER_THREAD_ONLY && !wrote_pt)) {
        // Either global-only, or per-thread-only failed -> fallback
        ::RingBuffer* grb = reinterpret_cast<::RingBuffer*>(ctx->index_ring());
        wrote = ring_buffer_write(grb, &event);
        if (mode == REGISTRY_MODE_PER_THREAD_ONLY && !wrote_pt) {
            // Best-effort visibility: bump fallback counter when available
            __atomic_fetch_add(&ctx->control_block()->fallback_events, (uint64_t)1, __ATOMIC_RELAXED);
        }
    } else {
        wrote = wrote_pt;
    }
    if (!wrote_pt) {
        ada_tls_state_t* ada_tls = ada_get_tls_state();
        ada_thread_metrics_t* metrics = ada_tls ? ada_tls->metrics : nullptr;
        if (!metrics) {
            ThreadLaneSet* fallback_lanes = ada_get_thread_lane();
            if (fallback_lanes) {
                metrics = thread_lanes_get_metrics(fallback_lanes);
                if (ada_tls) ada_tls->metrics = metrics;
            }
        }
        if (!wrote && metrics) {
            ada_thread_metrics_record_event_dropped(metrics);
        }
    }

    if (wrote) {
        LOG_EVENTS("[Agent] Wrote index event\n");
        ctx->increment_events_emitted();
    } else {
        LOG_EVENTS("[Agent] Failed to write index event\n");
    }
}

static void capture_detail_event(AgentContext* ctx, HookData* hook,
                                ThreadLocalData* tls, EventKind kind,
                                GumCpuContext* cpu) {
    if (!ctx->control_block()->detail_lane_enabled) {
        LOG_EVENTS("[Agent] Detail lane disabled\n");
        return;
    }
    
    LOG_EVENTS("[Agent] Capturing detail event\n");
    
    DetailEvent detail = {};
    detail.timestamp = platform_get_timestamp();
    detail.function_id = hook->function_id;
    detail.thread_id = tls->thread_id();
    detail.event_kind = kind;
    detail.call_depth = tls->call_depth();
    
    if (cpu) {
#ifdef __aarch64__
        if (kind == EVENT_KIND_CALL) {
            // Capture ARM64 ABI registers
            for (int i = 0; i < 8; i++) {
                detail.x_regs[i] = cpu->x[i]; // x0-x7: arguments
            }
            detail.lr = cpu->lr;
            detail.fp = cpu->fp;
            detail.sp = cpu->sp;
        } else {  // EVENT_KIND_RETURN
            detail.x_regs[0] = cpu->x[0]; // Return value
            detail.sp = cpu->sp;
        }
#elif defined(__x86_64__)
        if (kind == EVENT_KIND_CALL) {
            // Capture x86_64 ABI registers
            detail.x_regs[0] = cpu->rdi; // arg1
            detail.x_regs[1] = cpu->rsi; // arg2
            detail.x_regs[2] = cpu->rdx; // arg3
            detail.x_regs[3] = cpu->rcx; // arg4
            detail.x_regs[4] = cpu->r8;  // arg5
            detail.x_regs[5] = cpu->r9;  // arg6
            detail.x_regs[6] = cpu->rbp; // frame pointer
            detail.x_regs[7] = cpu->rsp; // stack pointer
            
            detail.sp = cpu->rsp;
            detail.fp = cpu->rbp;
        } else {  // EVENT_KIND_RETURN
            detail.x_regs[0] = cpu->rax; // Return value
            detail.sp = cpu->rsp;
        }
#endif
        
        // Optional stack window capture (128 bytes)
        if (kind == EVENT_KIND_CALL && 
            ctx->control_block()->capture_stack_snapshot) {
            void* stack_ptr = reinterpret_cast<void*>(detail.sp);
            size_t captured = safe_stack_capture(detail.stack_snapshot, 
                                                stack_ptr,
                                                sizeof(detail.stack_snapshot));
            detail.stack_size = captured;
            
            if (captured == 0) {
                ctx->increment_stack_capture_failures();
            }
        }
    }
    
    // Determine operating mode
    uint32_t mode = __atomic_load_n(&ctx->control_block()->registry_mode, __ATOMIC_ACQUIRE);
    bool wrote = false;
    bool wrote_pt = false;
    if (mode == REGISTRY_MODE_DUAL_WRITE || mode == REGISTRY_MODE_PER_THREAD_ONLY) {
        ada_tls_state_t* ada_tls = ada_get_tls_state();
        ThreadLaneSet* lanes = ada_get_thread_lane();
        ada_thread_metrics_t* metrics = ada_tls ? ada_tls->metrics : nullptr;
        if (!metrics && lanes) {
            metrics = thread_lanes_get_metrics(lanes);
            if (ada_tls) ada_tls->metrics = metrics;
        }
        if (lanes) {
            Lane* det_lane = thread_lanes_get_detail_lane(lanes);
            ::ThreadRegistry* reg = ada_get_global_registry();
            if (reg) {
                RingBufferHeader* hdr = thread_registry_get_active_ring_header(reg, det_lane);
                if (hdr) {
                    wrote_pt = ring_buffer_write_raw(hdr, sizeof(DetailEvent), &detail);
                    if (wrote_pt) {
                        LOG_EVENTS("[Agent] Wrote detail event (per-thread)\n");
                        ctx->increment_events_emitted();
                        if (metrics) {
                            ada_thread_metrics_record_event_written(metrics, sizeof(DetailEvent));
                        }
                    } else if (metrics) {
                        ada_thread_metrics_record_ring_full(metrics);
                    }
                }
            }
        }
    }

    if (mode == REGISTRY_MODE_DUAL_WRITE) {
        ::RingBuffer* grb = reinterpret_cast<::RingBuffer*>(ctx->detail_ring());
        wrote = ring_buffer_write(grb, &detail);
    } else if (mode == REGISTRY_MODE_GLOBAL_ONLY || (mode == REGISTRY_MODE_PER_THREAD_ONLY && !wrote_pt)) {
        ::RingBuffer* grb = reinterpret_cast<::RingBuffer*>(ctx->detail_ring());
        wrote = ring_buffer_write(grb, &detail);
        if (mode == REGISTRY_MODE_PER_THREAD_ONLY && !wrote_pt) {
            __atomic_fetch_add(&ctx->control_block()->fallback_events, (uint64_t)1, __ATOMIC_RELAXED);
        }
    } else {
        wrote = wrote_pt;
    }
    if (!wrote_pt) {
        ada_tls_state_t* ada_tls = ada_get_tls_state();
        ada_thread_metrics_t* metrics = ada_tls ? ada_tls->metrics : nullptr;
        if (!metrics) {
            ThreadLaneSet* fallback_lanes = ada_get_thread_lane();
            if (fallback_lanes) {
                metrics = thread_lanes_get_metrics(fallback_lanes);
                if (ada_tls) ada_tls->metrics = metrics;
            }
        }
        if (!wrote && metrics) {
            ada_thread_metrics_record_event_dropped(metrics);
        }
    }

    if (wrote) {
        LOG_EVENTS("[Agent] Wrote detail event\n");
        ctx->increment_events_emitted();
    } else {
        LOG_EVENTS("[Agent] Failed to write detail event\n");
    }
}

// C-style callbacks for Frida (must be extern "C")
extern "C" {

#ifdef ADA_MINIMAL_HOOKS
// Minimal implementation for testing hook installation without event capture overhead
void on_enter_callback(GumInvocationContext* ic, gpointer user_data) {
    // Minimal no-op implementation
    (void)ic;
    (void)user_data;
}
#else
// Full implementation with event capture
void on_enter_callback(GumInvocationContext* ic, gpointer user_data) {
    // Prevent execution during shutdown
    if (g_agent_shutting_down) return;

    // Prevent reentrancy during hook installation (e.g. from agent_log calls)
    // Check global flag FIRST before touching any user_data which might be unstable
    if (g_is_installing_hooks.load(std::memory_order_acquire)) return;

    auto* hook = static_cast<HookData*>(user_data);
    if (!hook || !hook->context) return;

    // Immediate debug output to verify callback is reached
    LOG_CALLBACKS("[HOOK] on_enter_callback FIRED!\n");

    // Simple test: just increment a counter
    static std::atomic<uint64_t> s_enter_count{0};
    uint64_t count = s_enter_count.fetch_add(1) + 1;
    if (count == 1 || count % 100 == 0) {
        LOG_CALLBACKS("[Agent] on_enter called (count=%llu)\n", (unsigned long long)count);
    }

    auto* ctx = hook->context;
    if (!ctx) {
        LOG_CALLBACKS("[Agent] ERROR: context is NULL in on_enter!\n");
        return;
    }

    auto* tls = get_thread_local();
    if (!tls) {
        LOG_CALLBACKS("[Agent] ERROR: TLS is NULL in on_enter!\n");
        return;
    }

    // Enhanced reentrancy guard with metrics
    if (tls->is_in_handler()) {
        tls->record_reentrancy_attempt();
        ctx->increment_reentrancy_blocked();
        return;
    }
    tls->enter_handler();

    // agent_log("[Agent] on_enter: %s (tid=%u)\n", hook->function_name.c_str(), tls->thread_id());

    // Tick agent mode state machine before captures
    const uint64_t now_ns = platform_get_timestamp();
    const uint64_t hb_timeout_ns = 500000000ull; // 500 ms
    ctx->update_registry_mode(now_ns, hb_timeout_ns);

    // Increment call depth
    tls->increment_depth();

    // Capture index event
    capture_index_event(ctx, hook, tls, EVENT_KIND_CALL);

    // Capture detail event with full ABI registers and optional stack
    capture_detail_event(ctx, hook, tls, EVENT_KIND_CALL, ic->cpu_context);

    tls->exit_handler();
}
#endif // ADA_MINIMAL_HOOKS

#ifdef ADA_MINIMAL_HOOKS
// Minimal implementation for testing hook installation without event capture overhead
void on_leave_callback(GumInvocationContext* ic, gpointer user_data) {
    // Minimal no-op implementation
    (void)ic;
    (void)user_data;
}
#else
// Full implementation with event capture
void on_leave_callback(GumInvocationContext* ic, gpointer user_data) {
    // Prevent execution during shutdown
    if (g_agent_shutting_down) return;

    // Prevent reentrancy during hook installation
    if (g_is_installing_hooks.load(std::memory_order_acquire)) return;

    auto* hook = static_cast<HookData*>(user_data);
    if (!hook || !hook->context) return;

    auto* ctx = hook->context;
    auto* tls = get_thread_local();
    if (!tls) return;
    
    // Reentrancy guard
    if (tls->is_in_handler()) {
        tls->record_reentrancy_attempt();
        ctx->increment_reentrancy_blocked();
        return;
    }
    tls->enter_handler();
    
    if (ada::internal::g_agent_verbose) LOG_CALLBACKS("[Agent] on_leave: %s\n", hook->function_name.c_str());
    
    // Tick agent mode state machine before captures
    const uint64_t now_ns = platform_get_timestamp();
    const uint64_t hb_timeout_ns = 500000000ull; // 500 ms
    ctx->update_registry_mode(now_ns, hb_timeout_ns);

    // Capture index event
    capture_index_event(ctx, hook, tls, EVENT_KIND_RETURN);
    
    // Capture detail event with return value
    if (ctx->control_block()->flight_state == FLIGHT_RECORDER_RECORDING) {
        capture_detail_event(ctx, hook, tls, EVENT_KIND_RETURN, ic->cpu_context);
    }
    
    // Decrement call depth
    tls->decrement_depth();
    
    tls->exit_handler();
}
#endif // ADA_MINIMAL_HOOKS

} // extern "C"

} // namespace internal
} // namespace ada

// ============================================================================
// Agent Entry Points (extern "C")
// ============================================================================

extern "C" {

// Estimate planned hook count without installing hooks.
// This is used by the loader to compute a startup timeout budget.
__attribute__((visibility("default")))
uint32_t agent_estimate_hooks(void) {
    // Ensure GUM is initialized before using its APIs
    gum_init_embedded();

    // Build exclude list (defaults + overrides)
    AdaExcludeList* xs = ada_exclude_create(256);
    if (xs) {
        ada_exclude_add_defaults(xs);
        if (ada::internal::g_exclude_csv[0] != '\0') {
            ada_exclude_add_from_csv(xs, ada::internal::g_exclude_csv);
        }
        const char* env_ex = getenv("ADA_EXCLUDE");
        if (env_ex && *env_ex) {
            ada_exclude_add_from_csv(xs, env_ex);
        }
    }

    ada::agent::HookRegistry registry;
    uint32_t count = 0;

    // Enumerate main module exports and plan hooks
    GumModule* main_mod = gum_process_get_main_module();
    std::vector<ada::internal::ExportEntry> main_exports_entries;
    if (main_mod) {
        gum_module_enumerate_exports(main_mod, ada::internal::collect_exports_cb, &main_exports_entries);
    }
    std::vector<std::string> main_export_names;
    main_export_names.reserve(main_exports_entries.size());
    for (auto& e : main_exports_entries) {
        main_export_names.push_back(e.name);
    }
    auto main_plan = ada::agent::plan_module_hooks("<main>", main_export_names, xs, registry);
    count += static_cast<uint32_t>(main_plan.size());

    // Check if we should skip DSO hooking for testing
    bool skip_dso_hooks = false;
    const char* skip_env = getenv("ADA_SKIP_DSO_HOOKS");
    if (skip_env && skip_env[0] == '1') {
        skip_dso_hooks = true;
    }

    // Enumerate DSOs and plan hooks with the same matching rules
    GumModuleMap* map = skip_dso_hooks ? nullptr : gum_module_map_new();
    if (map) {
        GPtrArray* mods = gum_module_map_get_values(map);
        for (guint i = 0; mods && i < mods->len; i++) {
            GumModule* mod = static_cast<GumModule*>(g_ptr_array_index(mods, i));
            if (mod == main_mod) continue;
            const char* path = gum_module_get_path(mod);
            if (!path || path[0] == '\0') continue;
            std::vector<ada::internal::ExportEntry> exps;
            gum_module_enumerate_exports(mod, ada::internal::collect_exports_cb, &exps);
            if (exps.empty()) continue;
            std::vector<std::string> names;
            names.reserve(exps.size());
            for (auto& e : exps) {
                names.push_back(e.name);
            }
            auto plan = ada::agent::plan_module_hooks(path, names, xs, registry);
            count += static_cast<uint32_t>(plan.size());
        }
        g_object_unref(map);
    }

    if (xs) {
        ada_exclude_destroy(xs);
    }

    LOG_HOOK_INSTALL("[Agent] agent_estimate_hooks returning %u planned hooks\n", count);
    return count;
}

// ============================================================================
// Public API for log level control
// ============================================================================

// Enable/disable log categories at runtime using bitmask
void agent_set_log_lifecycle(int enabled) {
    if (enabled) {
        ada::internal::g_log_enabled |= ada::internal::LOG_LIFECYCLE;
    } else {
        ada::internal::g_log_enabled &= ~ada::internal::LOG_LIFECYCLE;
    }
}

void agent_set_log_hooks(int enabled) {
    if (enabled) {
        ada::internal::g_log_enabled |= (ada::internal::LOG_HOOK_INSTALL | ada::internal::LOG_HOOK_SUMMARY);
    } else {
        ada::internal::g_log_enabled &= ~(ada::internal::LOG_HOOK_INSTALL | ada::internal::LOG_HOOK_SUMMARY);
    }
}

void agent_set_log_callbacks(int enabled) {
    if (enabled) {
        ada::internal::g_log_enabled |= ada::internal::LOG_CALLBACKS;
    } else {
        ada::internal::g_log_enabled &= ~ada::internal::LOG_CALLBACKS;
    }
}

void agent_set_log_events(int enabled) {
    if (enabled) {
        ada::internal::g_log_enabled |= ada::internal::LOG_EVENTS;
    } else {
        ada::internal::g_log_enabled &= ~ada::internal::LOG_EVENTS;
    }
}

// Set multiple categories at once
void agent_set_log_mask(uint32_t mask) {
    ada::internal::g_log_enabled = mask;
}

// Get current log mask
uint32_t agent_get_log_mask() {
    return ada::internal::g_log_enabled;
}

// Agent initialization - the main entry point called by Frida
__attribute__((visibility("default")))
void agent_init(const gchar* data, gint data_size) {
    // Check for debugger wait request immediately
    ada::internal::wait_for_debugger();

    // Initialize GUM first before using any GLib functions
    gum_init_embedded();
    // Ensure log mask is set up
    // Default is LIFECYCLE | HOOKS
    
    LOG_LIFECYCLE("[Agent] agent_init called! data_size=%d\n", data_size);
    
    if (ada::internal::g_agent_verbose)     LOG_LIFECYCLE("[Agent] Initializing with data size: %d\n", data_size);
    
    // Parse initialization data
    uint32_t arg_host = 0, arg_sid = 0;
    ada::internal::parse_init_payload(data, data_size, &arg_host, &arg_sid);
    ada::internal::g_host_pid = arg_host;
    ada::internal::g_session_id = arg_sid;
    
    if (ada::internal::g_agent_verbose)     LOG_LIFECYCLE("[Agent] Parsed host_pid=%u, session_id=%u\n", 
            ada::internal::g_host_pid, ada::internal::g_session_id);
    
    // Get singleton context (thread-safe, initialized once)
    // Note: We use the global unique_ptr directly here as we are in the same translation unit
    // and friend/namespace access allows it.
    if (!ada::internal::g_agent_context) {
        ada::internal::g_agent_context = std::make_unique<ada::internal::AgentContext>();
    }
    
    // Initialize context
    if (!ada::internal::g_agent_context->initialize(ada::internal::g_host_pid, ada::internal::g_session_id)) {
        LOG_LIFECYCLE("[Agent] Failed to create agent context\n");
        return;
    }

    // Install hooks
    LOG_LIFECYCLE("[Agent] About to install hooks...\n");
    ada::internal::g_agent_context->install_hooks();
    LOG_LIFECYCLE("[Agent] Hooks installation completed\n");
}

// Agent cleanup
__attribute__((destructor)) 
static void agent_deinit() {
    LOG_LIFECYCLE("[Agent] Begin to deinit\n");
    bool needs_reset = false;
    {
        // Check if context exists and needs reset
        LOG_LIFECYCLE("[Agent] Will destruct context\n");
        needs_reset = (ada::internal::g_agent_context != nullptr);
    }
    
    if (needs_reset) {
        ada::internal::g_agent_context.reset();
        LOG_LIFECYCLE("[Agent] Did destruct context (needs reset: %s)\n", needs_reset ? "true" : "false");
    }
    
    LOG_LIFECYCLE("[Agent] Will deinit\n");
    
    if (needs_reset) {
        LOG_LIFECYCLE("[Agent] Did deinit\n");
    } else {
        LOG_LIFECYCLE("[Agent] Did not deinit\n");
    }
}

} // extern "C"

