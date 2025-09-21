#include "system_test_fixture.h"
#include "system_trace_constants.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <random>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

uint64_t monotonic_now_ns() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::string random_token(std::size_t length = 8) {
    static const char alphabet[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, static_cast<int>(sizeof(alphabet) - 2));
    std::string value;
    value.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        value.push_back(alphabet[dist(gen)]);
    }
    return value;
}

std::string describe_errno(int code) {
    std::ostringstream oss;
    oss << std::strerror(code) << " (errno=" << code << ")";
    return oss.str();
}

}  // namespace

bool test_fixture_init(test_fixture_t* fixture, const test_fixture_options_t& options,
                       std::string* error_message) {
    if (!fixture) {
        if (error_message) {
            *error_message = "fixture pointer was null";
        }
        return false;
    }

    fixture->mode = options.mode;
    fixture->options = options;

    if (options.registry_capacity == 0) {
        if (error_message) {
            *error_message = "registry capacity must be greater than zero";
        }
        return false;
    }

    fixture->registry_bytes = thread_registry_calculate_memory_size_with_capacity(options.registry_capacity);
    try {
        fixture->registry_arena = std::unique_ptr<uint8_t[]>(new uint8_t[fixture->registry_bytes]);
    } catch (const std::bad_alloc&) {
        if (error_message) {
            *error_message = "failed to allocate registry arena";
        }
        return false;
    }
    std::memset(fixture->registry_arena.get(), 0, fixture->registry_bytes);

    fixture->registry = thread_registry_init_with_capacity(fixture->registry_arena.get(),
                                                           fixture->registry_bytes,
                                                           options.registry_capacity);
    if (!fixture->registry) {
        if (error_message) {
            *error_message = "thread_registry_init_with_capacity returned null";
        }
        return false;
    }

    if (!thread_registry_attach(fixture->registry)) {
        if (error_message) {
            *error_message = "thread_registry_attach failed";
        }
        thread_registry_deinit(fixture->registry);
        fixture->registry = nullptr;
        return false;
    }

    auto base_dir = std::filesystem::temp_directory_path() /
                    ("ada_system_integration_" + random_token());
    std::error_code ec;
    std::filesystem::create_directories(base_dir, ec);
    if (ec) {
        if (error_message) {
            *error_message = "failed to create temp directory: " + ec.message();
        }
        thread_registry_deinit(fixture->registry);
        fixture->registry = nullptr;
        return false;
    }

    fixture->output_root = base_dir.string();

    AtfV4WriterConfig config{};
    config.output_root = fixture->output_root.c_str();
    config.session_label = options.session_label.empty() ? nullptr : options.session_label.c_str();
    config.pid = static_cast<uint32_t>(getpid());
    config.session_id = static_cast<uint64_t>(monotonic_now_ns());
    config.enable_manifest = options.enable_manifest;

    int rc = atf_v4_writer_init(&fixture->writer, &config);
    if (rc != 0) {
        if (error_message) {
            *error_message = "atf_v4_writer_init failed: " + describe_errno(-rc);
        }
        thread_registry_deinit(fixture->registry);
        fixture->registry = nullptr;
        return false;
    }

    fixture->writer_initialized = true;
    const char* session_dir = atf_v4_writer_session_dir(&fixture->writer);
    const char* events_path = atf_v4_writer_events_path(&fixture->writer);
    if (session_dir) {
        fixture->session_dir = session_dir;
    }
    if (events_path) {
        fixture->events_path = events_path;
    }

    return true;
}

void test_fixture_publish_trace_start(test_fixture_t* fixture,
                                      const std::vector<std::string>& argv) {
    if (!fixture || !fixture->writer_initialized) {
        return;
    }

    std::vector<const char*> argv_ptrs;
    argv_ptrs.reserve(argv.size());
    for (const auto& arg : argv) {
        argv_ptrs.push_back(arg.c_str());
    }

    static const char kDefaultOs[] = "macOS";
    static const char kDefaultArch[] = "arm64";

    AtfV4Event event{};
    event.kind = ATF_V4_EVENT_TRACE_START;
    event.event_id = 0;
    event.thread_id = kTraceLifecycleThreadId;
    event.timestamp_ns = monotonic_now_ns();
    event.payload.trace_start.executable_path = argv.empty() ? "" : argv.front().c_str();
    event.payload.trace_start.argv = argv_ptrs.empty() ? nullptr : argv_ptrs.data();
    event.payload.trace_start.argc = argv_ptrs.size();
    event.payload.trace_start.operating_system = kDefaultOs;
    event.payload.trace_start.cpu_architecture = kDefaultArch;

    (void)atf_v4_writer_write_event(&fixture->writer, &event);
}

