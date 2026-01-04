#ifndef AGENT_INTERNAL_H
#define AGENT_INTERNAL_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <pthread.h>

// C headers
extern "C" {
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/thread_registry.h>
#include <frida-gum.h>
// ADA TLS API
#include <tracer_backend/ada/thread.h>
// Agent mode state machine (C API)
#include <tracer_backend/utils/agent_mode.h>
}

// Include HookRegistry for symbol table persistence
#include <tracer_backend/agent/hook_registry.h>

// Forward declarations for C++ classes
namespace ada {
namespace internal {
    class RingBuffer;
    class ThreadRegistry;
}}

namespace ada {
namespace internal {

// ============================================================================
// Thread Local Data for Reentrancy Protection
// ============================================================================

// Custom deleter for GObjects (RAII)
struct GObjectDeleter {
    void operator()(gpointer ptr) {
        if (ptr) g_object_unref(ptr);
    }
};

class ThreadLocalData {
public:
    ThreadLocalData();
    ~ThreadLocalData() = default;

    uint32_t thread_id() const { return thread_id_; }
    uint32_t call_depth() const { return call_depth_; }
    
    void increment_depth() { call_depth_++; }
    void decrement_depth() { 
        if (call_depth_ > 0) call_depth_--; 
    }
    
    bool is_in_handler() const { return in_handler_.load(std::memory_order_acquire); }
    void enter_handler() { in_handler_.store(true, std::memory_order_release); }
    void exit_handler() { in_handler_.store(false, std::memory_order_release); }
    
    void record_reentrancy_attempt() { reentrancy_attempts_++; }
    uint64_t reentrancy_attempts() const { return reentrancy_attempts_; }

private:
    uint32_t thread_id_;
    uint32_t call_depth_;
    std::atomic<bool> in_handler_;
    uint64_t reentrancy_attempts_;
};

// ============================================================================
// Hook Data Structure
// ============================================================================

struct HookData {
    class AgentContext* context;
    uint64_t function_id;
    std::string function_name;
    GumAddress function_address;
    GumInvocationListener* listener{};  // Keep listener alive

    HookData(AgentContext* ctx, uint64_t id, const std::string& name, GumAddress addr)
        : context(ctx), function_id(id), function_name(name), function_address(addr) {}
        
    ~HookData();
};

// ============================================================================
// Hook Result for Reporting
// ============================================================================

struct HookResult {
    std::string name;
    GumAddress address;
    uint64_t id;
    bool success;
    
    HookResult(const std::string& n, GumAddress a, uint64_t i, bool s)
        : name(n), address(a), id(i), success(s) {}
};

// ============================================================================
// Shared Memory Reference (RAII wrapper)
// ============================================================================

class SharedMemoryRef {
public:
    SharedMemoryRef() : ref_(nullptr) {}
    explicit SharedMemoryRef(SharedMemoryRef* ref) : ref_(ref) {}
    ~SharedMemoryRef();
    
    SharedMemoryRef(const SharedMemoryRef&) = delete;
    SharedMemoryRef& operator=(const SharedMemoryRef&) = delete;
    
    SharedMemoryRef(SharedMemoryRef&& other) noexcept : ref_(other.ref_) {
        other.ref_ = nullptr;
    }
    
    SharedMemoryRef& operator=(SharedMemoryRef&& other) noexcept {
        if (this != &other) {
            if (ref_) {
                cleanup();
            }
            ref_ = other.ref_;
            other.ref_ = nullptr;
        }
        return *this;
    }
    
    void* get_address() const;
    bool is_valid() const { return ref_ != nullptr; }
    
    // Factory method for opening shared memory
    static SharedMemoryRef open_unique(uint32_t role, uint32_t host_pid, uint32_t session_id, size_t size);

private:
    void cleanup();
    SharedMemoryRef* ref_;  // Opaque pointer to C implementation
};

// ============================================================================
// Main Agent Context
// ============================================================================

class AgentContext {
public:
    AgentContext();
    ~AgentContext();
    
