#include "frida_controller_internal.h"
#include "../utils/ring_buffer_private.h"
#include "../utils/thread_registry_private.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <chrono>
#include <thread>

#ifdef __APPLE__
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif

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
// Constructor/Destructor
// ============================================================================

FridaController::FridaController(const std::string& output_dir)
    : output_dir_(output_dir)
{
    // Initialize state
    state_ = PROCESS_STATE_INITIALIZED;
    spawn_method_ = SpawnMethod::None;
    
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
    
    // Start drain thread
    drain_running_ = true;
    drain_thread_ = std::make_unique<std::thread>(&FridaController::drain_thread_main, this);
}

FridaController::~FridaController() {
    // Stop drain thread
    drain_running_ = false;
    if (drain_thread_ && drain_thread_->joinable()) {
        drain_thread_->join();
    }
    
    // Cleanup Frida objects
    cleanup_frida_objects();
    
    // Close output file
    if (output_file_) {
        fclose(output_file_);
        output_file_ = nullptr;
    }
    
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
// Process management
// ============================================================================

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
    
    const char* envp[] = {
        g_strdup_printf("PATH=%s", g_getenv("PATH")),
        g_strdup_printf("HOME=%s", g_get_home_dir()),
        g_strdup_printf("__CF_USER_TEXT_ENCODING=%s", 
                       g_getenv("__CF_USER_TEXT_ENCODING") ?: "0x1F5:0x0:0x0"),
        ada_session.c_str(),
        ada_host.c_str(),
        nullptr
    };
    frida_spawn_options_set_envp(options, const_cast<gchar**>(envp), 5);
    frida_spawn_options_set_stdio(options, FRIDA_STDIO_INHERIT);
    
    // Spawn suspended
    guint pid = frida_device_spawn_sync(device_, path, options, nullptr, &error);
    g_object_unref(options);
    
    // Free duplicated strings
    for (int i = 0; i < 3; i++) {
        g_free(const_cast<char*>(envp[i]));
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
    
    return 0;
}

int FridaController::attach(uint32_t pid) {
    state_ = PROCESS_STATE_ATTACHING;
    control_block_->process_state = PROCESS_STATE_ATTACHING;
    
    GError* error = nullptr;
    FridaSessionOptions* options = frida_session_options_new();
    
    session_ = frida_device_attach_sync(device_, pid, options, nullptr, &error);
    g_object_unref(options);
    
    if (error) {
        g_printerr("Failed to attach: %s\n", error->message);
        g_error_free(error);
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
    
    return 0;
}

int FridaController::detach() {
    if (!session_) {
        return -1;
    }
    
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
    
    return 0;
}

int FridaController::pause() {
    // TODO: Implement pause functionality if needed
    return -1;
}

// ============================================================================
// Agent injection
// ============================================================================

int FridaController::install_hooks() {
    if (!session_) {
        return -1;
    }
    
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
    
    // Prepare initialization payload
    char init_payload[256];
    snprintf(init_payload, sizeof(init_payload),
             "host_pid=%u;session_id=%08x",
             shared_memory_get_pid(), shared_memory_get_session_id());
    
    // Create QuickJS loader script
    char script_source[4096];
    snprintf(script_source, sizeof(script_source),
        "console.log('[Loader] Starting native agent injection');\n"
        "console.log('[Loader] Agent path: %s');\n"
        "console.log('[Loader] Init payload: %s');\n"
        "\n"
        "try {\n"
        "  const agent_path = '%s';\n"
        "  const init_payload = '%s';\n"
        "  \n"
        "  // Load the native agent module\n"
        "  const mod = Module.load(agent_path);\n"
        "  console.log('[Loader] Agent loaded at base:', mod.base);\n"
        "  \n"
        "  // Get the agent_init function\n"
        "  const agent_init = mod.getExportByName('agent_init');\n"
        "  if (agent_init) {\n"
        "    console.log('[Loader] Found agent_init at:', agent_init);\n"
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
        "      console.log('[Loader] Agent initialized successfully');\n"
        "    } catch (e2) {\n"
        "      console.error('[Loader] Error calling agent_init:', e2.toString());\n"
        "    }\n"
        "  } else {\n"
        "    console.error('[Loader] agent_init not found in agent');\n"
        "  }\n"
        "  \n"
        "  // Export a ping function for health checks\n"
        "  rpc.exports = {\n"
        "    ping: function() { return 'ok'; }\n"
        "  };\n"
        "} catch (e) {\n"
        "  console.error('[Loader] Error:', e.toString());\n"
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
    
    // Load script
    frida_script_load_sync(script_, nullptr, &error);
    if (error) {
        g_printerr("Failed to load script: %s\n", error->message);
        g_error_free(error);
        return -1;
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

FlightRecorderState FridaController::get_flight_state() const {
    if (!control_block_) {
        return FLIGHT_RECORDER_IDLE;
    }
    
    return control_block_->flight_state;
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
    state_ = PROCESS_STATE_INITIALIZED;
    control_block_->process_state = PROCESS_STATE_INITIALIZED;
}

void FridaController::on_message(const gchar* message, GBytes* data) {
    g_debug("Script message: %s\n", message);
}

// ============================================================================
// Drain thread
// ============================================================================

void FridaController::drain_thread_main() {
    g_debug("Drain thread started\n");
    
    constexpr size_t INDEX_BATCH_SIZE = 1000;
    constexpr size_t DETAIL_BATCH_SIZE = 100;
    
    auto index_events = std::make_unique<IndexEvent[]>(INDEX_BATCH_SIZE);
    auto detail_events = std::make_unique<DetailEvent[]>(DETAIL_BATCH_SIZE);
    
    g_debug("Drain thread initialized\n");
    
    while (drain_running_) {
        // Drain index lane
        size_t index_count = index_ring_->read_batch(
            index_events.get(), INDEX_BATCH_SIZE);
        
        if (index_count > 0) {
            stats_.events_captured += index_count;
            stats_.bytes_written += index_count * sizeof(IndexEvent);
            
            // Write to file if open
            if (output_file_) {
                fwrite(index_events.get(), sizeof(IndexEvent), 
                      index_count, output_file_);
            }
        }
        
        // Drain detail lane
        size_t detail_count = detail_ring_->read_batch(
            detail_events.get(), DETAIL_BATCH_SIZE);
        
        if (detail_count > 0) {
            stats_.events_captured += detail_count;
            stats_.bytes_written += detail_count * sizeof(DetailEvent);
            
            // Write to file if open
            if (output_file_) {
                fwrite(detail_events.get(), sizeof(DetailEvent),
                      detail_count, output_file_);
            }
        }
        
        // stats_.drain_cycles++; // Field removed from TracerStats
        
        // Sleep for 100ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    g_debug("Drain thread exiting\n");
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
