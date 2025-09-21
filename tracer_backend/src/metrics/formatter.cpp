#include <iomanip>
#include <ostream>
#include <sstream>

extern "C" {
#include <tracer_backend/metrics/formatter.h>
}

namespace {

static const char* to_kind_label(ada_metrics_report_kind_t kind) {
    switch (kind) {
        case ADA_METRICS_REPORT_KIND_PERIODIC:
            return "periodic";
        case ADA_METRICS_REPORT_KIND_FORCED:
            return "forced";
        case ADA_METRICS_REPORT_KIND_SUMMARY:
            return "summary";
        default:
            return "unknown";
    }
}

static void write_thread_text_line(const ada_thread_metrics_snapshot_t& snap,
                                   std::ostream& os) {
    os << "  thread=" << snap.thread_id
       << " slot=" << snap.slot_index
       << " events=" << snap.events_written
       << " dropped=" << snap.events_dropped
       << " filtered=" << snap.events_filtered
       << " bytes=" << snap.bytes_written
       << " eps=" << std::fixed << std::setprecision(2) << snap.events_per_second
       << " bps=" << std::fixed << std::setprecision(2) << snap.bytes_per_second
       << " drop%=" << std::fixed << std::setprecision(2) << snap.drop_rate_percent
       << " swaps=" << snap.swap_count
       << " swaps_per_s=" << std::fixed << std::setprecision(2) << snap.swaps_per_second
       << " avg_swap_ns=" << snap.avg_swap_duration_ns
       << '\n';
}

static void write_threads_json(const ada_metrics_report_view_t* view,
                               std::ostream& os) {
    os << "\"threads\":[";
    for (size_t i = 0; i < view->snapshot_count; ++i) {
        if (i != 0u) {
            os << ',';
        }
        const ada_thread_metrics_snapshot_t& snap = view->snapshots[i];
        os << '{'
           << "\"thread_id\":" << snap.thread_id << ','
           << "\"slot_index\":" << snap.slot_index << ','
           << "\"events_written\":" << snap.events_written << ','
           << "\"events_dropped\":" << snap.events_dropped << ','
           << "\"events_filtered\":" << snap.events_filtered << ','
           << "\"bytes_written\":" << snap.bytes_written << ','
           << "\"events_per_second\":" << std::setprecision(6) << std::fixed << snap.events_per_second << ','
           << "\"bytes_per_second\":" << std::setprecision(6) << std::fixed << snap.bytes_per_second << ','
           << "\"drop_rate_percent\":" << std::setprecision(6) << std::fixed << snap.drop_rate_percent << ','
           << "\"swap_count\":" << snap.swap_count << ','
           << "\"swaps_per_second\":" << std::setprecision(6) << std::fixed << snap.swaps_per_second << ','
           << "\"avg_swap_duration_ns\":" << snap.avg_swap_duration_ns << '}'
           << std::setprecision(2);
    }
    os << ']';
}

} // namespace

extern "C" {

bool ada_metrics_formatter_write_text(const ada_metrics_report_view_t* view,
                                      FILE* stream) {
    if (!view || !stream) {
        return false;
    }

    std::ostringstream oss;
    oss << "[metrics][" << to_kind_label(view->kind) << "]"
        << " ts=" << view->timestamp_ns
        << " total_events=" << view->totals.total_events_written
        << " dropped=" << view->totals.total_events_dropped
        << " filtered=" << view->totals.total_events_filtered
        << " bytes=" << view->totals.total_bytes_written
        << " active_threads=" << view->totals.active_thread_count
        << " eps=" << std::fixed << std::setprecision(2) << view->rates.system_events_per_second
        << " bps=" << std::fixed << std::setprecision(2) << view->rates.system_bytes_per_second
        << " window_ns=" << view->rates.last_window_ns
        << '\n';

    for (size_t i = 0; i < view->snapshot_count; ++i) {
        write_thread_text_line(view->snapshots[i], oss);
    }

    const std::string str = oss.str();
    size_t written = fwrite(str.data(), 1, str.size(), stream);
    fflush(stream);
    return written == str.size();
}

bool ada_metrics_formatter_write_json(const ada_metrics_report_view_t* view,
                                      FILE* stream) {
    if (!view || !stream) {
        return false;
    }

    std::ostringstream oss;
    oss << '{'
        << "\"kind\":\"" << to_kind_label(view->kind) << "\",";
    oss << "\"timestamp_ns\":" << view->timestamp_ns << ','
        << "\"totals\":{"
        << "\"events_written\":" << view->totals.total_events_written << ','
        << "\"events_dropped\":" << view->totals.total_events_dropped << ','
        << "\"events_filtered\":" << view->totals.total_events_filtered << ','
        << "\"bytes_written\":" << view->totals.total_bytes_written << ','
        << "\"active_threads\":" << view->totals.active_thread_count
        << "},"
        << "\"rates\":{"
        << "\"events_per_second\":" << std::setprecision(6) << std::fixed << view->rates.system_events_per_second << ','
        << "\"bytes_per_second\":" << std::setprecision(6) << std::fixed << view->rates.system_bytes_per_second << ','
        << "\"window_ns\":" << view->rates.last_window_ns
        << "},";

    write_threads_json(view, oss);
    oss << "}\n";

    const std::string str = oss.str();
    size_t written = fwrite(str.data(), 1, str.size(), stream);
    fflush(stream);
    return written == str.size();
}

} // extern "C"
