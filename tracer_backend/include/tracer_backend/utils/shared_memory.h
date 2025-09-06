#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Shared memory segment
typedef struct __SharedMemory __SharedMemory;
typedef __SharedMemory * SharedMemoryRef;

#pragma mark - Shared Memory Constants

// Typed constants for shared memory naming
extern const char ADA_SHM_PREFIX[];
extern const char ADA_ROLE_CONTROL[];
extern const char ADA_ROLE_INDEX[];
extern const char ADA_ROLE_DETAIL[];

// Return this process' session id (generated once).
//
// Supposed to be used by the controller to create shared memory segments.
// The agent shall receive the session id from the controller as environment variable or argument.
uint32_t shared_memory_get_session_id(void);

// Return this process' pid.
//
// Supposed to be used by the controller to create shared memory segments.
// The agent shall receive the pid from the controller as environment variable or argument.
uint32_t shared_memory_get_pid(void);

#pragma mark - Managing Shared Memory Segments

// Create shared memory with unique name derived from role, pid and session id.
//
// - parameter role: The role of the shared memory segment.
// - parameter pid: The host process pid. You get it from `shared_memory_get_pid()`.
// - parameter session_id: The host process session id. You get it from `shared_memory_get_session_id()`.
// - parameter size: The size of the shared memory segment.
// - parameter out_name: The name of the shared memory segment.
// - parameter out_name_len: The length of the shared memory segment name.
//
// Returns the created object and optionally copies the final name into out_name.
SharedMemoryRef shared_memory_create_unique(const char* role, pid_t pid, uint32_t session_id,
                                          size_t size, char* out_name, size_t out_name_len);

// Open shared memory created by shared_memory_create_unique, using the same role, pid, sid.
//
// - parameter role: The role of the shared memory segment.
// - parameter pid: The host process pid. The agent shall receive the pid from the controller as
// environment variable or argument.
// - parameter session_id: The host process session id. The agent shall receive the session id
// from the controller as environment variable or argument.
// - parameter size: The size of the shared memory segment.
//
SharedMemoryRef shared_memory_open_unique(const char* role, pid_t pid, uint32_t session_id,
                                        size_t size);

// Destroy shared memory
void shared_memory_destroy(SharedMemoryRef shm);

// Unlink shared memory (remove from system)
int shared_memory_unlink(SharedMemoryRef shm);

#pragma mark - Accessing Shared Memory Properties

void* shared_memory_get_address(SharedMemoryRef shm);
size_t shared_memory_get_size(SharedMemoryRef shm);
int shared_memory_get_fd(SharedMemoryRef shm);
const char* shared_memory_get_name(SharedMemoryRef shm);
bool shared_memory_is_creator(SharedMemoryRef shm);

#ifdef __cplusplus
}
#endif

#endif // SHARED_MEMORY_H
