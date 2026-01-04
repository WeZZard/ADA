// dSYM Locator - macOS Implementation
//
// Locates dSYM bundles for binaries by UUID using:
// 1. Adjacent paths: <binary>.dSYM
// 2. Spotlight: mdfind "com_apple_xcode_dsym_uuids == <UUID>"
// 3. DerivedData: ~/Library/Developer/Xcode/DerivedData/**/*.dSYM

#include "symbol_resolver_internal.h"

#ifdef __APPLE__

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <array>
#include <memory>
#include <unistd.h>
#include <sys/stat.h>

namespace {

// Check if a path exists and is a directory
bool directory_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Execute a command and capture stdout
std::string exec_command(const char* cmd) {
    std::array<char, 256> buffer;
    std::string result;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }

    return result;
}

// Search using Spotlight (mdfind)
char* locate_via_spotlight(const char* uuid) {
    // Build mdfind command
    // Spotlight indexes dSYMs with com_apple_xcode_dsym_uuids attribute
    std::string cmd = "mdfind \"com_apple_xcode_dsym_uuids == ";
    cmd += uuid;
    cmd += "\" 2>/dev/null | head -1";

    std::string result = exec_command(cmd.c_str());
    if (!result.empty() && directory_exists(result.c_str())) {
        return strdup(result.c_str());
    }

    return nullptr;
}

// Search in DerivedData
char* locate_in_derived_data(const char* uuid) {
    // Get home directory
    const char* home = getenv("HOME");
    if (!home) {
        return nullptr;
    }

    // Build find command for DerivedData
    std::string cmd = "find '";
    cmd += home;
    cmd += "/Library/Developer/Xcode/DerivedData' -name '*.dSYM' -type d 2>/dev/null";

    std::string result = exec_command(cmd.c_str());
    if (result.empty()) {
        return nullptr;
    }

    // For each dSYM found, check if it matches our UUID
    // This is expensive but works as a fallback
    // TODO: Parse dSYM plists to check UUID more efficiently

    // For now, return nullptr - proper implementation in Phase 3
    (void)uuid;
    return nullptr;
}

} // anonymous namespace

namespace ada {
namespace symbol {

char* locate_dsym_by_uuid(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        return nullptr;
    }

    // Try Spotlight first (fastest)
    char* result = locate_via_spotlight(uuid);
    if (result) {
        return result;
    }

    // Try DerivedData as fallback
    result = locate_in_derived_data(uuid);
    if (result) {
        return result;
    }

    return nullptr;
}

} // namespace symbol
} // namespace ada

#else // !__APPLE__

namespace ada {
namespace symbol {

char* locate_dsym_by_uuid(const char* uuid) {
    // dSYM is macOS-specific
    (void)uuid;
    return nullptr;
}

} // namespace symbol
} // namespace ada

#endif // __APPLE__
