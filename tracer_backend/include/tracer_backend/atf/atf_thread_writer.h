/**
 * @file atf_thread_writer.h
 * @brief Public API for ATF v2 thread writer
 *
 * Unified writer API that coordinates index and detail writers for a single thread.
 * Handles bidirectional linking and file finalization.
 */

#ifndef TRACER_BACKEND_ATF_THREAD_WRITER_H
#define TRACER_BACKEND_ATF_THREAD_WRITER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque thread writer handle */
typedef struct AtfThreadWriter AtfThreadWriter;

/**
 * Create a thread writer
 *
 * Creates both index and detail writers for the specified thread.
 *
 * @param session_dir Base session directory (e.g., "ada_traces/session_20250101_120000/pid_12345")
 * @param thread_id Thread ID
 * @param clock_type Clock type (1=mach_continuous, 2=qpc, 3=boottime)
 * @return Pointer to writer, or NULL on error
 */
AtfThreadWriter* atf_thread_writer_create(const char* session_dir,
                                          uint32_t thread_id,
                                          uint8_t clock_type);

/**
 * Write an index event with optional detail
 *
 * @param writer Pointer to writer
 * @param timestamp_ns Timestamp in nanoseconds
 * @param function_id Function ID ((moduleId << 32) | symbolIndex)
 * @param event_kind Event kind (CALL=1, RETURN=2, EXCEPTION=3)
 * @param call_depth Call stack depth
 * @param detail_payload Optional detail payload (NULL if no detail)
 * @param detail_payload_size Size of detail payload (0 if no detail)
 * @return Index sequence number, or UINT32_MAX on error
 */
uint32_t atf_thread_writer_write_event(AtfThreadWriter* writer,
                                       uint64_t timestamp_ns,
                                       uint64_t function_id,
                                       uint32_t event_kind,
                                       uint32_t call_depth,
                                       const void* detail_payload,
                                       size_t detail_payload_size);

/**
 * Finalize both index and detail files
 *
 * Updates headers and writes footers for both files.
 *
 * @param writer Pointer to writer
 * @return 0 on success, negative errno on error
 */
int atf_thread_writer_finalize(AtfThreadWriter* writer);

/**
 * Close and free the thread writer
 *
 * @param writer Pointer to writer
 */
void atf_thread_writer_close(AtfThreadWriter* writer);

#ifdef __cplusplus
}
#endif

#endif /* TRACER_BACKEND_ATF_THREAD_WRITER_H */
