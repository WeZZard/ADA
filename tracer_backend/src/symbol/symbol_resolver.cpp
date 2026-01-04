// Symbol Resolver - Main C ABI implementation
//
// This file provides the C ABI entry points that delegate to platform-specific
// implementations. The resolver loads the manifest.json symbol table and
// provides resolution services.

#include <tracer_backend/symbol/symbol_resolver.h>
#include "symbol_resolver_internal.h"

#include <cstdlib>
#include <cstring>
#include <cerrno>

// =============================================================================
// C ABI Implementation
// =============================================================================

extern "C" {

SymbolResolver* symbol_resolver_create(const char* session_path) {
    if (!session_path || session_path[0] == '\0') {
        errno = EINVAL;
        return nullptr;
    }

    auto* resolver = new (std::nothrow) SymbolResolver();
    if (!resolver) {
        errno = ENOMEM;
        return nullptr;
    }

    if (!resolver->load_manifest(session_path)) {
        delete resolver;
        return nullptr;
    }

    return resolver;
}

void symbol_resolver_destroy(SymbolResolver* resolver) {
    delete resolver;
}

int symbol_resolver_resolve(
    SymbolResolver* resolver,
    uint64_t function_id,
    ResolvedSymbol* out
) {
    if (!resolver || !out) {
        return SYMBOL_RESOLVE_INVALID_ARG;
    }

    return resolver->resolve(function_id, out);
}

int symbol_resolver_resolve_batch(
    SymbolResolver* resolver,
    const uint64_t* function_ids,
    size_t count,
    ResolvedSymbol* out
) {
    if (!resolver || !function_ids || !out || count == 0) {
        return -1;
    }

    int resolved_count = 0;
    for (size_t i = 0; i < count; ++i) {
        int result = resolver->resolve(function_ids[i], &out[i]);
        if (result == SYMBOL_RESOLVE_OK) {
            resolved_count++;
        } else {
            // Mark failed resolutions
            out[i].function_id = 0;
        }
    }

    return resolved_count;
}

char* symbol_resolver_locate_dsym(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        return nullptr;
    }

    return ada::symbol::locate_dsym_by_uuid(uuid);
}

char* symbol_resolver_demangle(const char* mangled) {
    if (!mangled) {
        return nullptr;
    }

    return ada::symbol::demangle(mangled);
}

size_t symbol_resolver_module_count(const SymbolResolver* resolver) {
    if (!resolver) {
        return 0;
    }
    return resolver->module_count();
}

size_t symbol_resolver_symbol_count(const SymbolResolver* resolver) {
    if (!resolver) {
        return 0;
    }
    return resolver->symbol_count();
}

const char* symbol_resolver_get_format_version(const SymbolResolver* resolver) {
    if (!resolver) {
        return nullptr;
    }
    return resolver->format_version();
}

} // extern "C"
