# Design Guide: A Robust Shared Memory IPC Mechanism

This document outlines the design principles for a high-performance, cross-language Inter-Process Communication (IPC) mechanism suitable for a tracer/agent architecture.

---

## 1. Core Technology: Shared Memory

For maximum performance, **shared memory** is the chosen IPC method. Its key advantage is that it's a **zero-copy** technique, meaning the CPU is not wasted copying data between the controller and the agent. Both processes operate on the exact same region of physical RAM, enabling the fastest possible data exchange.

---

## 2. Choosing the Right Type of Shared Memory

There are two fundamental types of shared memory, each with a distinct trade-off between performance and reliability.

### a. Anonymous Shared Memory

* **How it Works**: A volatile memory region created directly in RAM without being tied to a file on the filesystem. It is identified by a name (e.g., `/my_app_shm`).
* **Pros**:
  * **Peak Performance**: The absolute fastest option, with minimal setup latency and no disk I/O involved.
  * **Simplicity**: Easier to manage for temporary data, as the OS handles cleanup on reboot.
* **Cons**:
  * **Volatility**: All data is instantly lost if the process that created it crashes or the system reboots.
* **Best For**: **An MVP (Minimum Viable Product)** where the primary goal is to validate core functionality and raw speed, and crash recovery is not yet a requirement.

### b. File-Backed Shared Memory

* **How it Works**: A region of memory that uses a regular file on the filesystem as its backing store.
* **Pros**:
  * **Persistence**: Data survives process crashes and system reboots, making it essential for crash recovery and post-mortem analysis.
  * **Easier Debugging**: The backing file can be inspected with standard command-line tools.
  * **Simple Initialization**: Can be pre-loaded with data just by writing to the file.
* **Cons**:
  * **Slower Setup**: Involves file system I/O to create and configure the backing file.
  * **Performance Considerations**: While runtime access is at RAM speed, poor data locality (random reads/writes) can trigger frequent **page faults**, forcing the OS to load data from disk and hurting performance.
* **Best For**: **A production system** where reliability, data integrity, and the ability to survive crashes are critical requirements.

---

## 3. Synchronization: A Non-Negotiable Requirement

Shared memory is not inherently safe. Without a synchronization mechanism, you will encounter race conditions that lead to data corruption.

* **The Solution**: Use a **Named POSIX Semaphore**.
* **Why**: It is an OS-level primitive, making it language-agnostic. A C-based controller and a Rust-based agent can both open the same semaphore by name (e.g., `/my_app_semaphore`) to coordinate access, ensuring only one process is writing or reading at any given moment.

---

## 4. Architectural Pattern: Opaque Type with a C API

To ensure safety, maintainability, and ease of use across different languages, the shared memory implementation should be hidden behind a clean abstraction layer.

* **The Strategy**: Expose the shared memory functionality as an **opaque type** (or "opaque pointer") via a C API.
* **How it Works**:
    1. A C header file (`.h`) declares a `struct` but does not define its members (e.g., `typedef struct TracerShm TracerShm;`).
    2. The header exposes a set of functions for operating on this opaque pointer (e.g., `tracer_shm_create()`, `tracer_shm_write()`, `tracer_shm_read()`, `tracer_shm_destroy()`).
    3. The implementation details (whether it's anonymous or file-backed, the synchronization logic, the data structures used) are entirely contained within the C source file (`.c`).
* **Benefits**:
  * **Encapsulation**: The agent and controller don't need to know the internal details. You can change the implementation (e.g., switch from anonymous to file-backed) without changing any client code.
  * **Safety**: The API enforces correct usage. Clients cannot forget to lock a semaphore or corrupt internal metadata.
  * **FFI-Friendly**: A stable C API is the universal standard for interoperability, making it straightforward to call from Rust, Python, or any other language.
