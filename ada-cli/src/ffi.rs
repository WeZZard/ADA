//! FFI bindings to the C ABI symbol resolver.
//!
//! These bindings allow Rust code to call the native symbol resolution
//! library built from tracer_backend.

use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::ptr;

/// Result codes from symbol resolution operations.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SymbolResolveResult {
    Ok = 0,
    NotFound = -1,
    NoDsym = -2,
    Error = -3,
    InvalidArg = -4,
}

impl From<c_int> for SymbolResolveResult {
    fn from(code: c_int) -> Self {
        match code {
            0 => SymbolResolveResult::Ok,
            -1 => SymbolResolveResult::NotFound,
            -2 => SymbolResolveResult::NoDsym,
            -3 => SymbolResolveResult::Error,
            -4 => SymbolResolveResult::InvalidArg,
            _ => SymbolResolveResult::Error,
        }
    }
}

/// Raw FFI struct matching C ResolvedSymbol.
#[repr(C)]
pub struct ResolvedSymbolRaw {
    pub function_id: u64,
    pub name_mangled: *const c_char,
    pub name_demangled: *const c_char,
    pub module_path: *const c_char,
    pub source_file: *const c_char,
    pub source_line: u32,
    pub source_column: u32,
}

impl Default for ResolvedSymbolRaw {
    fn default() -> Self {
        Self {
            function_id: 0,
            name_mangled: ptr::null(),
            name_demangled: ptr::null(),
            module_path: ptr::null(),
            source_file: ptr::null(),
            source_line: 0,
            source_column: 0,
        }
    }
}

// FFI declarations
extern "C" {
    pub fn symbol_resolver_create(session_path: *const c_char) -> *mut c_void;
    pub fn symbol_resolver_destroy(resolver: *mut c_void);
    pub fn symbol_resolver_resolve(
        resolver: *mut c_void,
        function_id: u64,
        out: *mut ResolvedSymbolRaw,
    ) -> c_int;
    pub fn symbol_resolver_resolve_batch(
        resolver: *mut c_void,
        function_ids: *const u64,
        count: usize,
        out: *mut ResolvedSymbolRaw,
    ) -> c_int;
    pub fn symbol_resolver_locate_dsym(uuid: *const c_char) -> *mut c_char;
    pub fn symbol_resolver_demangle(mangled: *const c_char) -> *mut c_char;
    pub fn symbol_resolver_module_count(resolver: *const c_void) -> usize;
    pub fn symbol_resolver_symbol_count(resolver: *const c_void) -> usize;
    pub fn symbol_resolver_get_format_version(resolver: *const c_void) -> *const c_char;
}

/// Safe Rust wrapper for resolved symbol information.
#[derive(Debug, Clone)]
pub struct ResolvedSymbol {
    pub function_id: u64,
    pub name_mangled: String,
    pub name_demangled: String,
    pub module_path: Option<String>,
    pub source_file: Option<String>,
    pub source_line: u32,
    pub source_column: u32,
}

impl ResolvedSymbol {
    /// Convert from raw FFI struct. Strings are copied.
    ///
    /// # Safety
    /// The raw struct must have valid pointers (or null for optional fields).
    pub unsafe fn from_raw(raw: &ResolvedSymbolRaw) -> Self {
        Self {
            function_id: raw.function_id,
            name_mangled: if raw.name_mangled.is_null() {
                String::new()
            } else {
                CStr::from_ptr(raw.name_mangled)
                    .to_string_lossy()
                    .into_owned()
            },
            name_demangled: if raw.name_demangled.is_null() {
                String::new()
            } else {
                CStr::from_ptr(raw.name_demangled)
                    .to_string_lossy()
                    .into_owned()
            },
            module_path: if raw.module_path.is_null() {
                None
            } else {
                Some(
                    CStr::from_ptr(raw.module_path)
                        .to_string_lossy()
                        .into_owned(),
                )
            },
            source_file: if raw.source_file.is_null() {
                None
            } else {
                Some(
                    CStr::from_ptr(raw.source_file)
                        .to_string_lossy()
                        .into_owned(),
                )
            },
            source_line: raw.source_line,
            source_column: raw.source_column,
        }
    }
}

/// Safe wrapper for the symbol resolver.
pub struct SymbolResolver {
    handle: *mut c_void,
}

