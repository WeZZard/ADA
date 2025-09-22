use std::convert::TryFrom;

use prost::{Message, Oneof};
use prost_types::Timestamp;

#[derive(Clone, PartialEq, Message)]
pub struct Event {
    #[prost(uint64, tag = "1")]
    pub event_id: u64,
    #[prost(int32, tag = "2")]
    pub thread_id: i32,
    #[prost(message, optional, tag = "3")]
    pub timestamp: Option<Timestamp>,
    #[prost(oneof = "event::Payload", tags = "10, 11, 12, 13, 14")]
    pub payload: Option<event::Payload>,
}

pub mod event {
    use super::*;

    #[derive(Clone, PartialEq, Oneof)]
    pub enum Payload {
        #[prost(message, tag = "10")]
        TraceStart(super::TraceStart),
        #[prost(message, tag = "11")]
        TraceEnd(super::TraceEnd),
        #[prost(message, tag = "12")]
        FunctionCall(super::FunctionCall),
        #[prost(message, tag = "13")]
        FunctionReturn(super::FunctionReturn),
        #[prost(message, tag = "14")]
        SignalDelivery(super::SignalDelivery),
    }
}

#[derive(Clone, PartialEq, Message)]
pub struct TraceStart {
    #[prost(string, tag = "1")]
    pub executable_path: String,
    #[prost(string, repeated, tag = "2")]
    pub args: Vec<String>,
    #[prost(string, tag = "3")]
    pub operating_system: String,
    #[prost(string, tag = "4")]
    pub cpu_architecture: String,
}

#[derive(Clone, PartialEq, Message)]
pub struct TraceEnd {
    #[prost(int32, tag = "1")]
    pub exit_code: i32,
}

#[derive(Clone, PartialEq, Message)]
pub struct FunctionCall {
    #[prost(string, tag = "1")]
    pub symbol: String,
    #[prost(uint64, tag = "2")]
    pub address: u64,
    #[prost(map = "string, uint64", tag = "3")]
    pub argument_registers: ::std::collections::HashMap<String, u64>,
    #[prost(bytes, tag = "4")]
    pub stack_shallow_copy: Vec<u8>,
}

#[derive(Clone, PartialEq, Message)]
pub struct FunctionReturn {
    #[prost(string, tag = "1")]
    pub symbol: String,
    #[prost(uint64, tag = "2")]
    pub address: u64,
    #[prost(map = "string, uint64", tag = "3")]
    pub return_registers: ::std::collections::HashMap<String, u64>,
}

#[derive(Clone, PartialEq, Message)]
pub struct SignalDelivery {
    #[prost(int32, tag = "1")]
    pub number: i32,
    #[prost(string, tag = "2")]
    pub name: String,
    #[prost(map = "string, uint64", tag = "3")]
    pub registers: ::std::collections::HashMap<String, u64>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ParsedEvent {
    pub timestamp_ns: u64,
    pub thread_id: u32,
    pub kind: ParsedEventKind,
}

#[derive(Debug, Clone, PartialEq)]
pub enum ParsedEventKind {
    TraceStart,
    TraceEnd,
    FunctionCall { symbol: Option<String> },
    FunctionReturn { symbol: Option<String> },
    SignalDelivery { name: Option<String> },
    Unknown,
}

impl ParsedEventKind {
    pub fn as_str(&self) -> &'static str {
        match self {
            ParsedEventKind::TraceStart => "TraceStart",
            ParsedEventKind::TraceEnd => "TraceEnd",
            ParsedEventKind::FunctionCall { .. } => "FunctionCall",
            ParsedEventKind::FunctionReturn { .. } => "FunctionReturn",
            ParsedEventKind::SignalDelivery { .. } => "SignalDelivery",
            ParsedEventKind::Unknown => "Unknown",
        }
    }

    pub fn function_symbol(&self) -> Option<&str> {
        match self {
            ParsedEventKind::FunctionCall { symbol }
            | ParsedEventKind::FunctionReturn { symbol } => symbol.as_deref(),
            _ => None,
        }
    }
}

impl ParsedEvent {
    pub fn from_proto(event: Event) -> Self {
        let timestamp_ns = event.timestamp.map(timestamp_to_ns).unwrap_or_default();

        let thread_id = u32::try_from(event.thread_id).unwrap_or_default();
        let kind = match event.payload {
            Some(event::Payload::TraceStart(_)) => ParsedEventKind::TraceStart,
            Some(event::Payload::TraceEnd(_)) => ParsedEventKind::TraceEnd,
            Some(event::Payload::FunctionCall(call)) => ParsedEventKind::FunctionCall {
                symbol: some_non_empty(call.symbol),
            },
            Some(event::Payload::FunctionReturn(ret)) => ParsedEventKind::FunctionReturn {
                symbol: some_non_empty(ret.symbol),
            },
            Some(event::Payload::SignalDelivery(sig)) => ParsedEventKind::SignalDelivery {
                name: some_non_empty(sig.name),
            },
            None => ParsedEventKind::Unknown,
        };

        ParsedEvent {
            timestamp_ns,
            thread_id,
            kind,
        }
    }

