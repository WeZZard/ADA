#include "agent_internal.h"

// C++ headers
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <memory>
#include <mutex>

// System headers
#include <unistd.h>
#include <pthread.h>

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
}

// Include C++ implementation headers
#include "../utils/ring_buffer_private.h"

// Forward declare the C callbacks
extern "C" {
void on_enter_callback(GumInvocationContext* ic, gpointer user_data);
void on_leave_callback(GumInvocationContext* ic, gpointer user_data);
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

// For initialization parsing
static uint32_t g_host_pid = UINT32_MAX;
static uint32_t g_session_id = UINT32_MAX;

// Signal handling for safe stack capture
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
// SharedMemoryRef Implementation  
// ============================================================================

SharedMemoryRef::~SharedMemoryRef() {
    cleanup();
}

void SharedMemoryRef::cleanup() {
    if (ref_) {
        shared_memory_destroy(reinterpret_cast<::SharedMemoryRef>(ref_));
        ref_ = nullptr;
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
    if (rb) {
        ring_buffer_destroy(reinterpret_cast<::RingBuffer*>(rb));
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
    , stack_capture_failures_(0) {
}

AgentContext::~AgentContext() {
    g_debug("[Agent] Shutting down (emitted=%llu events, blocked=%llu reentrancy)\n",
            static_cast<unsigned long long>(events_emitted_.load()),
            static_cast<unsigned long long>(reentrancy_blocked_.load()));
    
    // Print final statistics
    g_debug("[Agent] Final stats: events_emitted=%llu, reentrancy_blocked=%llu, "
            "stack_failures=%llu\n",
            static_cast<unsigned long long>(events_emitted_.load()),
            static_cast<unsigned long long>(reentrancy_blocked_.load()),
            static_cast<unsigned long long>(stack_capture_failures_.load()));
    
    // Cleanup Frida interceptor
    if (interceptor_) {
        g_object_unref(interceptor_);
        interceptor_ = nullptr;
    }
    
    // Ring buffers and shared memory are cleaned up by destructors
}

bool AgentContext::initialize(uint32_t host_pid, uint32_t session_id) {
    host_pid_ = host_pid;
    session_id_ = session_id;
    
    g_debug("[Agent] Initializing with host_pid=%u, session_id=%u\n", 
            host_pid_, session_id_);
    
    if (!open_shared_memory()) {
        g_debug("[Agent] Failed to open shared memory\n");
        return false;
    }
    
    if (!attach_ring_buffers()) {
        g_debug("[Agent] Failed to attach ring buffers\n");
        return false;
    }
    
    // Get Frida interceptor
    interceptor_ = gum_interceptor_obtain();
    g_debug("[Agent] Got interceptor: %p\n", interceptor_);
    
    return true;
}

bool AgentContext::open_shared_memory() {
    g_debug("[Agent] Opening shared memory segments...\n");
    
    shm_control_ = SharedMemoryRef::open_unique(0, host_pid_, 
                                               session_id_, 4096);
    shm_index_ = SharedMemoryRef::open_unique(1, host_pid_, 
                                             session_id_, 32 * 1024 * 1024);
    shm_detail_ = SharedMemoryRef::open_unique(2, host_pid_, 
                                              session_id_, 32 * 1024 * 1024);
    
    if (!shm_control_.is_valid() || !shm_index_.is_valid() || 
        !shm_detail_.is_valid()) {
        g_debug("[Agent] Failed to open shared memory\n");
        return false;
    }
    
    g_debug("[Agent] Successfully opened all shared memory segments\n");
    
    // Map control block
    control_block_ = static_cast<ControlBlock*>(shm_control_.get_address());
    g_debug("[Agent] Control block mapped at %p\n", control_block_);
    
    return true;
}

bool AgentContext::attach_ring_buffers() {
    void* index_addr = shm_index_.get_address();
    void* detail_addr = shm_detail_.get_address();
    
    g_debug("[Agent] Ring buffer addresses: index=%p, detail=%p\n", 
            index_addr, detail_addr);
    
    // Attach to existing ring buffers
    auto* index_rb = ring_buffer_attach(index_addr, 32 * 1024 * 1024, 
                                        sizeof(IndexEvent));
    index_ring_.reset(reinterpret_cast<ada::internal::RingBuffer*>(index_rb));
    g_debug("[Agent] Index ring attached: %p\n", index_ring_.get());
    
    auto* detail_rb = ring_buffer_attach(detail_addr, 32 * 1024 * 1024, 
                                         sizeof(DetailEvent));
    detail_ring_.reset(reinterpret_cast<ada::internal::RingBuffer*>(detail_rb));
    g_debug("[Agent] Detail ring attached: %p\n", detail_ring_.get());
    
    return index_ring_ && detail_ring_;
}

void AgentContext::hook_function(const char* name) {
    g_debug("[Agent] Finding symbol: %s\n", name);
    num_hooks_attempted_++;
    
    // Generate stable function ID from name
    uint32_t function_id = hash_string(name);
    
    // Find function address
    GumModule* main_module = gum_process_get_main_module();
    GumAddress func_addr = gum_module_find_symbol_by_name(main_module, name);
    
    // Record result
    hook_results_.emplace_back(name, func_addr, function_id, func_addr != 0);
    
    if (func_addr != 0) {
        g_debug("[Agent] Found symbol: %s at 0x%llx\n", name, func_addr);
        
        // Create hook data
        auto hook = std::make_unique<HookData>(this, function_id, name, func_addr);
        
        // Create listener with C callbacks (defined in extern "C" block below)
        GumInvocationListener* listener = gum_make_call_listener(
            on_enter_callback, 
            on_leave_callback,
            hook.get(),
            nullptr  // No destructor needed, we manage lifetime
        );
        
        gum_interceptor_attach(interceptor_, 
                              GSIZE_TO_POINTER(func_addr),
                              listener, 
                              hook.get(), 
                              GUM_ATTACH_FLAGS_NONE);
        
        hooks_.push_back(std::move(hook));
        num_hooks_successful_++;
        
        g_debug("[Agent] Hooked: %s at 0x%llx (id=%u)\n", name,
                static_cast<unsigned long long>(func_addr), function_id);
    } else {
        g_debug("[Agent] Failed to find: %s\n", name);
    }
}

void AgentContext::install_hooks() {
    // Begin transaction for batch hooking
    gum_interceptor_begin_transaction(interceptor_);
    g_debug("[Agent] Beginning hook installation...\n");
    
    // Define functions to hook
    const char* functions_to_hook[] = {
        // test_cli functions
        "fibonacci", "process_file", "calculate_pi", "recursive_function",
        // test_runloop functions
        "simulate_network", "monitor_file", "dispatch_work", "signal_handler",
        "timer_callback", 
        nullptr
    };
    
    // Hook each function
    for (const char** fname = functions_to_hook; *fname != nullptr; fname++) {
        hook_function(*fname);
    }
    
    // End transaction
    gum_interceptor_end_transaction(interceptor_);
    
    // Send hook summary
    send_hook_summary();
    
    g_debug("[Agent] Initialization complete: %u/%u hooks installed\n",
            num_hooks_successful_, num_hooks_attempted_);
}

void AgentContext::send_hook_summary() {
    // For now, just print the summary
    // TODO: Implement proper Frida messaging when API is available
    g_debug("[Agent] Hook Summary: attempted=%u, successful=%u, failed=%u\n",
            num_hooks_attempted_, num_hooks_successful_,
            num_hooks_attempted_ - num_hooks_successful_);
    
    for (const auto& result : hook_results_) {
        g_debug("[Agent]   %s: address=0x%llx, id=%u, %s\n", 
                result.name.c_str(),
                static_cast<unsigned long long>(result.address), 
                result.id,
                result.success ? "hooked" : "failed");
    }
}

// ============================================================================
// TLS Management
// ============================================================================

static void tls_destructor(void* data) {
    if (data) {
        delete static_cast<ThreadLocalData*>(data);
    }
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
    return mach_absolute_time();
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
            // Support raw hex without 0x prefix
            unsigned int parsed = UINT32_MAX;
            if (strncmp(val, "0x", 2) == 0 || strncmp(val, "0X", 2) == 0) {
                parsed = static_cast<unsigned int>(strtoul(val, nullptr, 16));
            } else {
                // Detect hex if contains [a-fA-F]
                bool looks_hex = false;
                for (const char* p = val; *p; p++) {
                    if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                        looks_hex = true;
                        break;
                    }
                }
                parsed = static_cast<unsigned int>(
                    strtoul(val, nullptr, looks_hex ? 16 : 10));
            }
            *out_session_id = static_cast<uint32_t>(parsed);
        }
    }
}

// ============================================================================
// Hook Callbacks (C++ implementation)
// ============================================================================

static void capture_index_event(AgentContext* ctx, HookData* hook, 
                               ThreadLocalData* tls, EventKind kind) {
    if (!ctx->control_block()->index_lane_enabled) {
        g_debug("[Agent] Index lane disabled\n");
        return;
    }
    
    g_debug("[Agent] Capturing index event\n");
    
    IndexEvent event = {};
    event.timestamp = platform_get_timestamp();
    event.function_id = hook->function_id;
    event.thread_id = tls->thread_id();
    event.event_kind = kind;
    event.call_depth = tls->call_depth();
    event._padding = 0;
    
    if (ring_buffer_write(reinterpret_cast<::RingBuffer*>(ctx->index_ring()), &event)) {
        g_debug("[Agent] Wrote index event\n");
        ctx->increment_events_emitted();
    } else {
        g_debug("[Agent] Failed to write index event\n");
    }
}

static void capture_detail_event(AgentContext* ctx, HookData* hook,
                                ThreadLocalData* tls, EventKind kind,
                                GumCpuContext* cpu) {
    if (!ctx->control_block()->detail_lane_enabled) {
        g_debug("[Agent] Detail lane disabled\n");
        return;
    }
    
    g_debug("[Agent] Capturing detail event\n");
    
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
    
    if (ring_buffer_write(reinterpret_cast<::RingBuffer*>(ctx->detail_ring()), &detail)) {
        g_debug("[Agent] Wrote detail event\n");
        ctx->increment_events_emitted();
    } else {
        g_debug("[Agent] Failed to write detail event\n");
    }
}

// C-style callbacks for Frida (must be extern "C")
extern "C" {

void on_enter_callback(GumInvocationContext* ic, gpointer user_data) {
    auto* hook = static_cast<HookData*>(user_data);
    auto* ctx = hook->context;
    auto* tls = get_thread_local();
    if (!tls) return;
    
    // Enhanced reentrancy guard with metrics
    if (tls->is_in_handler()) {
        tls->record_reentrancy_attempt();
        ctx->increment_reentrancy_blocked();
        return;
    }
    tls->enter_handler();
    
    g_debug("[Agent] on_enter: %s\n", hook->function_name.c_str());
    
    // Increment call depth
    tls->increment_depth();
    
    // Capture index event
    capture_index_event(ctx, hook, tls, EVENT_KIND_CALL);
    
    // Capture detail event with full ABI registers and optional stack
    capture_detail_event(ctx, hook, tls, EVENT_KIND_CALL, ic->cpu_context);
    
    tls->exit_handler();
}

void on_leave_callback(GumInvocationContext* ic, gpointer user_data) {
    auto* hook = static_cast<HookData*>(user_data);
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
    
    g_debug("[Agent] on_leave: %s\n", hook->function_name.c_str());
    
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

} // extern "C"

} // namespace internal
} // namespace ada

