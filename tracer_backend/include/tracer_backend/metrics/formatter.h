#ifndef TRACER_BACKEND_METRICS_FORMATTER_H
#define TRACER_BACKEND_METRICS_FORMATTER_H

#include <stdbool.h>
#include <stdio.h>

#include <tracer_backend/metrics/metrics_reporter.h>

#ifdef __cplusplus
extern "C" {
#endif

// Write a human-readable report to the provided stream.
bool ada_metrics_formatter_write_text(const ada_metrics_report_view_t* view,
                                      FILE* stream);

// Write a JSON report to the provided stream.
bool ada_metrics_formatter_write_json(const ada_metrics_report_view_t* view,
                                      FILE* stream);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_METRICS_FORMATTER_H
