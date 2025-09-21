#ifndef SYSTEM_VALIDATOR_H
#define SYSTEM_VALIDATOR_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct validator_event_t {
    uint64_t event_id{0};
    int32_t thread_id{0};
    uint64_t timestamp_ns{0};
    int payload_case{0};
};

struct validator_t {
    std::string events_path;
    std::vector<validator_event_t> events;
    uint64_t parse_errors{0};
};

bool validator_load(validator_t* validator, const std::string& events_path,
                    std::string* error_message);
uint64_t validator_total_events(const validator_t* validator);
uint64_t validator_count_for_thread(const validator_t* validator, int32_t thread_id);
bool validator_verify_thread_isolation(const validator_t* validator, std::string* details);
bool validator_verify_temporal_order(const validator_t* validator, std::string* details);

#endif  // SYSTEM_VALIDATOR_H