void test_fixture_publish_trace_end(test_fixture_t* fixture, int32_t exit_code) {
    if (!fixture || !fixture->writer_initialized) {
        return;
    }

    AtfV4Event event{};
    event.kind = ATF_V4_EVENT_TRACE_END;
    event.event_id = 0;
    event.thread_id = kTraceLifecycleThreadId;
    event.timestamp_ns = monotonic_now_ns();
    event.payload.trace_end.exit_code = exit_code;
    (void)atf_v4_writer_write_event(&fixture->writer, &event);
}

bool test_fixture_launch_target(test_fixture_t* fixture, const std::string& executable,
                                const std::vector<std::string>& args,
                                std::string* error_message) {
    if (!fixture) {
        if (error_message) {
            *error_message = "fixture pointer was null";
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(fixture->process_mutex);
    if (fixture->child_running) {
        if (error_message) {
            *error_message = "target process already running";
        }
        return false;
    }

    std::vector<std::string> storage;
    storage.reserve(args.size() + 1);
    std::filesystem::path exe_path(executable);
    std::string argv0 = exe_path.filename().empty() ? executable : exe_path.filename().string();
    storage.push_back(argv0);
    for (const auto& arg : args) {
        storage.push_back(arg);
    }

    std::vector<char*> argv_ptrs;
    argv_ptrs.reserve(storage.size() + 1);
    argv_ptrs.push_back(const_cast<char*>(storage[0].c_str()));
    for (std::size_t i = 1; i < storage.size(); ++i) {
        argv_ptrs.push_back(const_cast<char*>(storage[i].c_str()));
    }
    argv_ptrs.push_back(nullptr);

    pid_t pid = -1;
    int rc = posix_spawn(&pid, executable.c_str(), nullptr, nullptr, argv_ptrs.data(), environ);
    if (rc != 0) {
        if (error_message) {
            *error_message = "posix_spawn failed: " + describe_errno(rc);
        }
        return false;
    }

    fixture->child_pid = pid;
    fixture->child_running = true;
    fixture->spawn_operations.fetch_add(1, std::memory_order_relaxed);

    std::vector<std::string> trace_argv;
    trace_argv.push_back(executable);
    trace_argv.insert(trace_argv.end(), args.begin(), args.end());
    test_fixture_publish_trace_start(fixture, trace_argv);

    return true;
}

bool test_fixture_attach_to_pid(test_fixture_t* fixture, pid_t pid,
                                std::string* error_message) {
    if (!fixture) {
        if (error_message) {
            *error_message = "fixture pointer was null";
        }
        return false;
    }

    if (pid <= 0) {
        if (error_message) {
            *error_message = "invalid pid";
        }
        return false;
    }

    if (kill(pid, 0) != 0) {
        if (error_message) {
            *error_message = "unable to signal target pid: " + describe_errno(errno);
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(fixture->process_mutex);
    fixture->child_pid = pid;
    fixture->child_running = true;
    fixture->attach_operations.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void test_fixture_shutdown(test_fixture_t* fixture) {
    if (!fixture) {
        return;
    }

    pid_t pid = -1;
    {
        std::lock_guard<std::mutex> lock(fixture->process_mutex);
        pid = fixture->child_pid;
        fixture->child_running = false;
        fixture->child_pid = -1;
    }

    int exit_code = -1;
    if (pid > 0) {
        kill(pid, SIGTERM);
        int status = 0;
        if (waitpid(pid, &status, 0) >= 0) {
            if (WIFEXITED(status)) {
                exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_code = 128 + WTERMSIG(status);
            }
        }
    }

    test_fixture_publish_trace_end(fixture, exit_code);
    fixture->shutdown_operations.fetch_add(1, std::memory_order_relaxed);

    if (fixture->writer_initialized) {
        (void)atf_v4_writer_finalize(&fixture->writer);
        atf_v4_writer_deinit(&fixture->writer);
        fixture->writer_initialized = false;
    }

    if (fixture->registry) {
        thread_registry_deinit(fixture->registry);
        fixture->registry = nullptr;
    }
}

ThreadRegistry* test_fixture_registry(test_fixture_t* fixture) {
    return fixture ? fixture->registry : nullptr;
}

AtfV4Writer* test_fixture_writer(test_fixture_t* fixture) {
    if (!fixture || !fixture->writer_initialized) {
        return nullptr;
    }
    return &fixture->writer;
}

std::string test_fixture_events_path(const test_fixture_t* fixture) {
    if (!fixture) {
        return {};
    }
    if (!fixture->events_path.empty()) {
        return fixture->events_path;
    }
    return {};
}

size_t test_fixture_registry_bytes(const test_fixture_t* fixture) {
    return fixture ? fixture->registry_bytes : 0;
}

pid_t test_fixture_pid(const test_fixture_t* fixture) {
    return fixture ? fixture->child_pid : -1;
}
