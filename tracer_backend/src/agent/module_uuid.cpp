// Module UUID extraction for macOS (Mach-O LC_UUID)
//
// Extracts the UUID from a Mach-O binary loaded at the given base address.
// This UUID is used to match binaries with their dSYM debug symbols.

#include <tracer_backend/agent/module_uuid.h>

#ifdef __APPLE__
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <cstring>

namespace ada {
namespace agent {

bool extract_module_uuid(uintptr_t base_address, uint8_t out_uuid[16]) {
    if (base_address == 0 || out_uuid == nullptr) {
        return false;
    }

    // Clear output
    std::memset(out_uuid, 0, 16);

    // Check Mach-O magic
    const uint32_t magic = *reinterpret_cast<const uint32_t*>(base_address);

    const struct mach_header_64* header = nullptr;
    const uint8_t* load_commands_start = nullptr;
    uint32_t ncmds = 0;

    if (magic == MH_MAGIC_64) {
        // 64-bit Mach-O
        header = reinterpret_cast<const struct mach_header_64*>(base_address);
        load_commands_start = reinterpret_cast<const uint8_t*>(base_address + sizeof(struct mach_header_64));
        ncmds = header->ncmds;
    } else if (magic == MH_MAGIC) {
        // 32-bit Mach-O (less common but handle for completeness)
        const struct mach_header* header32 = reinterpret_cast<const struct mach_header*>(base_address);
        load_commands_start = reinterpret_cast<const uint8_t*>(base_address + sizeof(struct mach_header));
        ncmds = header32->ncmds;
    } else {
        // Not a Mach-O or FAT binary at this address
        return false;
    }

    // Iterate through load commands to find LC_UUID
    const uint8_t* cmd_ptr = load_commands_start;
    for (uint32_t i = 0; i < ncmds; ++i) {
        const struct load_command* lc = reinterpret_cast<const struct load_command*>(cmd_ptr);

        if (lc->cmd == LC_UUID) {
            const struct uuid_command* uuid_cmd = reinterpret_cast<const struct uuid_command*>(cmd_ptr);
            std::memcpy(out_uuid, uuid_cmd->uuid, 16);
            return true;
        }

        cmd_ptr += lc->cmdsize;
    }

    // LC_UUID not found (some stripped binaries may not have it)
    return false;
}

} // namespace agent
} // namespace ada

#else // !__APPLE__

// TODO: Linux implementation (ELF build-id)
// TODO: Windows implementation (PE GUID)

namespace ada {
namespace agent {

bool extract_module_uuid(uintptr_t base_address, uint8_t out_uuid[16]) {
    (void)base_address;
    if (out_uuid) {
        std::memset(out_uuid, 0, 16);
    }
    return false;
}

} // namespace agent
} // namespace ada

#endif // __APPLE__
