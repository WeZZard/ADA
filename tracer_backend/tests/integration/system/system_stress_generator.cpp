#include "system_stress_generator.h"

#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <pthread.h>
#include <system_error>
#include <thread>

extern "C" {
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/utils/tracer_types.h>
}

namespace {

uint64_t monotonic_now_ns() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

void record_event(perf_monitor_t* monitor, uint64_t latency_ns, uint64_t events) {
    if (!monitor) {
        return;
    }
    perf_monitor_record(monitor, events, latency_ns, events * sizeof(IndexEvent));
}

struct WorkerContext {
    stress_generator_t* generator{nullptr};
    uint32_t worker_index{0};
};

void worker_entry(WorkerContext ctx) {
    stress_generator_t* generator = ctx.generator;
    if (!generator || !generator->fixture) {
        return;
    }

    ThreadRegistry* registry = test_fixture_registry(generator->fixture);
    if (!registry) {
        return;
    }

    uintptr_t tid = reinterpret_cast<uintptr_t>(pthread_self()) ^ (static_cast<uintptr_t>(ctx.worker_index) << 8);
    ThreadLaneSet* lanes = thread_registry_register(registry, tid);
    if (!lanes) {
        return;
    }

    Lane* index_lane = thread_lanes_get_index_lane(lanes);
    if (!index_lane) {
        thread_registry_unregister(lanes);
        return;
    }

    AtfV4Writer* writer = test_fixture_writer(generator->fixture);
    if (!writer) {
        thread_registry_unregister(lanes);
        return;
    }

    std::mt19937_64 rng(monotonic_now_ns() ^ (static_cast<uint64_t>(ctx.worker_index) << 32));
    std::uniform_int_distribution<int> syscall_dist(0x100, 0x1FF);
    std::uniform_int_distribution<int> chaos_dist(0, 9);

    const char* register_names[] = {"X0", "X1", "LR"};

    while (generator->running.load(std::memory_order_relaxed)) {
        uint32_t ring_idx = lane_get_free_ring(index_lane);
        if (ring_idx == UINT32_MAX) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        RingBufferHeader* header = thread_registry_get_ring_header_by_idx(registry, index_lane, ring_idx);
        if (!header) {
            lane_return_ring(index_lane, ring_idx);
            continue;
        }

        header->read_pos = 0;
        header->write_pos = 0;

        const uint32_t burst_length = generator->config.burst_length;
        const uint32_t syscalls_per_burst = generator->config.syscalls_per_burst;
        const uint32_t events_per_burst = burst_length * syscalls_per_burst;

        for (uint32_t burst = 0; burst < burst_length; ++burst) {
            uint64_t syscall_id = static_cast<uint64_t>(syscall_dist(rng));
            for (uint32_t s = 0; s < syscalls_per_burst; ++s) {
                IndexEvent evt{};
                evt.timestamp = monotonic_now_ns();
                evt.function_id = (syscall_id << 16) | ((s + 1) & 0xFFFF);
                evt.thread_id = static_cast<uint32_t>(ctx.worker_index + 1);
                evt.event_kind = (s % 2 == 0) ? EVENT_KIND_CALL : EVENT_KIND_RETURN;
                evt.call_depth = s % 32;

                if (!ring_buffer_write_raw(header, sizeof(IndexEvent), &evt)) {
                    // Ring is unexpectedly full; drop this event and continue
                    continue;
                }

                std::array<uint8_t, 16> stack_bytes{};
                stack_bytes.fill(static_cast<uint8_t>(evt.call_depth));

                AtfV4Event atf_event{};
                atf_event.kind = (evt.event_kind == EVENT_KIND_CALL) ? ATF_V4_EVENT_FUNCTION_CALL
                                                                     : ATF_V4_EVENT_FUNCTION_RETURN;
                atf_event.thread_id = static_cast<int32_t>(evt.thread_id);
                atf_event.timestamp_ns = evt.timestamp;

                std::string symbol = "syscall_" + std::to_string(syscall_id);
                if (atf_event.kind == ATF_V4_EVENT_FUNCTION_CALL) {
                    atf_event.payload.function_call.symbol = symbol.c_str();
                    atf_event.payload.function_call.address = 0x10000000ull + syscall_id;
                    atf_event.payload.function_call.register_count = 3;
                    for (size_t i = 0; i < 3; ++i) {
                        auto& reg = atf_event.payload.function_call.registers[i];
                        std::strncpy(reg.name, register_names[i], sizeof(reg.name) - 1);
                        reg.name[sizeof(reg.name) - 1] = '\0';
                        reg.value = evt.function_id + i;
                    }
                    atf_event.payload.function_call.stack_bytes = stack_bytes.data();
                    atf_event.payload.function_call.stack_size = stack_bytes.size();
                } else {
                    atf_event.payload.function_return.symbol = symbol.c_str();
                    atf_event.payload.function_return.address = 0x10000000ull + syscall_id;
                    atf_event.payload.function_return.register_count = 2;
                    for (size_t i = 0; i < 2; ++i) {
                        auto& reg = atf_event.payload.function_return.registers[i];
                        std::strncpy(reg.name, register_names[i], sizeof(reg.name) - 1);
                        reg.name[sizeof(reg.name) - 1] = '\0';
                        reg.value = evt.function_id + 100 + i;
                    }
                }

                auto start = std::chrono::steady_clock::now();
                int rc = atf_v4_writer_write_event(writer, &atf_event);
                auto end = std::chrono::steady_clock::now();
                if (rc == 0) {
                    auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                    record_event(generator->monitor, static_cast<uint64_t>(latency_ns), 1);
                    generator->total_events.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        generator->bursts_completed.fetch_add(1, std::memory_order_relaxed);

        if (generator->config.chaos_mode && chaos_dist(rng) == 0) {
            generator->chaos_operations.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(200 + chaos_dist(rng) * 50));
        } else {
            std::this_thread::yield();
        }

        // Publish the ring and immediately reclaim to simulate drain completion
        if (!lane_submit_ring(index_lane, ring_idx)) {
            // If submission fails, return ring directly to avoid leaks
            lane_return_ring(index_lane, ring_idx);
        } else {
            lane_return_ring(index_lane, ring_idx);
        }
    }

    thread_registry_unregister(lanes);
}

}  // namespace

bool stress_generator_start(stress_generator_t* generator, test_fixture_t* fixture,
                            const stress_generator_config_t& config, perf_monitor_t* monitor,
                            std::string* error_message) {
    if (!generator) {
        if (error_message) {
            *error_message = "generator pointer was null";
        }
        return false;
    }
    if (!fixture) {
        if (error_message) {
            *error_message = "fixture pointer was null";
        }
        return false;
    }
    if (!test_fixture_registry(fixture)) {
        if (error_message) {
            *error_message = "fixture registry was not initialized";
        }
        return false;
    }
    if (!test_fixture_writer(fixture)) {
        if (error_message) {
            *error_message = "fixture writer not available";
        }
        return false;
    }

    generator->fixture = fixture;
    generator->config = config;
    generator->monitor = monitor;
    generator->running.store(true, std::memory_order_relaxed);
    generator->total_events.store(0, std::memory_order_relaxed);
    generator->bursts_completed.store(0, std::memory_order_relaxed);
    generator->chaos_operations.store(0, std::memory_order_relaxed);
    generator->workers.clear();
    generator->workers.reserve(config.worker_threads);

    for (uint32_t idx = 0; idx < config.worker_threads; ++idx) {
        WorkerContext ctx{generator, idx};
        try {
            generator->workers.emplace_back(worker_entry, ctx);
        } catch (const std::system_error& err) {
            if (error_message) {
                *error_message = std::string("failed to start worker thread: ") + err.what();
            }
            generator->running.store(false, std::memory_order_relaxed);
            stress_generator_stop(generator);
            return false;
        }
    }

    return true;
}

void stress_generator_stop(stress_generator_t* generator) {
    if (!generator) {
        return;
    }
    generator->running.store(false, std::memory_order_relaxed);
    for (auto& worker : generator->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    generator->workers.clear();
}

uint64_t stress_generator_events(const stress_generator_t* generator) {
    return generator ? generator->total_events.load(std::memory_order_relaxed) : 0;
}

uint64_t stress_generator_bursts(const stress_generator_t* generator) {
    return generator ? generator->bursts_completed.load(std::memory_order_relaxed) : 0;
}

uint64_t stress_generator_chaos_ops(const stress_generator_t* generator) {
    return generator ? generator->chaos_operations.load(std::memory_order_relaxed) : 0;
}
