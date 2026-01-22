#include "frida_controller_internal.h"
#include "../utils/ring_buffer_private.h"
extern "C" {
#include <tracer_backend/utils/control_block_ipc.h>
}
extern "C" {
#include <tracer_backend/utils/ring_buffer.h>
}
#include "../utils/thread_registry_private.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <vector>
#include <cctype>

#ifdef __APPLE__
#include <crt_externs.h>
#include <mach/mach_time.h>
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif

namespace {
using AttachSyncFn =
    FridaSession* (*)(FridaDevice*, guint, FridaSessionOptions*, GCancellable*, GError**);

AttachSyncFn attach_sync_fn = frida_device_attach_sync;
}

extern "C" void frida_controller_test_set_attach_sync(AttachSyncFn fn) {
    attach_sync_fn = fn ? fn : frida_device_attach_sync;
}

extern "C" void frida_controller_test_reset_attach_sync(void) {
    attach_sync_fn = frida_device_attach_sync;
}

namespace ada {
namespace internal {

// ============================================================================
// Static initialization/cleanup for Frida
// ============================================================================

__attribute__((constructor))
static void frida_init_per_process() {
    frida_init();
}

__attribute__((destructor))
static void frida_deinit_per_process() {
    frida_deinit();
}

// ============================================================================
// Startup timeout configuration helpers (M1_E6_I1)
// ============================================================================

StartupTimeoutConfig StartupTimeoutConfig::from_env() {
    StartupTimeoutConfig cfg;

    if (const char* env = getenv("ADA_STARTUP_WARM_UP_DURATION")) {
        char* end = nullptr;
        long v = strtol(env, &end, 10);
        if (end != env && *end == '\0' && v > 0) {
            cfg.startup_ms = static_cast<uint32_t>(v);
        }
    }

    if (const char* env = getenv("ADA_STARTUP_PER_SYMBOL_COST")) {
        char* end = nullptr;
        long v = strtol(env, &end, 10);
        if (end != env && *end == '\0' && v >= 0) {
            cfg.per_symbol_ms = static_cast<uint32_t>(v);
        }
    }

    if (const char* env = getenv("ADA_STARTUP_TIMEOUT_TOLERANCE")) {
        char* end = nullptr;
        double v = strtod(env, &end);
        if (end != env && *end == '\0' && v >= 0.0) {
            cfg.tolerance_pct = v;
        }
    }

    if (const char* env = getenv("ADA_STARTUP_TIMEOUT")) {
        char* end = nullptr;
        long v = strtol(env, &end, 10);
        if (end != env && *end == '\0' && v > 0) {
            cfg.override_ms = static_cast<uint32_t>(v);
        }
    }

    // If waiting for debugger, disable timeout (set to max)
    if (getenv("ADA_WAIT_FOR_DEBUGGER")) {
        cfg.override_ms = UINT32_MAX;
    }

    return cfg;
}

uint32_t StartupTimeoutConfig::compute_timeout_ms(uint32_t symbol_count) const {
    if (override_ms > 0) {
        return override_ms;
    }

    double estimated = static_cast<double>(startup_ms) +
                       static_cast<double>(symbol_count) * static_cast<double>(per_symbol_ms);
    double timeout = estimated * (1.0 + tolerance_pct);
    if (timeout < 0.0) {
        timeout = 0.0;
    }
    if (timeout > static_cast<double>(UINT32_MAX)) {
        timeout = static_cast<double>(UINT32_MAX);
    }
    return static_cast<uint32_t>(timeout);
}

namespace {

struct ScriptLoadContext {
    GCancellable* cancellable;
    GMainLoop* loop;
    GError* error;
    bool completed;
    bool timed_out;
};

static void on_script_load_finished(GObject* source_object,
                                    GAsyncResult* res,
                                    gpointer user_data) {
    auto* ctx = static_cast<ScriptLoadContext*>(user_data);
    if (!ctx || ctx->completed) {
        return;
    }

    FridaScript* script = FRIDA_SCRIPT(source_object);
    frida_script_load_finish(script, res, &ctx->error);
    ctx->completed = true;

    if (ctx->loop && g_main_loop_is_running(ctx->loop)) {
        g_main_loop_quit(ctx->loop);
    }
}

static gboolean on_script_load_timeout_cb(gpointer user_data) {
    auto* ctx = static_cast<ScriptLoadContext*>(user_data);
    if (!ctx || ctx->completed) {
        return G_SOURCE_REMOVE;
    }

    ctx->timed_out = true;
    if (ctx->cancellable) {
        g_cancellable_cancel(ctx->cancellable);
    }
    if (ctx->loop && g_main_loop_is_running(ctx->loop)) {
        g_main_loop_quit(ctx->loop);
    }

    return G_SOURCE_REMOVE;
}

} // namespace

// ============================================================================
// Constructor/Destructor
// ============================================================================

FridaController::FridaController(const std::string& output_dir)
    : output_dir_(output_dir)
{
    // Initialize state
    state_ = PROCESS_STATE_INITIALIZED;
    spawn_method_ = SpawnMethod::None;

    // Load startup timeout configuration from environment
    startup_cfg_ = StartupTimeoutConfig::from_env();
    
    // Create GLib main context and loop
    main_context_ = g_main_context_new();
    g_main_context_push_thread_default(main_context_);
    main_loop_ = g_main_loop_new(main_context_, TRUE);
    
    // Create device manager
    manager_ = frida_device_manager_new();
    
    // Get local device
    GError* error = nullptr;
    FridaDeviceList* devices = frida_device_manager_enumerate_devices_sync(
        manager_, nullptr, &error);
    
    if (error) {
        g_printerr("Failed to enumerate devices: %s\n", error->message);
        g_error_free(error);
        cleanup_frida_objects();
        throw std::runtime_error("Failed to enumerate Frida devices");
    }
    
    // Find local device
    gint num_devices = frida_device_list_size(devices);
    for (gint i = 0; i < num_devices; i++) {
        FridaDevice* device = frida_device_list_get(devices, i);
        if (frida_device_get_dtype(device) == FRIDA_DEVICE_TYPE_LOCAL) {
            device_ = static_cast<FridaDevice*>(g_object_ref(device));
        }
        g_object_unref(device);
    }
    
    frida_unref(devices);
    
    if (!device_) {
        cleanup_frida_objects();
        throw std::runtime_error("Failed to find local Frida device");
    }
    
    // Initialize shared memory
    if (!initialize_shared_memory()) {
        cleanup_frida_objects();
        throw std::runtime_error("Failed to initialize shared memory");
    }
    
    // Initialize ring buffers
    if (!initialize_ring_buffers()) {
        cleanup_frida_objects();
        throw std::runtime_error("Failed to initialize ring buffers");
    }
    
    // Create and start C-based drain thread (with ATF session management)
    DrainConfig drain_config;
    drain_config_default(&drain_config);
    drain_ = drain_thread_create(registry_, &drain_config);
    if (!drain_) {
        cleanup_frida_objects();
        throw std::runtime_error("Failed to create drain thread");
    }
    drain_thread_set_control_block(drain_, control_block_);

    if (drain_thread_start(drain_) != 0) {
        drain_thread_destroy(drain_);
        drain_ = nullptr;
        cleanup_frida_objects();
        throw std::runtime_error("Failed to start drain thread");
    }

    start_registry_maintenance();
}

FridaController::~FridaController() {
    stop_registry_maintenance();

    // Stop ATF session first (finalizes files)
    stop_atf_session();

    // Stop and destroy C-based drain thread
    if (drain_) {
        drain_thread_stop(drain_);
        drain_thread_destroy(drain_);
        drain_ = nullptr;
    }

    // Deinitialize thread registry (testing/runtime hygiene)
    if (registry_) {
        thread_registry_deinit(registry_);
        registry_ = nullptr;
    }

    // Cleanup Frida objects
    cleanup_frida_objects();

    // Cleanup event loop
    if (main_loop_) {
        g_main_loop_quit(main_loop_);
        g_main_loop_unref(main_loop_);
    }
    
    if (main_context_) {
        g_main_context_pop_thread_default(main_context_);
        g_main_context_unref(main_context_);
    }
}

void FridaController::start_registry_maintenance() {
    // LCOV_EXCL_START - Covered indirectly in integration workflows.
    if (maintenance_thread_.joinable()) {
        return;
    }
    // LCOV_EXCL_STOP
    maintenance_stop_.store(false);
    maintenance_thread_ = std::thread([this]() { registry_maintenance_loop(); });
}

void FridaController::stop_registry_maintenance() {
    maintenance_stop_.store(true);
    if (maintenance_thread_.joinable()) {
        maintenance_thread_.join();
    }
}

void FridaController::registry_maintenance_loop() {
    constexpr uint32_t kTickMs = 100;
    constexpr uint32_t kWarmupTicks = 5;
    uint32_t warmup_ticks = 0;

    while (!maintenance_stop_.load()) {
        if (control_block_) {
            uint64_t now_ns = static_cast<uint64_t>(g_get_monotonic_time()) * 1000;
            cb_update_heartbeat_ns(control_block_, now_ns);

            if (cb_get_registry_ready(control_block_) != 0) {
                uint32_t mode = cb_get_registry_mode(control_block_);
                if (mode == REGISTRY_MODE_DUAL_WRITE) {
                    if (++warmup_ticks >= kWarmupTicks) {
                        cb_set_registry_mode(control_block_, REGISTRY_MODE_PER_THREAD_ONLY);
                    }
                } else {
                    warmup_ticks = 0;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
    }
}

// ============================================================================
// ATF Session Management
// ============================================================================

bool FridaController::start_atf_session() {
    if (!drain_) {
        g_printerr("[Controller] Cannot start ATF session: drain thread not initialized\n");
        return false;
    }

    if (!session_dir_.empty()) {
        g_debug("[Controller] ATF session already started: %s\n", session_dir_.c_str());
        return true;  // Already started
    }

    // Build session directory path: output_dir/session_YYYYMMDD_HHMMSS/pid_XXXXX
    char timestamp[64];
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);

    char session_path[1024];
    snprintf(session_path, sizeof(session_path),
             "%s/session_%s/pid_%u",
             output_dir_.c_str(), timestamp, static_cast<unsigned int>(pid_));

    session_dir_ = session_path;

    // Create session directory hierarchy
    char mkdir_cmd[1100];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s'", session_dir_.c_str());
    int rc = system(mkdir_cmd);
    if (rc != 0) {
        g_printerr("[Controller] Failed to create session directory: %s\n", session_dir_.c_str());
        session_dir_.clear();
        return false;
    }

    // Start ATF session in drain thread
    rc = drain_thread_start_session(drain_, session_dir_.c_str());
    if (rc != 0) {
        g_printerr("[Controller] Failed to start ATF session: %d\n", rc);
        session_dir_.clear();
        return false;
    }

    g_print("[Controller] ATF session started: %s\n", session_dir_.c_str());
    return true;
}

void FridaController::stop_atf_session() {
    if (!drain_ || session_dir_.empty()) {
        return;  // Nothing to stop
    }

    // Read symbol table from agent temp file (Phase 1: symbol resolution)
    {
        char symbols_path[256];
        uint32_t host_pid = shared_memory_get_pid();
        uint32_t session_id = shared_memory_get_session_id();
        snprintf(symbols_path, sizeof(symbols_path), "/tmp/ada_symbols_%u_%08x.json",
                 host_pid, session_id);

        FILE* symbols_file = fopen(symbols_path, "r");
        if (symbols_file) {
            // Get file size
            fseek(symbols_file, 0, SEEK_END);
            long file_size = ftell(symbols_file);
            fseek(symbols_file, 0, SEEK_SET);

            if (file_size > 0 && file_size < 10 * 1024 * 1024) {  // Max 10MB
                char* buffer = (char*)malloc(static_cast<size_t>(file_size) + 1);
                if (buffer) {
                    size_t read_size = fread(buffer, 1, static_cast<size_t>(file_size), symbols_file);
                    buffer[read_size] = '\0';
                    drain_thread_set_symbol_table(drain_, buffer);
                    free(buffer);
                    g_debug("[Controller] Loaded symbol table: %zu bytes\n", read_size);
                }
            }
            fclose(symbols_file);

            // Clean up temp file
            unlink(symbols_path);
        }
    }

    drain_thread_stop_session(drain_);
    g_print("[Controller] ATF session finalized: %s\n", session_dir_.c_str());
    session_dir_.clear();
}

// ============================================================================
// Initialization helpers
// ============================================================================

std::string FridaController::build_shm_name(const char* role, pid_t pid_hint) {
    char name[256];
    uint32_t sid = shared_memory_get_session_id();
    pid_t pid_part = (pid_hint > 0) ? pid_hint : getpid();
    snprintf(name, sizeof(name), "%s_%s_%d_%08x", 
             ADA_SHM_PREFIX, role, static_cast<int>(pid_part), 
             static_cast<unsigned int>(sid));
    return std::string(name);
}

bool FridaController::initialize_shared_memory() {
    // Create shared memory with the controller's PID so agent can find it
    uint32_t controller_pid = shared_memory_get_pid();
    uint32_t session_id = shared_memory_get_session_id();
    
    g_debug("Creating shared memory with controller_pid: %u, session_id: %u\n", 
            controller_pid, session_id);
    
    // Create control block
    SharedMemoryRef control_ref = shared_memory_create_unique(
        ADA_ROLE_CONTROL, controller_pid, session_id, 
        CONTROL_BLOCK_SIZE, nullptr, 0);
    if (!control_ref) {
        return false;
    }
    shm_control_.reset(control_ref);
    g_debug("Created control block shared memory: %s\n", 
            shared_memory_get_name(control_ref));
    
    // Create index lane
    SharedMemoryRef index_ref = shared_memory_create_unique(
        ADA_ROLE_INDEX, controller_pid, session_id,
        INDEX_LANE_SIZE, nullptr, 0);
    if (!index_ref) {
        return false;
    }
    shm_index_.reset(index_ref);
    g_debug("Created index lane shared memory: %s\n", 
            shared_memory_get_name(index_ref));
    
    // Create detail lane
    SharedMemoryRef detail_ref = shared_memory_create_unique(
        ADA_ROLE_DETAIL, controller_pid, session_id,
        DETAIL_LANE_SIZE, nullptr, 0);
    if (!detail_ref) {
        return false;
    }
    shm_detail_.reset(detail_ref);
    g_debug("Created detail lane shared memory: %s\n", 
            shared_memory_get_name(detail_ref));
    
    // Initialize control block
    control_block_ = static_cast<ControlBlock*>(
        shared_memory_get_address(shm_control_.get()));
    control_block_->process_state = PROCESS_STATE_INITIALIZED;
    control_block_->flight_state = FLIGHT_RECORDER_IDLE;
    control_block_->index_lane_enabled = 1;
    control_block_->detail_lane_enabled = 1;
    control_block_->pre_roll_ms = 1000;
    control_block_->post_roll_ms = 1000;
    // Init IPC fields to defaults
    cb_set_registry_ready(control_block_, 0);
    cb_set_registry_version(control_block_, 0);
    cb_set_registry_epoch(control_block_, 0);
    cb_set_registry_mode(control_block_, REGISTRY_MODE_GLOBAL_ONLY);
    cb_set_heartbeat_ns(control_block_, 0);

    // Optional: allow disabling registry via env (verification / fallback)
    bool disable_registry = false;
    if (const char* env = getenv("ADA_DISABLE_REGISTRY")) {
        if (env[0] != '\0' && env[0] != '0') disable_registry = true;
    }

    // Create thread registry shared memory and initialize it (unless disabled)
    size_t registry_size = thread_registry_calculate_memory_size_with_capacity(MAX_THREADS);
    if (!disable_registry) {
        SharedMemoryRef registry_ref = shared_memory_create_unique(
            ADA_ROLE_REGISTRY, controller_pid, session_id,
            registry_size, nullptr, 0);
        if (!registry_ref) {
            return false;
        }
        shm_registry_.reset(registry_ref);
        g_debug("Created registry shared memory: %s\n",
                shared_memory_get_name(registry_ref));

        void* reg_addr = shared_memory_get_address(registry_ref);
        registry_ = thread_registry_init(reg_addr, registry_size);
        if (!registry_) {
            g_debug("Failed to initialize thread registry at %p (size=%zu)\n", reg_addr, registry_size);
            return false;
        }
        // Publish SHM directory (M1_E1_I8)
        control_block_->shm_directory.schema_version = 1;
        control_block_->shm_directory.count = 1; // Only registry arena for now
        auto* e0 = &control_block_->shm_directory.entries[0];
        memset(e0->name, 0, sizeof(e0->name));
        const char* reg_name = shared_memory_get_name(shm_registry_.get());
        if (reg_name && reg_name[0] != '\0') {
            // shared_memory ensures leading '/'; copy as-is
            strncpy(e0->name, reg_name, sizeof(e0->name) - 1);
        }
        e0->size = (uint64_t)registry_size;
        // Publish registry IPC readiness
        cb_set_registry_version(control_block_, 1);
        cb_set_registry_epoch(control_block_, 1);
        cb_set_registry_ready(control_block_, 1);
        // Set initial heartbeat so agent sees a healthy registry immediately
        // This prevents the agent from falling back to GLOBAL_ONLY on first tick
        uint64_t now_ns = static_cast<uint64_t>(g_get_monotonic_time()) * 1000;
        cb_set_heartbeat_ns(control_block_, now_ns);
        // Begin with dual-write to warm up, then controller will transition later
        cb_set_registry_mode(control_block_, REGISTRY_MODE_DUAL_WRITE);
    } else {
        g_debug("Registry disabled by ADA_DISABLE_REGISTRY\n");
    }

    return true;
}

bool FridaController::initialize_ring_buffers() {
    // Create ring buffers using internal C++ classes
    index_ring_ = std::make_unique<RingBuffer>();
    if (!index_ring_->initialize(
            shared_memory_get_address(shm_index_.get()),
            INDEX_LANE_SIZE,
            sizeof(IndexEvent))) {
        return false;
    }
    
    detail_ring_ = std::make_unique<RingBuffer>();
    if (!detail_ring_->initialize(
            shared_memory_get_address(shm_detail_.get()),
            DETAIL_LANE_SIZE,
            sizeof(DetailEvent))) {
        return false;
    }
    
    return true;
}

void FridaController::cleanup_frida_objects() {
    if (script_) {
        frida_script_unload_sync(script_, nullptr, nullptr);
        frida_unref(script_);
        script_ = nullptr;
    }

    if (script_cancellable_) {
        g_object_unref(script_cancellable_);
        script_cancellable_ = nullptr;
    }
    
    if (session_) {
        frida_session_detach_sync(session_, nullptr, nullptr);
        frida_unref(session_);
        session_ = nullptr;
    }
    
    if (device_) {
        frida_unref(device_);
        device_ = nullptr;
    }
    
    if (manager_) {
        frida_device_manager_close_sync(manager_, nullptr, nullptr);
        frida_unref(manager_);
        manager_ = nullptr;
    }
}

// ============================================================================
// ============================================================================
// Process management
// ============================================================================

void FridaController::wait_for_debugger_if_needed() const {
    if (getenv("ADA_WAIT_FOR_DEBUGGER")) {
        printf("[Controller] Waiting for debugger... (PID: %d)\n", pid_);
        printf("[Controller] Run: lldb -p %d\n", pid_);
        printf("[Controller] Or use VS Code 'Attach to Process'\n");
        
        raise(SIGSTOP);
        
        printf("[Controller] Debugger attached! Resuming...\n");
    }
}

int FridaController::spawn_suspended(const char* path, char* const argv[], uint32_t* out_pid) {
    printf("[Controller] Spawning process: %s\n", path);
    
    if (!path) {
        return -1;
    }
    
    state_ = PROCESS_STATE_SPAWNING;
    control_block_->process_state = PROCESS_STATE_SPAWNING;
    
    GError* error = nullptr;
    FridaSpawnOptions* options = frida_spawn_options_new();
    
    // Build argv array
    gint argv_len = 0;
    if (argv) {
        while (argv[argv_len]) {
            argv_len++;
        }
    }
    frida_spawn_options_set_argv(options, const_cast<gchar**>(argv), argv_len);
    
    // Pass session id and host pid to spawned process
    uint32_t sid = shared_memory_get_session_id();
    char sid_hex[16];
    snprintf(sid_hex, sizeof(sid_hex), "%08x", static_cast<unsigned int>(sid));
    pid_t host_pid = shared_memory_get_pid();
    char host_pid_str[32];
    snprintf(host_pid_str, sizeof(host_pid_str), "%d", static_cast<int>(host_pid));
    
    // Build environment
    std::string ada_session = std::string("ADA_SHM_SESSION_ID=") + sid_hex;
    std::string ada_host = std::string("ADA_SHM_HOST_PID=") + host_pid_str;

    // Propagate LLVM_PROFILE_FILE for coverage collection in child processes
    const char* llvm_profile = g_getenv("LLVM_PROFILE_FILE");
    std::string llvm_profile_str;
    if (llvm_profile) {
        llvm_profile_str = std::string("LLVM_PROFILE_FILE=") + llvm_profile;
    }

    // Propagate ADA_SKIP_DSO_HOOKS for testing
    const char* skip_dso = g_getenv("ADA_SKIP_DSO_HOOKS");
    std::string skip_dso_str;
    if (skip_dso) {
        skip_dso_str = std::string("ADA_SKIP_DSO_HOOKS=") + skip_dso;
    }

    // Build envp array dynamically
    std::vector<const char*> envp_vec;
    envp_vec.push_back(g_strdup_printf("PATH=%s", g_getenv("PATH")));
    envp_vec.push_back(g_strdup_printf("HOME=%s", g_get_home_dir()));
    envp_vec.push_back(g_strdup_printf("__CF_USER_TEXT_ENCODING=%s",
                       g_getenv("__CF_USER_TEXT_ENCODING") ?: "0x1F5:0x0:0x0"));
    envp_vec.push_back(ada_session.c_str());
    envp_vec.push_back(ada_host.c_str());

    // Add LLVM_PROFILE_FILE if present
    if (!llvm_profile_str.empty()) {
        envp_vec.push_back(llvm_profile_str.c_str());
    }

    // Add ADA_SKIP_DSO_HOOKS if present
    if (!skip_dso_str.empty()) {
        envp_vec.push_back(skip_dso_str.c_str());
    }

    // Also propagate other coverage-related variables
    const char* rust_cov = g_getenv("RUSTFLAGS");
    std::string rust_cov_str;
    if (rust_cov && strstr(rust_cov, "instrument-coverage")) {
        rust_cov_str = std::string("RUSTFLAGS=") + rust_cov;
        envp_vec.push_back(rust_cov_str.c_str());
    }

    // Propagate ADA_WAIT_FOR_DEBUGGER
    const char* wait_debug = g_getenv("ADA_WAIT_FOR_DEBUGGER");
    std::string wait_debug_str;
    if (wait_debug) {
        wait_debug_str = std::string("ADA_WAIT_FOR_DEBUGGER=") + wait_debug;
        envp_vec.push_back(wait_debug_str.c_str());
    }

    envp_vec.push_back(nullptr);

    frida_spawn_options_set_envp(options, const_cast<gchar**>(envp_vec.data()), envp_vec.size() - 1);
    frida_spawn_options_set_stdio(options, FRIDA_STDIO_INHERIT);

    // Spawn suspended
    guint pid = frida_device_spawn_sync(device_, path, options, nullptr, &error);
    g_object_unref(options);

    // Free duplicated strings (first 3 entries)
    for (int i = 0; i < 3; i++) {
        g_free(const_cast<char*>(envp_vec[i]));
    }
    
    if (error) {
        g_printerr("Failed to spawn: %s\n", error->message);
        g_error_free(error);
        state_ = PROCESS_STATE_FAILED;
        control_block_->process_state = PROCESS_STATE_FAILED;
        return -1;
    }
    
    pid_ = pid;
    *out_pid = pid;
    spawn_method_ = SpawnMethod::Frida;
    state_ = PROCESS_STATE_SUSPENDED;
    control_block_->process_state = PROCESS_STATE_SUSPENDED;
    
    // Check if we should wait for debugger (Controller side)
    wait_for_debugger_if_needed();

    return 0;
}

int FridaController::attach(uint32_t pid) {
    state_ = PROCESS_STATE_ATTACHING;
    control_block_->process_state = PROCESS_STATE_ATTACHING;

    const int max_attempts = 5;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        GError* error = nullptr;
        FridaSessionOptions* options = frida_session_options_new();

        session_ = attach_sync_fn(device_, pid, options, nullptr, &error);
        g_object_unref(options);

        if (!error) {
            break;
        }

        bool retry = (error->domain == FRIDA_ERROR &&
                      (error->code == FRIDA_ERROR_TIMED_OUT ||
                       error->code == FRIDA_ERROR_PROCESS_NOT_FOUND ||
                       error->code == FRIDA_ERROR_PROCESS_NOT_RESPONDING));
        g_printerr("Failed to attach (attempt %d/%d): %s\n", attempt, max_attempts, error->message);
        g_error_free(error);
        session_ = nullptr;

        if (retry && attempt < max_attempts) {
            const int sleep_ms =
                (error->code == FRIDA_ERROR_PROCESS_NOT_FOUND ||
                 error->code == FRIDA_ERROR_PROCESS_NOT_RESPONDING)
                    ? 500
                    : 200;
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            continue;
        }

        state_ = PROCESS_STATE_FAILED;
        control_block_->process_state = PROCESS_STATE_FAILED;
        return -1;
    }
    
    if (!session_) {
        state_ = PROCESS_STATE_FAILED;
        control_block_->process_state = PROCESS_STATE_FAILED;
        return -1;
    }
    
    // Connect detached signal
    g_signal_connect(session_, "detached",
                     G_CALLBACK(on_detached_callback), this);
    
    pid_ = pid;
    state_ = PROCESS_STATE_ATTACHED;
    control_block_->process_state = PROCESS_STATE_ATTACHED;
    
    g_debug("[Controller] Attached to PID %u, detached signal connected\n", pid);
    
    return 0;
}

int FridaController::detach() {
    if (!session_) {
        return -1;
    }

    // Stop ATF session before detaching (finalizes files)
    stop_atf_session();

    state_ = PROCESS_STATE_DETACHING;
    control_block_->process_state = PROCESS_STATE_DETACHING;
    
    GError* error = nullptr;
    frida_session_detach_sync(session_, nullptr, &error);
    
    if (error) {
        g_error_free(error);
        return -1;
    }
    
    state_ = PROCESS_STATE_INITIALIZED;
    control_block_->process_state = PROCESS_STATE_INITIALIZED;
    
    return 0;
}

int FridaController::resume() {
    if (state_ != PROCESS_STATE_SUSPENDED && 
        state_ != PROCESS_STATE_ATTACHED) {
        return -1;
    }
    
    if (spawn_method_ != SpawnMethod::Frida || !device_ || pid_ == 0) {
        state_ = PROCESS_STATE_FAILED;
        control_block_->process_state = PROCESS_STATE_FAILED;
        return -1;
    }

    // If we have a script (hooks were installed), verify they're ready
    if (spawn_method_ == SpawnMethod::Frida && control_block_ && script_) {
        if (cb_get_hooks_ready(control_block_) == 0) {
            g_printerr("[Controller] Error: hooks_ready not set before resume\n");
            state_ = PROCESS_STATE_FAILED;
            control_block_->process_state = PROCESS_STATE_FAILED;
            return -1;
        }
        g_debug("[Controller] Hooks ready confirmed; proceeding to resume\n");
    }

    GError* error = nullptr;
    frida_device_resume_sync(device_, pid_, nullptr, &error);
    
    if (error) {
        g_error_free(error);
        state_ = PROCESS_STATE_FAILED;
        control_block_->process_state = PROCESS_STATE_FAILED;
        return -1;
    }
    
    state_ = PROCESS_STATE_RUNNING;
    control_block_->process_state = PROCESS_STATE_RUNNING;

    // Start ATF session when process begins running
    if (!start_atf_session()) {
        g_printerr("[Controller] Warning: Failed to start ATF session (tracing continues without file output)\n");
        // Continue anyway - tracing still works via ring buffers
    }

    return 0;
}

int FridaController::pause() {
    // TODO: Implement pause functionality if needed
    return -1;
}

// ============================================================================
// Agent injection
// ============================================================================

static const char * frida_error_code_to_string(guint code) {
    switch (code) {
        case FRIDA_ERROR_SERVER_NOT_RUNNING:
            return "FRIDA_ERROR_SERVER_NOT_RUNNING";
        case FRIDA_ERROR_EXECUTABLE_NOT_FOUND:
            return "FRIDA_ERROR_EXECUTABLE_NOT_FOUND";
        case FRIDA_ERROR_EXECUTABLE_NOT_SUPPORTED:
            return "FRIDA_ERROR_EXECUTABLE_NOT_SUPPORTED";
        case FRIDA_ERROR_PROCESS_NOT_FOUND:
            return "FRIDA_ERROR_PROCESS_NOT_FOUND";
        case FRIDA_ERROR_PROCESS_NOT_RESPONDING:
            return "FRIDA_ERROR_PROCESS_NOT_RESPONDING";
        case FRIDA_ERROR_INVALID_ARGUMENT:
            return "FRIDA_ERROR_INVALID_ARGUMENT";
        case FRIDA_ERROR_INVALID_OPERATION:
            return "FRIDA_ERROR_INVALID_OPERATION";
        case FRIDA_ERROR_PERMISSION_DENIED:
            return "FRIDA_ERROR_PERMISSION_DENIED";
        case FRIDA_ERROR_ADDRESS_IN_USE:
            return "FRIDA_ERROR_ADDRESS_IN_USE";
        case FRIDA_ERROR_TIMED_OUT:
            return "FRIDA_ERROR_TIMED_OUT";
        case FRIDA_ERROR_NOT_SUPPORTED:
            return "FRIDA_ERROR_NOT_SUPPORTED";
        case FRIDA_ERROR_PROTOCOL:
            return "FRIDA_ERROR_PROTOCOL";
        case FRIDA_ERROR_TRANSPORT:
            return "FRIDA_ERROR_TRANSPORT";
        default:
            return "Unknown frida error"; 
    }
}

int FridaController::install_hooks() {
    if (!session_) {
        return -1;
    }

    // Reset readiness and symbol estimate for this startup sequence
    if (control_block_) {
        cb_set_hooks_ready(control_block_, 0);
    }
    symbol_estimate_.store(0u, std::memory_order_relaxed);
    has_symbol_estimate_.store(false, std::memory_order_relaxed);

    // Find agent library
    char agent_path[1024];
    memset(agent_path, 0, sizeof(agent_path));
    
#ifdef __APPLE__
    const char* lib_basename = "libfrida_agent.dylib";
#else
    const char* lib_basename = "libfrida_agent.so";
#endif

    // Check ADA_AGENT_RPATH_SEARCH_PATHS
    const char* rpath = getenv("ADA_AGENT_RPATH_SEARCH_PATHS");
    bool found = false;
    
    if (rpath && *rpath) {
        std::string search_paths(rpath);
        size_t start = 0;
        size_t end = search_paths.find(':');
        
        while (!found) {
            std::string path = (end == std::string::npos) 
                ? search_paths.substr(start)
                : search_paths.substr(start, end - start);
            
            snprintf(agent_path, sizeof(agent_path), "%s/%s", 
                    path.c_str(), lib_basename);
            printf("[Controller] Trying agent path: %s\n", agent_path);
            
            if (access(agent_path, F_OK) == 0) {
                found = true;
                break;
            }
            
            if (end == std::string::npos) break;
            start = end + 1;
            end = search_paths.find(':', start);
        }
    }

    if (!found) {
        fprintf(stderr, "[Controller] Agent library not found\n");
        return -1;
    }

    printf("[Controller] Using agent library: %s\n", agent_path);

    // Prepare initialization payload (optionally include exclude CSV)
    const char* exclude_csv = getenv("ADA_EXCLUDE");
    char init_payload[512];
    if (exclude_csv && *exclude_csv) {
        // Trim payload if too long
        char exclude_buf[256];
        size_t n = strlen(exclude_csv);
        if (n >= sizeof(exclude_buf)) n = sizeof(exclude_buf) - 1;
        memcpy(exclude_buf, exclude_csv, n);
        exclude_buf[n] = '\0';
        snprintf(init_payload, sizeof(init_payload),
                 "host_pid=%u;session_id=%08x;exclude=%s",
                 shared_memory_get_pid(), shared_memory_get_session_id(), exclude_buf);
    } else {
        snprintf(init_payload, sizeof(init_payload),
                 "host_pid=%u;session_id=%08x",
                 shared_memory_get_pid(), shared_memory_get_session_id());
    }

    // --------------------------------------------------------------------
    // Phase 1: Estimate symbol count via lightweight QuickJS script
    // --------------------------------------------------------------------
    {
        char estimate_source[2048];
        snprintf(estimate_source, sizeof(estimate_source),
#if DEBUG
            "console.log('[Loader] Estimating hooks for agent');\n"
#endif
            "const agent_path = '%s';\n"
            "try {\n"
            "  const mod = Module.load(agent_path);\n"
#if DEBUG
            "  console.log('[Loader] Agent loaded for estimation at base:', mod.base);\n"
#endif
            "  const est = mod.getExportByName('agent_estimate_hooks');\n"
            "  let count = 0;\n"
            "  if (est) {\n"
#if DEBUG
            "    console.log('[Loader] Using agent_estimate_hooks export');\n"
#endif
            "    const fn = new NativeFunction(est, 'uint32', []);\n"
            "    count = fn();\n"
#if DEBUG
            "    console.log('[Loader] agent_estimate_hooks reported ' + count);\n"
#endif
            "    send('ESTIMATE:' + count + ':agent');\n"
            "  } else {\n"
#if DEBUG
            "    console.log('[Loader] agent_estimate_hooks missing, falling back to JS enumeration');\n"
#endif
            "    const mods = Process.enumerateModules();\n"
            "    for (let i = 0; i < mods.length; i++) {\n"
            "      try { count += Module.enumerateExports(mods[i].name).length; } catch (e2) {}\n"
            "    }\n"
#if DEBUG
            "    console.log('[Loader] Fallback JS estimate count ' + count);\n"
#endif
            "    send('ESTIMATE:' + count + ':fallback');\n"
            "  }\n"
            "} catch (e) {\n"
#if DEBUG
            "  console.error('[Loader] Estimation failed:', e.toString());\n"
#endif
            "  send('ESTIMATE:0:error');\n"
            "}\n",
            agent_path);

        GError* error = nullptr;
        FridaScriptOptions* est_opts = frida_script_options_new();
        frida_script_options_set_name(est_opts, "agent-estimator");
        frida_script_options_set_runtime(est_opts, FRIDA_SCRIPT_RUNTIME_QJS);

        FridaScript* estimator = frida_session_create_script_sync(
            session_, estimate_source, est_opts, nullptr, &error);
        g_object_unref(est_opts);

        if (error) {
            g_printerr("Failed to create estimator script: %s\n", error->message);
            g_error_free(error);
            return -1;
        }

        // Reuse the controller's message handler to capture ESTIMATE messages
        g_signal_connect(estimator, "message",
                         G_CALLBACK(on_message_callback), this);

        frida_script_load_sync(estimator, nullptr, &error);
        if (error) {
            g_printerr("Failed to load estimator script: %s\n", error->message);
            g_error_free(error);
            frida_script_unload_sync(estimator, nullptr, nullptr);
            frida_unref(estimator);
            return -1;
        }

        frida_script_unload_sync(estimator, nullptr, nullptr);
        frida_unref(estimator);
    }

    uint32_t symbol_count = symbol_estimate_.load(std::memory_order_relaxed);
    last_startup_timeout_ms_ = startup_cfg_.compute_timeout_ms(symbol_count);
    printf("[Controller] Startup timeout: symbols=%u, timeout_ms=%u, "
           "startup_ms=%u, per_symbol_ms=%u, tolerance=%.3f, override_ms=%u\n",
           symbol_count,
           last_startup_timeout_ms_,
           startup_cfg_.startup_ms,
           startup_cfg_.per_symbol_ms,
           startup_cfg_.tolerance_pct,
           startup_cfg_.override_ms);

    // --------------------------------------------------------------------
    // Phase 2: Create QuickJS loader script and load asynchronously
    // --------------------------------------------------------------------
    char script_source[4096];
    snprintf(script_source, sizeof(script_source),
#if DEBUG
        "console.log('[Loader] Starting native agent injection');\n"
        "console.log('[Loader] Agent path: %s');\n"
        "console.log('[Loader] Init payload: %s');\n"
#endif
        "\n"
        "try {\n"
        "  const agent_path = '%s';\n"
        "  const init_payload = '%s';\n"
        "  \n"
        "  // Load the native agent module\n"
        "  const mod = Module.load(agent_path);\n"
#if DEBUG
        "  console.log('[Loader] Agent loaded at base:', mod.base);\n"
#endif
        "  \n"
        "  // Get the agent_init function\n"
        "  const agent_init = mod.getExportByName('agent_init');\n"
        "  if (agent_init) {\n"
#if DEBUG
        "    console.log('[Loader] Found agent_init at:', agent_init);\n"
#endif
        "    \n"
        "    // Create native function wrapper\n"
        "    const initFunc = new NativeFunction(agent_init, 'void', ['pointer', 'int']);\n"
        "    \n"
        "    // Allocate and write the payload\n"
        "    const payloadBuf = Memory.allocUtf8String(init_payload);\n"
        "    \n"
        "    // Call agent_init\n"
        "    try {\n"
        "      initFunc(payloadBuf, init_payload.length);\n"
#if DEBUG
        "      console.log('[Loader] Agent initialized successfully');\n"
#endif
        "    } catch (e2) {\n"
#if DEBUG
        "      console.error('[Loader] Error calling agent_init:', e2.toString());\n"
#endif
        "    }\n"
        "  } else {\n"
#if DEBUG
        "    console.error('[Loader] agent_init not found in agent');\n"
#endif
        "  }\n"
        "  \n"
        "  // Export a ping function for health checks\n"
        "  rpc.exports = {\n"
        "    ping: function() { return 'ok'; }\n"
        "  };\n"
        "} catch (e) {\n"
#if DEBUG
        "  console.error('[Loader] Error:', e.toString());\n"
#endif
        "  throw e;\n"
        "}\n",
        agent_path, init_payload, agent_path, init_payload);

    GError* error = nullptr;
    FridaScriptOptions* options = frida_script_options_new();
    frida_script_options_set_name(options, "agent-loader");
    frida_script_options_set_runtime(options, FRIDA_SCRIPT_RUNTIME_QJS);

    script_ = frida_session_create_script_sync(session_, script_source,
                                               options, nullptr, &error);
    g_object_unref(options);

    if (error) {
        g_printerr("Failed to create script: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    // Connect message handler
    g_signal_connect(script_, "message",
                     G_CALLBACK(on_message_callback), this);

    // Load script asynchronously with deadline enforced by GCancellable + GMainLoop
    ScriptLoadContext ctx{};
    ctx.cancellable = g_cancellable_new();
    ctx.loop = g_main_loop_new(main_context_, FALSE);
    ctx.error = nullptr;
    ctx.completed = false;
    ctx.timed_out = false;

    script_cancellable_ = ctx.cancellable;

    auto load_start_time = std::chrono::steady_clock::now();

    frida_script_load(script_, ctx.cancellable,
                      on_script_load_finished, &ctx);

    guint timeout_ms = last_startup_timeout_ms_ > 0 ? last_startup_timeout_ms_ : 60000u;
    g_timeout_add(timeout_ms, on_script_load_timeout_cb, &ctx);

    g_main_loop_run(ctx.loop);

    auto load_end_time = std::chrono::steady_clock::now();
    auto load_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(load_end_time - load_start_time).count();

    script_cancellable_ = nullptr;

    if (ctx.cancellable) {
        g_object_unref(ctx.cancellable);
    }
    if (ctx.loop) {
        g_main_loop_unref(ctx.loop);
    }

    if (ctx.error) {
        bool timeout_class = false;
        if (ctx.error->domain == FRIDA_ERROR &&
            ctx.error->code == FRIDA_ERROR_TIMED_OUT) {
            timeout_class = true;
        } else if (ctx.error->domain == G_IO_ERROR &&
                   ctx.error->code == G_IO_ERROR_CANCELLED) {
            timeout_class = true;
        }

        if (timeout_class) {
            g_printerr("[Controller] Agent loader timed out after %u ms (estimated symbols=%u)\n",
                       timeout_ms,
                       symbol_count);
        } else {
            g_printerr("Failed to load script: %s; domain: %s; code: %s\n", ctx.error->message, g_quark_to_string(ctx.error->domain), frida_error_code_to_string(ctx.error->code));
        }
        g_printerr("Is timedout: %s\n", ctx.timed_out ? "yes" : "no");
        g_printerr("Is completed: %s\n", ctx.completed ? "yes" : "no");
        g_printerr("Load duration: %lld ms\n", load_duration_ms);
        g_printerr("Timeout duration: %u ms\n", timeout_ms);

        g_error_free(ctx.error);

        if (script_) {
            frida_script_unload_sync(script_, nullptr, nullptr);
            frida_unref(script_);
            script_ = nullptr;
        }

        state_ = PROCESS_STATE_FAILED;
        if (control_block_) {
            control_block_->process_state = PROCESS_STATE_FAILED;
        }
        return -1;
    }

    printf("[Controller] Agent loader script loaded successfully\n");

    // Wait for the agent to signal readiness (hooks_ready flag)
    // The script loads the agent and calls agent_init, which eventually sets hooks_ready
    if (control_block_) {
        const uint32_t poll_ms = 10;
        // Use the computed startup timeout as readiness deadline
        uint32_t max_wait_ms = last_startup_timeout_ms_ > 0 ? last_startup_timeout_ms_ : 30000u;
        uint32_t waited_ms = 0;

        printf("[Controller] Waiting for agent to signal hooks_ready...\n");
        while (cb_get_hooks_ready(control_block_) == 0) {
            if (waited_ms >= max_wait_ms) {
                g_printerr("[Controller] Timeout waiting for agent to set hooks_ready after %u ms\n",
                           max_wait_ms);
                state_ = PROCESS_STATE_FAILED;
                if (control_block_) {
                    control_block_->process_state = PROCESS_STATE_FAILED;
                }
                return -1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
            waited_ms += poll_ms;
        }

        printf("[Controller] Agent reported hooks_ready after %u ms\n", waited_ms);
    }

    return 0;
}

int FridaController::inject_agent(const char* agent_path) {
    // Delegate to install_hooks flow after ensuring the given path is used
    // Set ADA_AGENT_RPATH_SEARCH_PATHS to include the provided directory
    if (!agent_path) return -1;
    std::string dir;
    if (const char* slash = strrchr(agent_path, '/')) {
        dir.assign(agent_path, slash - agent_path);
    }
    if (!dir.empty()) {
        setenv("ADA_AGENT_RPATH_SEARCH_PATHS", dir.c_str(), 1);
    }

    // If we have a spawned process but no session, attach first
    if (!session_ && pid_ > 0 && state_ == PROCESS_STATE_SUSPENDED) {
        int attach_result = attach(pid_);
        if (attach_result != 0) {
            return attach_result;
        }
    }

    return install_hooks();
}

// ============================================================================
// Flight recorder control
// ============================================================================

int FridaController::arm_trigger(uint32_t pre_roll_ms, uint32_t post_roll_ms) {
    if (!control_block_) {
        return -1;
    }
    
    control_block_->pre_roll_ms = pre_roll_ms;
    control_block_->post_roll_ms = post_roll_ms;
    control_block_->flight_state = FLIGHT_RECORDER_ARMED;
    
    return 0;
}

int FridaController::fire_trigger() {
    if (!control_block_) {
        return -1;
    }
    
    if (control_block_->flight_state != FLIGHT_RECORDER_ARMED) {
        return -1;
    }
    
    control_block_->flight_state = FLIGHT_RECORDER_RECORDING;
    
    return 0;
}

int FridaController::disarm_trigger() {
    if (!control_block_) {
        return -1;
    }
    
    control_block_->flight_state = FLIGHT_RECORDER_IDLE;
    
    return 0;
}

int FridaController::set_detail_enabled(uint32_t enabled) {
    if (!control_block_) {
        return -1;
    }

    control_block_->detail_lane_enabled = enabled ? 1 : 0;

    return 0;
}

int FridaController::start_session() {
    if (!start_atf_session()) {
        return -1;
    }
    return 0;
}

int FridaController::stop_session() {
    stop_atf_session();
    return 0;
}

FlightRecorderState FridaController::get_flight_state() const {
    if (!control_block_) {
        return FLIGHT_RECORDER_IDLE;
    }

    return control_block_->flight_state;
}

TracerStats FridaController::get_stats() const {
    TracerStats result = {};

    if (drain_) {
        DrainMetrics dm;
        drain_thread_get_metrics(drain_, &dm);

        result.events_captured = dm.total_events_drained;
        result.bytes_written = dm.total_bytes_drained;
        // Note: active_threads and hooks_installed would need additional tracking
    }

    return result;
}

// ============================================================================
// Callbacks
// ============================================================================

void FridaController::on_detached_callback(FridaSession* session,
                                           FridaSessionDetachReason reason,
                                           FridaCrash* crash,
                                           gpointer user_data) {
    auto* controller = static_cast<FridaController*>(user_data);
    controller->on_detached(reason, crash);
}

void FridaController::on_message_callback(FridaScript* script,
                                          const gchar* message,
                                          GBytes* data,
                                          gpointer user_data) {
    auto* controller = static_cast<FridaController*>(user_data);
    controller->on_message(message, data);
}

void FridaController::on_detached(FridaSessionDetachReason reason, FridaCrash* crash) {
    (void)crash;
    g_debug("Frida session detached (reason=%d)\n", static_cast<int>(reason));

    // If a script load is in progress, cancel it to unblock the startup loop
    if (script_cancellable_) {
        g_cancellable_cancel(script_cancellable_);
    }

    state_ = PROCESS_STATE_INITIALIZED;
    if (control_block_) {
        control_block_->process_state = PROCESS_STATE_INITIALIZED;
    }
}

// ============================================================================
// Internal Helpers
// ============================================================================

static std::string unescape_json_string(const std::string& s) {
    std::string res;
    res.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] == '\\' && i + 1 < s.length()) {
            switch (s[i+1]) {
                case '"': res += '"'; break;
                case '\\': res += '\\'; break;
                case '/': res += '/'; break;
                case 'b': res += '\b'; break;
                case 'f': res += '\f'; break;
                case 'n': res += '\n'; break;
                case 'r': res += '\r'; break;
                case 't': res += '\t'; break;
                default: res += s[i]; res += s[i+1]; break; // Unknown escape, keep raw
            }
            i++;
        } else {
            res += s[i];
        }
    }
    return res;
}

static std::string json_get_string(const std::string& json, const std::string& key) {
    std::string key_pattern = "\"" + key + "\":";
    size_t pos = json.find(key_pattern);
    if (pos == std::string::npos) return "";
    
    pos += key_pattern.length();
    // Skip whitespace
    while (pos < json.length() && isspace(json[pos])) pos++;
    
    if (pos >= json.length()) return "";
    
    if (json[pos] == '"') {
        // String value
        pos++;
        size_t end = pos;
        while (end < json.length()) {
            if (json[end] == '"' && json[end-1] != '\\') break;
            end++;
        }
        return unescape_json_string(json.substr(pos, end - pos));
    }
    
    // Non-string value (boolean, number, null) - simplistic extraction
    size_t end = pos;
    while (end < json.length() && (isalnum(json[end]) || json[end] == '.')) end++;
    return json.substr(pos, end - pos);
}

void FridaController::on_message(const gchar* message, GBytes* data) {
    (void)data;
    if (!message) {
        return;
    }

    // Handle console.log/error from script
    std::string msg(message);
    std::string type = json_get_string(msg, "type");
    
    if (type == "log") {
        std::string level = json_get_string(msg, "level");
        std::string payload = json_get_string(msg, "payload");
        
        FILE* target = (level == "error") ? stderr : stdout;
        fprintf(target, "[Script] %s\n", payload.c_str());
        fflush(target);
    } else if (type == "error") {
        std::string desc = json_get_string(msg, "description");
        std::string stack = json_get_string(msg, "stack");
        fprintf(stderr, "[Script Error] %s\n%s\n", desc.c_str(), stack.c_str());
        fflush(stderr);
    }

    g_debug("Script message: %s\n", message);

    // Lightweight parser for estimation messages from loader/estimator scripts.
    // We expect the JSON payload to contain a substring like:
    //   "ESTIMATE:<count>:<source>"
    const char* tag = "ESTIMATE:";
    const char* p = strstr(message, tag);
    if (p) {
        p += strlen(tag);
        uint64_t value = 0;
        while (*p >= '0' && *p <= '9') {
            value = value * 10u + static_cast<uint64_t>(*p - '0');
            ++p;
        }
        if (value <= UINT32_MAX) {
            uint32_t count = static_cast<uint32_t>(value);
            symbol_estimate_.store(count, std::memory_order_relaxed);
            has_symbol_estimate_.store(true, std::memory_order_relaxed);
            g_debug("[Controller] Parsed symbol estimate from loader: %u\n", count);
        }
    }
}

} // namespace internal
} // namespace ada

// ============================================================================
// C API Implementation
// ============================================================================

extern "C" {

using ada::internal::FridaController;

FridaController* frida_controller_create(const char* output_dir) {
    try {
        return reinterpret_cast<FridaController*>(
            new ada::internal::FridaController(output_dir));
    } catch (const std::exception& e) {
        g_debug("Failed to create FridaController: %s\n", e.what());
        return nullptr;
    }
}

void frida_controller_destroy(FridaController* controller) {
    delete reinterpret_cast<ada::internal::FridaController*>(controller);
}

int frida_controller_spawn_suspended(FridaController* controller,
                                     const char* path,
                                     char* const argv[],
                                     uint32_t* out_pid) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->spawn_suspended(path, argv, out_pid);
}

int frida_controller_attach(FridaController* controller, uint32_t pid) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->attach(pid);
}

