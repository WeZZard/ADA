// Module UUID extraction for symbol resolution
//
// Extracts platform-specific binary identifiers:
// - macOS: Mach-O LC_UUID
// - Linux: ELF build-id (TODO)
// - Windows: PE GUID (TODO)

#ifndef ADA_MODULE_UUID_H
#define ADA_MODULE_UUID_H

#include <cstdint>

namespace ada {
namespace agent {

// Extract the UUID from a Mach-O binary at the given base address.
// On macOS, this reads the LC_UUID load command.
//
// Parameters:
//   base_address: Runtime base address of the loaded module
//   out_uuid: 16-byte buffer to receive the UUID
//
// Returns:
//   true if UUID was successfully extracted
//   false if base_address is invalid or UUID not found
bool extract_module_uuid(uintptr_t base_address, uint8_t out_uuid[16]);

} // namespace agent
} // namespace ada

#endif // ADA_MODULE_UUID_H