// ============================================================================
// Agent Entry Points (extern "C")
// ============================================================================

extern "C" {

// Agent initialization - the main entry point called by Frida
__attribute__((visibility("default")))
void agent_init(const gchar* data, gint data_size) {
    // Initialize GUM first before using any GLib functions
    gum_init_embedded();
    
    g_debug("[Agent] Initializing with data size: %d\n", data_size);
    
    // Parse initialization data
    uint32_t arg_host = 0, arg_sid = 0;
    ada::internal::parse_init_payload(data, data_size, &arg_host, &arg_sid);
    ada::internal::g_host_pid = arg_host;
    ada::internal::g_session_id = arg_sid;
    
    g_debug("[Agent] Parsed host_pid=%u, session_id=%u\n", 
            ada::internal::g_host_pid, ada::internal::g_session_id);
    
    // Get singleton context (thread-safe, initialized once)
    auto* ctx = ada::internal::get_agent_context();
    
    if (!ctx) {
        g_debug("[Agent] Failed to create agent context\n");
        return;
    }
    
    // Install hooks
    ctx->install_hooks();
}

// Agent cleanup
__attribute__((destructor)) 
void agent_deinit() {
    bool needs_reset = false;
    {
        std::lock_guard<std::mutex> lock(ada::internal::g_context_mutex);
        needs_reset = ada::internal::g_agent_context.get() != nullptr;
        ada::internal::g_agent_context.reset();
    }
    
    pthread_key_delete(ada::internal::g_tls_key);
    if (needs_reset) {
        gum_deinit_embedded();
    }
}

} // extern "C"
