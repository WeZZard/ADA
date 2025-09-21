#include <gtest/gtest.h>

#include <chrono>

extern "C" {
#include <tracer_backend/backpressure/backpressure.h>
}

namespace {

using clock_mono = std::chrono::steady_clock;

double duration_per_iteration_ns(clock_mono::time_point start,
                                 clock_mono::time_point end,
                                 size_t iterations) {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    if (iterations == 0) return 0.0;
    return static_cast<double>(ns) / static_cast<double>(iterations);
}

} // namespace

TEST(BackpressureBench, normal_state_check__average_overhead_within_budget) {
    ada_backpressure_config_t cfg = {};
    cfg.pressure_threshold_percent = 25u;
    cfg.recovery_threshold_percent = 50u;
    cfg.recovery_stable_ns = 1'000'000'000ull;
    cfg.drop_log_interval = UINT32_MAX; // Disable logging during benchmark

    ada_backpressure_state_t state;
    ada_backpressure_state_init(&state, &cfg);
    ada_backpressure_state_set_total_rings(&state, 16u);

    const size_t kIterations = 2'000'000;

    auto start = clock_mono::now();
    for (size_t i = 0; i < kIterations; ++i) {
        ada_backpressure_state_sample(&state, 12u, 0u);
    }
    auto end = clock_mono::now();

    double per_iter_ns = duration_per_iteration_ns(start, end, kIterations);
    RecordProperty("normal_state_per_iter_ns", per_iter_ns);

    constexpr double kMaxAllowedNs = 500.0; // Generous cap to account for debug builds
    EXPECT_LT(per_iter_ns, kMaxAllowedNs)
        << "Normal state check overhead regression: " << per_iter_ns << "ns";
}

TEST(BackpressureBench, drop_execution__average_overhead_within_budget) {
    ada_backpressure_config_t cfg = {};
    cfg.pressure_threshold_percent = 25u;
    cfg.recovery_threshold_percent = 50u;
    cfg.recovery_stable_ns = 1'000'000'000ull;
    cfg.drop_log_interval = UINT32_MAX; // Disable logging during benchmark

    ada_backpressure_state_t state;
    ada_backpressure_state_init(&state, &cfg);

    const size_t kIterations = 500'000;

    auto start = clock_mono::now();
    for (size_t i = 0; i < kIterations; ++i) {
        ada_backpressure_state_on_drop(&state, 256u, 0u);
    }
    auto end = clock_mono::now();

    double per_iter_ns = duration_per_iteration_ns(start, end, kIterations);
    RecordProperty("drop_execution_per_iter_ns", per_iter_ns);

    constexpr double kMaxAllowedNs = 2'500.0; // Allow slack for sanitized/debug builds
    EXPECT_LT(per_iter_ns, kMaxAllowedNs)
        << "Drop execution overhead regression: " << per_iter_ns << "ns";
}

