use serde::{Deserialize, Serialize};
use serde_json::Value;

/// JSON-RPC 2.0 request as defined by the specification.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct JsonRpcRequest {
    pub jsonrpc: String,
    pub method: String,
    #[serde(default)]
    pub params: Option<Value>,
    #[serde(default)]
    pub id: Option<Value>,
}

impl JsonRpcRequest {
    pub fn validate(&self) -> Result<(), JsonRpcError> {
        if self.jsonrpc != "2.0" {
            return Err(JsonRpcError::invalid_request("jsonrpc field must be '2.0'"));
        }
        if self.method.trim().is_empty() {
            return Err(JsonRpcError::invalid_request("method must not be empty"));
        }
        Ok(())
    }
}

/// JSON-RPC 2.0 response as defined by the specification.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct JsonRpcResponse {
    pub jsonrpc: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    #[serde(default)]
    pub result: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    #[serde(default)]
    pub error: Option<JsonRpcError>,
    #[serde(default)]
    pub id: Option<Value>,
}

impl JsonRpcResponse {
    pub fn success(id: Option<Value>, result: Value) -> Self {
        Self {
            jsonrpc: "2.0".to_string(),
            result: Some(result),
            error: None,
            id,
        }
    }

    pub fn error(id: Option<Value>, error: JsonRpcError) -> Self {
        Self {
            jsonrpc: "2.0".to_string(),
            result: None,
            error: Some(error),
            id,
        }
    }
}

/// JSON-RPC 2.0 error payload.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct JsonRpcError {
    pub code: i32,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    #[serde(default)]
    pub data: Option<Value>,
}

impl JsonRpcError {
    pub fn new(code: i32, message: impl Into<String>, data: Option<Value>) -> Self {
        Self {
            code,
            message: message.into(),
            data,
        }
    }

    pub fn parse_error(details: impl Into<String>) -> Self {
        Self::new(-32700, "Parse error", Some(Value::String(details.into())))
    }

    pub fn invalid_request(details: impl Into<String>) -> Self {
        Self::new(
            -32600,
            "Invalid request",
            Some(Value::String(details.into())),
        )
    }

    pub fn method_not_found(method: &str) -> Self {
        Self::new(
            -32601,
            "Method not found",
            Some(Value::String(method.to_string())),
        )
    }

    pub fn invalid_params(details: impl Into<String>) -> Self {
        Self::new(
            -32602,
            "Invalid params",
            Some(Value::String(details.into())),
        )
    }

    pub fn internal(details: impl Into<String>) -> Self {
        Self::new(
            -32603,
            "Internal error",
            Some(Value::String(details.into())),
        )
    }

    pub fn trace_not_found() -> Self {
        Self::new(-32000, "Trace not found", None)
    }

    pub fn rate_limited() -> Self {
        Self::new(-32001, "Too many requests", None)
    }

    pub fn too_many_connections() -> Self {
        Self::new(-32002, "Too many concurrent connections", None)
    }
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;
    use serde_json::{json, Value};

    #[test]
    fn json_rpc_types__validate_with_valid_request__then_ok() {
        let request = JsonRpcRequest {
            jsonrpc: "2.0".to_string(),
            method: "trace.info".to_string(),
            params: Some(json!({"traceId": "abc"})),
            id: Some(json!(1)),
        };

        assert!(request.validate().is_ok());
    }

    #[test]
    fn json_rpc_types__validate_with_invalid_version__then_error() {
        let request = JsonRpcRequest {
            jsonrpc: "1.0".to_string(),
            method: "trace.info".to_string(),
            params: None,
            id: None,
        };

        let err = request.validate().expect_err("expected validation error");
        assert_eq!(err.code, -32600);
        assert_eq!(err.message, "Invalid request");
        assert_eq!(
            err.data,
            Some(Value::String("jsonrpc field must be '2.0'".into()))
        );
    }