    pub fn function_name(&self) -> Option<&str> {
        self.kind.function_symbol()
    }
}

fn timestamp_to_ns(ts: Timestamp) -> u64 {
    const NANOS_PER_SEC: u64 = 1_000_000_000;
    let seconds = ts.seconds.max(0) as u64;
    let nanos = ts.nanos.max(0) as u32;
    seconds
        .saturating_mul(NANOS_PER_SEC)
        .saturating_add(nanos as u64)
}

fn some_non_empty(value: String) -> Option<String> {
    if value.trim().is_empty() {
        None
    } else {
        Some(value)
    }
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;

    fn event_with_payload(payload: event::Payload) -> Event {
        Event {
            event_id: 1,
            thread_id: 7,
            timestamp: Some(Timestamp {
                seconds: 0,
                nanos: 500,
            }),
            payload: Some(payload),
        }
    }

    #[test]
    fn parsed_event__trace_start_payload__then_kind_trace_start() {
        let event = event_with_payload(event::Payload::TraceStart(TraceStart {
            executable_path: "/bin/app".into(),
            args: vec!["--flag".into()],
            operating_system: "linux".into(),
            cpu_architecture: "x86_64".into(),
        }));

        let parsed = ParsedEvent::from_proto(event);
        assert_eq!(parsed.kind.as_str(), "TraceStart");
        assert_eq!(parsed.thread_id, 7);
        assert_eq!(parsed.timestamp_ns, 500);
        assert!(parsed.function_name().is_none());
    }

    #[test]
    fn parsed_event__trace_end_payload__then_kind_trace_end() {
        let event = event_with_payload(event::Payload::TraceEnd(TraceEnd { exit_code: 0 }));
        let parsed = ParsedEvent::from_proto(event);
        assert_eq!(parsed.kind.as_str(), "TraceEnd");
        assert!(parsed.function_name().is_none());
    }

    #[test]
    fn parsed_event__function_call_with_symbol__then_returns_symbol() {
        let event = event_with_payload(event::Payload::FunctionCall(FunctionCall {
            symbol: "foo".into(),
            address: 0,
            argument_registers: Default::default(),
            stack_shallow_copy: Vec::new(),
        }));
        let parsed = ParsedEvent::from_proto(event);
        assert_eq!(parsed.kind.as_str(), "FunctionCall");
        assert_eq!(parsed.function_name(), Some("foo"));
    }

    #[test]
    fn parsed_event__function_return_with_whitespace_symbol__then_returns_none() {
        let event = event_with_payload(event::Payload::FunctionReturn(FunctionReturn {
            symbol: "   ".into(),
            address: 0,
            return_registers: Default::default(),
        }));
        let parsed = ParsedEvent::from_proto(event);
        assert_eq!(parsed.kind.as_str(), "FunctionReturn");
        assert!(parsed.function_name().is_none());
    }

    #[test]
    fn parsed_event__signal_delivery_empty_name__then_name_is_none() {
        let event = event_with_payload(event::Payload::SignalDelivery(SignalDelivery {
            number: 9,
            name: "".into(),
            registers: Default::default(),
        }));

        let parsed = ParsedEvent::from_proto(event);
        assert_eq!(parsed.kind.as_str(), "SignalDelivery");
        match parsed.kind {
            ParsedEventKind::SignalDelivery { ref name } => assert!(name.is_none()),
            _ => panic!("expected signal delivery"),
        }
    }

    #[test]
    fn parsed_event__missing_payload__then_kind_unknown() {
        let parsed = ParsedEvent::from_proto(Event {
            event_id: 42,
            thread_id: -1,
            timestamp: Some(Timestamp {
                seconds: -2,
                nanos: -100,
            }),
            payload: None,
        });

        assert_eq!(parsed.kind.as_str(), "Unknown");
        assert_eq!(parsed.thread_id, 0);
        assert_eq!(parsed.timestamp_ns, 0);
    }

    #[test]
    fn parsed_event_kind__function_symbol_non_function__then_none() {
        let kind = ParsedEventKind::TraceStart;
        assert!(kind.function_symbol().is_none());
    }
}
