#ifndef SYSTEM_TRACE_CONSTANTS_H
#define SYSTEM_TRACE_CONSTANTS_H

#include <cstdint>
#include <limits>

// Reserved thread identifier for trace lifecycle events to avoid worker collisions
constexpr int32_t kTraceLifecycleThreadId =
    static_cast<int32_t>(std::numeric_limits<uint32_t>::max());

static_assert(kTraceLifecycleThreadId == -1,
              "Trace lifecycle thread id must map to sentinel value -1");

#endif  // SYSTEM_TRACE_CONSTANTS_H