    #[test]
    fn json_rpc_types__validate_with_empty_method__then_error() {
        let request = JsonRpcRequest {
            jsonrpc: "2.0".to_string(),
            method: "   ".to_string(),
            params: None,
            id: None,
        };

        let err = request.validate().expect_err("expected validation error");
        assert_eq!(err.code, -32600);
        assert_eq!(err.message, "Invalid request");
        assert_eq!(
            err.data,
            Some(Value::String("method must not be empty".into()))
        );
    }

    #[test]
    fn json_rpc_types__response_success_constructor__then_sets_result() {
        let response = JsonRpcResponse::success(Some(json!(1)), json!({"ok": true}));

        assert_eq!(response.jsonrpc, "2.0");
        assert_eq!(response.id, Some(json!(1)));
        assert_eq!(response.result, Some(json!({"ok": true})));
        assert!(response.error.is_none());
    }

    #[test]
    fn json_rpc_types__response_error_constructor__then_sets_error() {
        let error = JsonRpcError::internal("boom");
        let response = JsonRpcResponse::error(Some(json!(2)), error.clone());

        assert_eq!(response.jsonrpc, "2.0");
        assert_eq!(response.id, Some(json!(2)));
        assert_eq!(response.error, Some(error));
        assert!(response.result.is_none());
    }

    #[test]
    fn json_rpc_types__error_constructors__then_match_specs() {
        let parse = JsonRpcError::parse_error("bad");
        assert_eq!(parse.code, -32700);
        assert_eq!(parse.message, "Parse error");
        assert_eq!(parse.data, Some(Value::String("bad".into())));

        let invalid_request = JsonRpcError::invalid_request("missing field");
        assert_eq!(invalid_request.code, -32600);
        assert_eq!(invalid_request.message, "Invalid request");
        assert_eq!(
            invalid_request.data,
            Some(Value::String("missing field".into()))
        );

        let method_not_found = JsonRpcError::method_not_found("trace.info");
        assert_eq!(method_not_found.code, -32601);
        assert_eq!(method_not_found.message, "Method not found");
        assert_eq!(
            method_not_found.data,
            Some(Value::String("trace.info".into()))
        );

        let invalid_params = JsonRpcError::invalid_params("bad params");
        assert_eq!(invalid_params.code, -32602);
        assert_eq!(invalid_params.message, "Invalid params");
        assert_eq!(
            invalid_params.data,
            Some(Value::String("bad params".into()))
        );

        let internal = JsonRpcError::internal("panic");
        assert_eq!(internal.code, -32603);
        assert_eq!(internal.message, "Internal error");
        assert_eq!(internal.data, Some(Value::String("panic".into())));

        let trace_not_found = JsonRpcError::trace_not_found();
        assert_eq!(trace_not_found.code, -32000);
        assert_eq!(trace_not_found.message, "Trace not found");
        assert!(trace_not_found.data.is_none());

        let rate_limited = JsonRpcError::rate_limited();
        assert_eq!(rate_limited.code, -32001);
        assert_eq!(rate_limited.message, "Too many requests");
        assert!(rate_limited.data.is_none());

        let too_many_connections = JsonRpcError::too_many_connections();
        assert_eq!(too_many_connections.code, -32002);
        assert_eq!(
            too_many_connections.message,
            "Too many concurrent connections"
        );
        assert!(too_many_connections.data.is_none());
    }

    #[test]
    fn json_rpc_types__serde_parsing__then_behaves_as_expected() {
        let json = r#"{"jsonrpc":"2.0","method":"trace.info","params":{"k":"v"},"id":1}"#;
        let parsed: JsonRpcRequest = serde_json::from_str(json).expect("valid request");
        assert_eq!(parsed.jsonrpc, "2.0");
        assert_eq!(parsed.method, "trace.info");
        assert_eq!(parsed.params, Some(json!({"k": "v"})));
        assert_eq!(parsed.id, Some(json!(1)));

        let invalid = r#"{"method":"trace.info"}"#;
        let result: Result<JsonRpcRequest, _> = serde_json::from_str(invalid);
        assert!(result.is_err());

        let notification = r#"{"jsonrpc":"2.0","method":"trace.info"}"#;
        let parsed_notification: JsonRpcRequest =
            serde_json::from_str(notification).expect("valid notification");
        assert!(parsed_notification.id.is_none());
    }
}
