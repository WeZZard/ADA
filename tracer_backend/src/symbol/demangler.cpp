// Symbol Demangler
//
// Demangles C++ and Swift symbol names.
// Uses:
//   - C++: cxxabi.h (__cxa_demangle)
//   - Swift: swift-demangle external tool or libswiftDemangle

#include "symbol_resolver_internal.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <memory>
#include <cstdio>
#include <array>

#ifdef __APPLE__
#include <cxxabi.h>
#endif

namespace {

// Check if name starts with prefix
bool starts_with(const char* name, const char* prefix) {
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

// Demangle C++ names using cxxabi
char* demangle_cxx(const char* mangled) {
#ifdef __APPLE__
    int status = 0;
    char* demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    if (status == 0 && demangled) {
        return demangled;
    }
    // status != 0 means demangling failed, return nullptr
    free(demangled);
#else
    (void)mangled;
#endif
    return nullptr;
}

// Demangle Swift names using swift-demangle tool
char* demangle_swift(const char* mangled) {
    // Build command - use xcrun to locate swift-demangle
    std::string cmd = "xcrun swift-demangle -compact '";
    cmd += mangled;
    cmd += "' 2>/dev/null";

    std::array<char, 512> buffer;
    std::string result;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return nullptr;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }

    // If swift demangle returned something different, use it
    if (!result.empty() && result != mangled) {
        return strdup(result.c_str());
    }

    return nullptr;
}

} // anonymous namespace

namespace ada {
namespace symbol {

char* demangle(const char* mangled) {
    if (!mangled || mangled[0] == '\0') {
        return nullptr;
    }

    // Check for Swift mangled names
    // Swift names start with: _$s, $s, _$S, $S (Swift 5+)
    // Or older: _T (Swift 4 and earlier)
    if (starts_with(mangled, "_$s") ||
        starts_with(mangled, "$s") ||
        starts_with(mangled, "_$S") ||
        starts_with(mangled, "$S") ||
        starts_with(mangled, "_T0") ||
        starts_with(mangled, "_T")) {
        char* result = demangle_swift(mangled);
        if (result) {
            return result;
        }
    }

    // Check for C++ mangled names
    // Itanium ABI: _Z prefix
    // macOS also uses _Z for C++
    if (starts_with(mangled, "_Z") || starts_with(mangled, "__Z")) {
        char* result = demangle_cxx(mangled);
        if (result) {
            return result;
        }
    }

    // Not mangled or demangling failed - return a copy
    return strdup(mangled);
}

} // namespace symbol
} // namespace ada
