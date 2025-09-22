use std::{net::IpAddr, time::Instant};

use dashmap::DashMap;
use parking_lot::Mutex;

#[derive(Debug)]
struct TokenBucket {
    tokens: f64,
    last_refill: Instant,
}

impl TokenBucket {
    fn new(capacity: f64) -> Self {
        Self {
            tokens: capacity,
            last_refill: Instant::now(),
        }
    }

    fn try_acquire(&mut self, capacity: f64, refill_per_sec: f64) -> bool {
        self.refill(capacity, refill_per_sec);
        if self.tokens >= 1.0 {
            self.tokens -= 1.0;
            true
        } else {
            false
        }
    }

    fn refill(&mut self, capacity: f64, refill_per_sec: f64) {
        let now = Instant::now();
        let elapsed = now.saturating_duration_since(self.last_refill);
        if elapsed.is_zero() {
            return;
        }
        self.last_refill = now;
        let refill = elapsed.as_secs_f64() * refill_per_sec;
        self.tokens = (self.tokens + refill).min(capacity);
    }
}

/// Simple per-IP token bucket rate limiter.
pub struct RateLimiter {
    capacity: f64,
    refill_per_sec: f64,
    buckets: DashMap<IpAddr, Mutex<TokenBucket>>,
    unlimited: bool,
}

impl RateLimiter {
    pub fn new(max_requests_per_second: u32) -> Self {
        if max_requests_per_second == 0 {
            return Self {
                capacity: f64::INFINITY,
                refill_per_sec: f64::INFINITY,
                buckets: DashMap::new(),
                unlimited: true,
            };
        }

        let capacity = max_requests_per_second as f64;
        Self {
            capacity,
            refill_per_sec: capacity,
            buckets: DashMap::new(),
            unlimited: false,
        }
    }

    pub fn allow(&self, ip: IpAddr) -> bool {
        if self.unlimited {
            return true;
        }

        let entry = self
            .buckets
            .entry(ip)
            .or_insert_with(|| Mutex::new(TokenBucket::new(self.capacity)));
        let mut bucket = entry.lock();
        bucket.try_acquire(self.capacity, self.refill_per_sec)
    }

    #[cfg(test)]
    pub fn tracked_ips(&self) -> usize {
        self.buckets.len()
    }

    pub fn capacity(&self) -> f64 {
        self.capacity
    }
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;
    use std::{
        net::{IpAddr, Ipv4Addr},
        thread,
        time::Duration,
    };

    fn localhost() -> IpAddr {
        IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))
    }

    #[test]
    fn json_rpc_rate_limit__rapid_successive_calls__then_triggers_zero_duration_check() {
        // This test specifically targets the zero-duration check on line 34
        let limiter = RateLimiter::new(2);
        let ip = localhost();

        // These rapid calls should trigger the zero-duration check in refill()
        assert!(limiter.allow(ip));
        assert!(limiter.allow(ip)); // Immediate call, likely zero duration
        assert!(!limiter.allow(ip)); // Should fail, no time to refill
    }

    #[test]
    fn json_rpc_rate_limit__allow_within_capacity__then_succeeds() {
        let limiter = RateLimiter::new(2);
        let ip = localhost();

        assert!(limiter.allow(ip));
        assert!(limiter.allow(ip));
        assert!(!limiter.allow(ip));
        assert_eq!(limiter.tracked_ips(), 1);
    }

    #[test]
    fn json_rpc_rate_limit__refill_after_sleep__then_allows_again() {
        let limiter = RateLimiter::new(1);
        let ip = localhost();

        assert!(limiter.allow(ip));
        assert!(!limiter.allow(ip));

        thread::sleep(Duration::from_millis(1100));

        assert!(limiter.allow(ip));
    }

    #[test]
    fn json_rpc_rate_limit__unlimited_configuration__then_always_allows() {
        let limiter = RateLimiter::new(0);
        let ip = localhost();

        for _ in 0..100 {
            assert!(limiter.allow(ip));
        }
        assert_eq!(limiter.tracked_ips(), 0);
        assert!(limiter.capacity().is_infinite());
    }
}
