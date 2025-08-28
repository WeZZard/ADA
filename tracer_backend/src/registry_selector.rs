// registry_selector.rs - Runtime selection between C and C++ implementations

use std::env;
use std::sync::Once;

static INIT: Once = Once::new();
static mut USE_CPP: bool = false;

/// Initialize the registry implementation selector based on environment
pub fn init_registry_selector() {
    INIT.call_once(|| {
        // Check environment variable for runtime override
        let use_cpp_env = env::var("ADA_USE_CPP_REGISTRY")
            .unwrap_or_else(|_| String::from("default"));
        
        // Determine which implementation to use
        unsafe {
            USE_CPP = match use_cpp_env.as_str() {
                "true" | "1" | "yes" => {
                    log::info!("Using C++ thread registry (runtime override)");
                    true
                },
                "false" | "0" | "no" => {
                    log::info!("Using C thread registry (runtime override)");
                    false
                },
                "default" | _ => {
                    // Use compile-time feature flag
                    #[cfg(feature = "use-cpp-registry")]
                    {
                        log::info!("Using C++ thread registry (feature flag)");
                        true
                    }
                    #[cfg(not(feature = "use-cpp-registry"))]
                    {
                        log::info!("Using C thread registry (default)");
                        false
                    }
                }
            };
        }
        
        // Log the decision
        log::info!("Thread registry implementation selected: {}", 
                  if unsafe { USE_CPP } { "C++" } else { "C" });
    });
}

/// Check if C++ implementation should be used
pub fn should_use_cpp_registry() -> bool {
    init_registry_selector();
    unsafe { USE_CPP }
}

/// Get implementation name for metrics/logging
pub fn get_registry_implementation_name() -> &'static str {
    if should_use_cpp_registry() {
        "cpp"
    } else {
        "c"
    }
}

/// Performance metrics collection
pub mod metrics {
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::time::Instant;
    
    pub struct RegistryMetrics {
        pub registration_count: AtomicU64,
        pub registration_time_ns: AtomicU64,
        pub lookup_count: AtomicU64,
        pub lookup_time_ns: AtomicU64,
    }
    
    impl RegistryMetrics {
        pub const fn new() -> Self {
            Self {
                registration_count: AtomicU64::new(0),
                registration_time_ns: AtomicU64::new(0),
                lookup_count: AtomicU64::new(0),
                lookup_time_ns: AtomicU64::new(0),
            }
        }
        
        pub fn record_registration(&self, duration_ns: u64) {
            self.registration_count.fetch_add(1, Ordering::Relaxed);
            self.registration_time_ns.fetch_add(duration_ns, Ordering::Relaxed);
        }
        
        pub fn record_lookup(&self, duration_ns: u64) {
            self.lookup_count.fetch_add(1, Ordering::Relaxed);
            self.lookup_time_ns.fetch_add(duration_ns, Ordering::Relaxed);
        }
        
        pub fn get_average_registration_ns(&self) -> f64 {
            let count = self.registration_count.load(Ordering::Relaxed);
            if count == 0 {
                0.0
            } else {
                self.registration_time_ns.load(Ordering::Relaxed) as f64 / count as f64
            }
        }
        
        pub fn get_average_lookup_ns(&self) -> f64 {
            let count = self.lookup_count.load(Ordering::Relaxed);
            if count == 0 {
                0.0
            } else {
                self.lookup_time_ns.load(Ordering::Relaxed) as f64 / count as f64
            }
        }
    }
    
    // Global metrics for both implementations
    pub static C_METRICS: RegistryMetrics = RegistryMetrics::new();
    pub static CPP_METRICS: RegistryMetrics = RegistryMetrics::new();
    
    /// Get metrics for current implementation
    pub fn get_current_metrics() -> &'static RegistryMetrics {
        if super::should_use_cpp_registry() {
            &CPP_METRICS
        } else {
            &C_METRICS
        }
    }
    
    /// Timer for measuring operations
    pub struct Timer {
        start: Instant,
    }
    
    impl Timer {
        pub fn start() -> Self {
            Self {
                start: Instant::now(),
            }
        }
        
        pub fn elapsed_ns(&self) -> u64 {
            self.start.elapsed().as_nanos() as u64
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_environment_override() {
        // Test that environment variable overrides feature flag
        env::set_var("ADA_USE_CPP_REGISTRY", "true");
        assert!(should_use_cpp_registry());
        
        // Reset for other tests
        env::remove_var("ADA_USE_CPP_REGISTRY");
    }
    
    #[test]
    fn test_metrics_collection() {
        let metrics = &metrics::C_METRICS;
        metrics.record_registration(1000);
        metrics.record_registration(2000);
        
        assert_eq!(metrics.registration_count.load(Ordering::Relaxed), 2);
        assert_eq!(metrics.get_average_registration_ns(), 1500.0);
    }
}