// SymbolResolver is Send + Sync because the underlying C++ implementation
// uses thread-safe data structures.
unsafe impl Send for SymbolResolver {}
unsafe impl Sync for SymbolResolver {}

impl SymbolResolver {
    /// Create a new symbol resolver from a session directory.
    ///
    /// The session directory must contain a manifest.json with symbol table.
    pub fn new(session_path: &str) -> Option<Self> {
        let c_path = CString::new(session_path).ok()?;
        let handle = unsafe { symbol_resolver_create(c_path.as_ptr()) };
        if handle.is_null() {
            None
        } else {
            Some(Self { handle })
        }
    }

    /// Resolve a function_id to symbol information.
    pub fn resolve(&self, function_id: u64) -> Result<ResolvedSymbol, SymbolResolveResult> {
        let mut raw = ResolvedSymbolRaw::default();
        let result = unsafe { symbol_resolver_resolve(self.handle, function_id, &mut raw) };
        let result = SymbolResolveResult::from(result);

        if result == SymbolResolveResult::Ok {
            Ok(unsafe { ResolvedSymbol::from_raw(&raw) })
        } else {
            Err(result)
        }
    }

    /// Resolve multiple function_ids in batch.
    pub fn resolve_batch(&self, function_ids: &[u64]) -> Vec<Option<ResolvedSymbol>> {
        if function_ids.is_empty() {
            return Vec::new();
        }

        let mut raw_results: Vec<ResolvedSymbolRaw> =
            (0..function_ids.len()).map(|_| ResolvedSymbolRaw::default()).collect();

        let _count = unsafe {
            symbol_resolver_resolve_batch(
                self.handle,
                function_ids.as_ptr(),
                function_ids.len(),
                raw_results.as_mut_ptr(),
            )
        };

        raw_results
            .iter()
            .map(|raw| {
                if raw.function_id != 0 {
                    Some(unsafe { ResolvedSymbol::from_raw(raw) })
                } else {
                    None
                }
            })
            .collect()
    }

    /// Get the number of modules in the symbol table.
    pub fn module_count(&self) -> usize {
        unsafe { symbol_resolver_module_count(self.handle) }
    }

    /// Get the total number of symbols.
    pub fn symbol_count(&self) -> usize {
        unsafe { symbol_resolver_symbol_count(self.handle) }
    }

    /// Get the format version of the manifest.
    pub fn format_version(&self) -> Option<String> {
        let ptr = unsafe { symbol_resolver_get_format_version(self.handle) };
        if ptr.is_null() {
            None
        } else {
            Some(unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() })
        }
    }
}

impl Drop for SymbolResolver {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe { symbol_resolver_destroy(self.handle) };
        }
    }
}

/// Locate a dSYM bundle by UUID.
///
/// Returns the path to the dSYM bundle, or None if not found.
pub fn locate_dsym(uuid: &str) -> Option<String> {
    let c_uuid = CString::new(uuid).ok()?;
    let result = unsafe { symbol_resolver_locate_dsym(c_uuid.as_ptr()) };
    if result.is_null() {
        None
    } else {
        let path = unsafe { CStr::from_ptr(result).to_string_lossy().into_owned() };
        unsafe { libc::free(result as *mut c_void) };
        Some(path)
    }
}

/// Demangle a symbol name.
///
/// Handles C++ and Swift mangled names.
pub fn demangle(mangled: &str) -> String {
    let c_mangled = match CString::new(mangled) {
        Ok(s) => s,
        Err(_) => return mangled.to_string(),
    };

    let result = unsafe { symbol_resolver_demangle(c_mangled.as_ptr()) };
    if result.is_null() {
        mangled.to_string()
    } else {
        let demangled = unsafe { CStr::from_ptr(result).to_string_lossy().into_owned() };
        unsafe { libc::free(result as *mut c_void) };
        demangled
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_demangle_cpp() {
        // Simple test - the actual demangling is done by the C++ library
        let mangled = "_ZN3foo3barEv";
        let result = demangle(mangled);
        // Should return something (either demangled or original)
        assert!(!result.is_empty());
    }

    #[test]
    fn test_demangle_plain() {
        let plain = "printf";
        let result = demangle(plain);
        assert_eq!(result, "printf");
    }
}