    // Initialize with host PID and session ID
    bool initialize(uint32_t host_pid, uint32_t session_id);
    
    // Hook management
    void install_hooks();
    std::vector<HookResult> get_hook_results() const { return hook_results_; }
    
    // Statistics
    uint64_t events_emitted() const { return events_emitted_.load(); }
    uint64_t reentrancy_blocked() const { return reentrancy_blocked_.load(); }
    uint64_t stack_capture_failures() const { return stack_capture_failures_.load(); }
    
    void increment_events_emitted() { events_emitted_.fetch_add(1); }
    void increment_reentrancy_blocked() { reentrancy_blocked_.fetch_add(1); }
    void increment_stack_capture_failures() { stack_capture_failures_.fetch_add(1); }
    
    // Ring buffer access
    RingBuffer* index_ring() { return index_ring_.get(); }
    RingBuffer* detail_ring() { return detail_ring_.get(); }
    ControlBlock* control_block() { return control_block_; }
    
    // Hook tracking
    uint32_t hooks_attempted() const { return num_hooks_attempted_; }
    uint32_t hooks_successful() const { return num_hooks_successful_; }

    // Agent mode state machine tick
    void update_registry_mode(uint64_t now_ns, uint64_t hb_timeout_ns);

    // Symbol table persistence: export hook registry as JSON for manifest
    std::string export_hook_registry_json() const { return hook_registry_.export_to_json(); }

    // Access hook registry for metadata capture during hook installation
    ada::agent::HookRegistry& hook_registry() { return hook_registry_; }

private:
    // Shared memory segments
    SharedMemoryRef shm_control_;
    SharedMemoryRef shm_index_;
    SharedMemoryRef shm_detail_;
    SharedMemoryRef shm_registry_;
    
    // Ring buffers (using unique_ptr with custom deleter)
    std::unique_ptr<RingBuffer, void(*)(RingBuffer*)> index_ring_;
    std::unique_ptr<RingBuffer, void(*)(RingBuffer*)> detail_ring_;
    
    // Control block
    ControlBlock* control_block_;
    
    // Frida interceptor
    std::unique_ptr<GumInterceptor, GObjectDeleter> interceptor_;
    
    // Hook tracking
    std::vector<std::unique_ptr<HookData>> hooks_;
    std::vector<HookResult> hook_results_;
    uint32_t num_hooks_attempted_;
    uint32_t num_hooks_successful_;
    
    // Session info
    uint32_t host_pid_;
    uint32_t session_id_;
    uint64_t module_base_;
    std::string agent_path_; // Path to the agent library itself (to avoid self-hooking)
    
    // Statistics (atomic for thread safety)
    std::atomic<uint64_t> events_emitted_;
    std::atomic<uint64_t> reentrancy_blocked_;
    std::atomic<uint64_t> stack_capture_failures_;

    // Agent mode state machine
    AgentModeState agent_mode_state_;

    // Hook registry for symbol table persistence (Phase 1)
    ada::agent::HookRegistry hook_registry_;

    // Helper methods
    bool open_shared_memory();
    bool attach_ring_buffers();
    void hook_function(const char* name);
    void send_hook_summary();
};

// ============================================================================
// Global Context Management
// ============================================================================

// Get or create the singleton agent context
AgentContext* get_agent_context();

// Thread-local storage management
ThreadLocalData* get_thread_local();

// ============================================================================
// Utility Functions
// ============================================================================

// Hash function for stable function IDs
uint32_t hash_string(const char* str);

// Platform-specific timestamp
uint64_t platform_get_timestamp();

// Safe stack capture with signal handling
size_t safe_stack_capture(void* dest, void* stack_ptr, size_t max_size);

// Parse initialization payload
void parse_init_payload(const char* data, int data_size, 
                       uint32_t* out_host_pid, uint32_t* out_session_id);

} // namespace internal
} // namespace ada

#endif // AGENT_INTERNAL_H