int frida_controller_detach(FridaController* controller) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->detach();
}

int frida_controller_resume(FridaController* controller) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->resume();
}

int frida_controller_pause(FridaController* controller) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->pause();
}

int frida_controller_install_hooks(FridaController* controller) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->install_hooks();
}

int frida_controller_inject_agent(FridaController* controller, const char* agent_path) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->inject_agent(agent_path);
}

int frida_controller_arm_trigger(FridaController* controller,
                                 uint32_t pre_roll_ms,
                                 uint32_t post_roll_ms) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->arm_trigger(pre_roll_ms, post_roll_ms);
}

int frida_controller_fire_trigger(FridaController* controller) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->fire_trigger();
}

int frida_controller_disarm_trigger(FridaController* controller) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->disarm_trigger();
}

int frida_controller_set_detail_enabled(FridaController* controller, uint32_t enabled) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->set_detail_enabled(enabled);
}

int frida_controller_start_session(FridaController* controller) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->start_session();
}

int frida_controller_stop_session(FridaController* controller) {
    if (!controller) return -1;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->stop_session();
}

ProcessState frida_controller_get_state(FridaController* controller) {
    if (!controller) return PROCESS_STATE_UNINITIALIZED;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->get_state();
}

FlightRecorderState frida_controller_get_flight_state(FridaController* controller) {
    if (!controller) return FLIGHT_RECORDER_IDLE;
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->get_flight_state();
}

TracerStats frida_controller_get_stats(FridaController* controller) {
    if (!controller) {
        TracerStats empty = {0};
        return empty;
    }
    return reinterpret_cast<ada::internal::FridaController*>(controller)
        ->get_stats();
}

} // extern "C"
