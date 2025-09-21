#include "system_validator.h"

#include "system_trace_constants.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

extern "C" {
#include "trace_schema.pb-c.h"
}

namespace {

constexpr uint64_t kNanosPerSecond = 1000000000ull;

bool decode_varint(const uint8_t* data, size_t remaining, size_t* consumed, uint64_t* value) {
    uint64_t result = 0;
    uint32_t shift = 0;
    size_t index = 0;
    while (index < remaining && shift < 64) {
        uint8_t byte = data[index];
        result |= static_cast<uint64_t>(byte & 0x7Fu) << shift;
        ++index;
        if ((byte & 0x80u) == 0) {
            *consumed = index;
            *value = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

uint64_t to_timestamp_ns(const Google__Protobuf__Timestamp* ts) {
    if (!ts) {
        return 0;
    }
    uint64_t seconds = static_cast<uint64_t>(ts->seconds);
    uint64_t nanos = static_cast<uint64_t>(ts->nanos);
    return seconds * kNanosPerSecond + nanos;
}

}  // namespace

bool validator_load(validator_t* validator, const std::string& events_path,
                    std::string* error_message) {
    if (!validator) {
        if (error_message) {
            *error_message = "validator pointer was null";
        }
        return false;
    }

    validator->events.clear();
    validator->parse_errors = 0;
    validator->events_path = events_path;

    std::ifstream file(events_path, std::ios::binary);
    if (!file) {
        if (error_message) {
            *error_message = "failed to open events file: " + events_path;
        }
        return false;
    }

    file.seekg(0, std::ios::end);
    std::streamsize total_size = file.tellg();
    if (total_size <= 0) {
        if (error_message) {
            *error_message = "events file is empty";
        }
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<size_t>(total_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), total_size)) {
        if (error_message) {
            *error_message = "failed to read events file";
        }
        return false;
    }

    size_t offset = 0;
    while (offset < buffer.size()) {
        size_t consumed = 0;
        uint64_t length = 0;
        if (!decode_varint(buffer.data() + offset, buffer.size() - offset, &consumed, &length)) {
            validator->parse_errors++;
            break;
        }
        offset += consumed;
        if (offset + length > buffer.size()) {
            validator->parse_errors++;
            break;
        }

        Event* evt = event__unpack(nullptr, static_cast<size_t>(length), buffer.data() + offset);
        if (!evt) {
            validator->parse_errors++;
            offset += length;
            continue;
        }

        validator_event_t record{};
        record.event_id = evt->event_id;
        record.thread_id = evt->thread_id;
        record.timestamp_ns = to_timestamp_ns(evt->timestamp);
        record.payload_case = evt->payload_case;
        validator->events.push_back(record);

        event__free_unpacked(evt, nullptr);
        offset += length;
    }

    if (!validator->events.empty()) {
        std::sort(validator->events.begin(), validator->events.end(),
                  [](const validator_event_t& a, const validator_event_t& b) {
                      return a.timestamp_ns < b.timestamp_ns;
                  });
    }

    if (validator->parse_errors > 0 && error_message) {
        std::ostringstream oss;
        oss << "detected " << validator->parse_errors << " parse error(s) while loading ATF events";
        *error_message = oss.str();
    }

    return !validator->events.empty();
}

uint64_t validator_total_events(const validator_t* validator) {
    return validator ? static_cast<uint64_t>(validator->events.size()) : 0;
}

uint64_t validator_count_for_thread(const validator_t* validator, int32_t thread_id) {
    if (!validator) {
        return 0;
    }
    return static_cast<uint64_t>(std::count_if(
        validator->events.begin(), validator->events.end(),
        [thread_id](const validator_event_t& evt) { return evt.thread_id == thread_id; }));
}

bool validator_verify_thread_isolation(const validator_t* validator, std::string* details) {
    if (!validator) {
        if (details) {
            *details = "validator pointer was null";
        }
        return false;
    }

    std::unordered_map<int32_t, validator_event_t> last_by_thread;
    for (const auto& evt : validator->events) {
        if (evt.thread_id == kTraceLifecycleThreadId) {
            continue;
        }
        auto it = last_by_thread.find(evt.thread_id);
        if (it != last_by_thread.end()) {
            if (evt.event_id != 0 && it->second.event_id != 0 && evt.event_id <= it->second.event_id) {
                if (details) {
                    std::ostringstream oss;
                    oss << "event_id regression for thread " << evt.thread_id << ": " << evt.event_id
                        << " <= " << it->second.event_id;
                    *details = oss.str();
                }
                return false;
            }
            if (evt.timestamp_ns < it->second.timestamp_ns) {
                if (details) {
                    std::ostringstream oss;
                    oss << "timestamp regression for thread " << evt.thread_id << ": " << evt.timestamp_ns
                        << " < " << it->second.timestamp_ns;
                    *details = oss.str();
                }
                return false;
            }
        }
        last_by_thread[evt.thread_id] = evt;
    }

    if (details) {
        *details = "thread isolation checks passed";
    }
    return true;
}

bool validator_verify_temporal_order(const validator_t* validator, std::string* details) {
    if (!validator) {
        if (details) {
            *details = "validator pointer was null";
        }
        return false;
    }

    uint64_t last_timestamp = 0;
    for (const auto& evt : validator->events) {
        if (evt.timestamp_ns < last_timestamp) {
            if (details) {
                std::ostringstream oss;
                oss << "non-monotonic timestamp detected: " << evt.timestamp_ns
                    << " < " << last_timestamp;
                *details = oss.str();
            }
            return false;
        }
        last_timestamp = evt.timestamp_ns;
    }

    if (details) {
        *details = "temporal order is monotonic";
    }
    return true;
}
