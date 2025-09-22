use std::{
    convert::Infallible,
    future::Future,
    net::{IpAddr, SocketAddr},
    sync::Arc,
};

use hyper::server::{conn::AddrIncoming, conn::AddrStream, Builder};
use hyper::{
    body,
    header::CONTENT_TYPE,
    http::StatusCode,
    service::{make_service_fn, service_fn},
    Body, Method, Request, Response,
};

use super::{
    connection::{ConnectionError, ConnectionManager, ConnectionManagerConfig},
    errors::{JsonRpcServerError, ServerError},
    handler::HandlerRegistry,
    rate_limit::RateLimiter,
    types::{JsonRpcError, JsonRpcRequest, JsonRpcResponse},
};

#[derive(Clone, Debug)]
pub struct JsonRpcServerConfig {
    pub max_requests_per_second: u32,
    pub max_concurrent_per_ip: usize,
    pub max_total_concurrent: usize,
}

impl Default for JsonRpcServerConfig {
    fn default() -> Self {
        Self {
            max_requests_per_second: 2_000,
            max_concurrent_per_ip: 2_000,
            max_total_concurrent: 20_000,
        }
    }
}

#[derive(Clone)]
pub struct JsonRpcServer {
    inner: Arc<JsonRpcServerInner>,
}

struct JsonRpcServerInner {
    config: JsonRpcServerConfig,
    handlers: HandlerRegistry,
    connections: ConnectionManager,
    rate_limiter: RateLimiter,
}

impl JsonRpcServer {
    pub fn new() -> Self {
        Self::with_config(JsonRpcServerConfig::default())
    }

    pub fn with_config(config: JsonRpcServerConfig) -> Self {
        let connection_config = ConnectionManagerConfig {
            max_total: config.max_total_concurrent,
            max_per_ip: config.max_concurrent_per_ip,
        };

        Self {
            inner: Arc::new(JsonRpcServerInner {
                handlers: HandlerRegistry::new(),
                connections: ConnectionManager::new(connection_config),
                rate_limiter: RateLimiter::new(config.max_requests_per_second),
                config,
            }),
        }
    }

    pub fn config(&self) -> &JsonRpcServerConfig {
        &self.inner.config
    }

    pub fn handler_registry(&self) -> HandlerRegistry {
        self.inner.handlers.clone()
    }

    pub fn register_async<F, Fut>(&self, method: impl Into<String>, func: F)
    where
        F: Fn(Option<serde_json::Value>) -> Fut + Send + Sync + 'static,
        Fut: Future<Output = Result<serde_json::Value, JsonRpcError>> + Send + 'static,
    {
        self.inner.handlers.register_async(method, func);
    }

    pub fn register_sync<F>(&self, method: impl Into<String>, func: F)
    where
        F: Fn(Option<serde_json::Value>) -> Result<serde_json::Value, JsonRpcError>
            + Send
            + Sync
            + 'static,
    {
        self.inner.handlers.register_sync(method, func);
    }

    pub async fn serve(&self, addr: SocketAddr) -> Result<(), ServerError> {
        self.serve_with_shutdown(addr, async { std::future::pending::<()>().await })
            .await
    }

    pub async fn serve_with_shutdown<F>(
        &self,
        addr: SocketAddr,
        shutdown: F,
    ) -> Result<(), ServerError>
    where
        F: Future<Output = ()> + Send + 'static,
    {
        let builder = hyper::Server::try_bind(&addr)?;
        self.serve_from_builder(builder, shutdown).await
    }

    pub async fn serve_on_listener<F>(
        &self,
        listener: std::net::TcpListener,
        shutdown: F,
    ) -> Result<(), ServerError>
    where
        F: Future<Output = ()> + Send + 'static,
    {
        listener.set_nonblocking(true)?;
        let builder = hyper::Server::from_tcp(listener)?;
        self.serve_from_builder(builder, shutdown).await
    }

    async fn serve_from_builder<F>(
        &self,
        builder: Builder<AddrIncoming>,
        shutdown: F,
    ) -> Result<(), ServerError>
    where
        F: Future<Output = ()> + Send + 'static,
    {
        let server = self.clone();
        let make_service = make_service_fn(move |conn: &AddrStream| {
            let remote_addr = conn.remote_addr();
            let server = server.clone();
            async move {
                Ok::<_, Infallible>(service_fn(move |req| {
                    let server = server.clone();
                    async move { server.handle_http_request(req, remote_addr).await }
                }))
            }
        });

        builder
            .serve(make_service)
            .with_graceful_shutdown(shutdown)
            .await?;
        Ok(())
    }

    async fn handle_http_request(
        &self,
        req: Request<Body>,
        remote_addr: SocketAddr,
    ) -> Result<Response<Body>, Infallible> {
        if req.method() != Method::POST || req.uri().path() != "/rpc" {
            return Ok(Response::builder()
                .status(StatusCode::NOT_FOUND)
                .body(Body::empty())
                .expect("building 404 response"));
        }

        let remote_ip = remote_addr.ip();

        if !self.inner.rate_limiter.allow(remote_ip) {
            let response = JsonRpcServerError::RateLimited.to_response(None);
            return Ok(json_response(response));
        }

        let guard = match self.inner.connections.acquire(remote_ip) {
            Ok(guard) => guard,
            Err(ConnectionError::GlobalLimit) | Err(ConnectionError::PerIpLimit(_)) => {
                let response = JsonRpcServerError::ConnectionLimit.to_response(None);
                return Ok(json_response(response));
            }
        };

        let outcome = self.handle_json_rpc(req, remote_ip).await;
        drop(guard);
        Ok(outcome)
    }

    async fn handle_json_rpc(&self, req: Request<Body>, remote_ip: IpAddr) -> Response<Body> {
        let bytes = match body::to_bytes(req.into_body()).await {
            Ok(b) => b,
            Err(err) => {
                let error = JsonRpcError::internal(format!("failed to read body: {err}"));
                return json_response(JsonRpcResponse::error(None, error));
            }
        };

        if bytes.is_empty() {
            return json_response(JsonRpcResponse::error(
                None,
                JsonRpcError::invalid_request("empty body"),
            ));
        }

        let value: serde_json::Value = match serde_json::from_slice(&bytes) {
            Ok(v) => v,
            Err(err) => {
                return json_response(JsonRpcResponse::error(
                    None,
                    JsonRpcError::parse_error(err.to_string()),
                ));
            }
        };

        if value.is_array() {
            return json_response(JsonRpcResponse::error(
                None,
                JsonRpcError::invalid_request("batch requests are not supported"),
            ));
        }

        let request: JsonRpcRequest = match serde_json::from_value(value) {
            Ok(r) => r,
            Err(err) => {
                return json_response(JsonRpcResponse::error(
                    None,
                    JsonRpcError::invalid_request(err.to_string()),
                ));
            }
        };

        if let Err(err) = request.validate() {
            let id = request.id.clone();
            return json_response(JsonRpcResponse::error(id, err));
        }

        self.dispatch_request(request, remote_ip).await
    }

    async fn dispatch_request(
        &self,
        request: JsonRpcRequest,
        _remote_ip: IpAddr,
    ) -> Response<Body> {
        let JsonRpcRequest {
            jsonrpc: _,
            method,
            params,
            id,
        } = request;

        if id.is_none() {
            let _ = self.inner.handlers.call(&method, params).await;
            return Response::builder()
                .status(StatusCode::NO_CONTENT)
                .body(Body::empty())
                .expect("building notification response");
        }

        let result = self.inner.handlers.call(&method, params).await;
        let response = match result {
            Ok(value) => JsonRpcResponse::success(id.clone(), value),
            Err(err) => JsonRpcResponse::error(id.clone(), err),
        };
        json_response(response)
    }
}

fn json_response(response: JsonRpcResponse) -> Response<Body> {
    let payload = serde_json::to_vec(&response).expect("serializing JSON-RPC response");
    Response::builder()
        .status(StatusCode::OK)
        .header(CONTENT_TYPE, "application/json")
        .body(Body::from(payload))
        .expect("building JSON response")
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;
    use hyper::body::to_bytes;
    use serde_json::{json, Value};
    use std::{
        net::{IpAddr, Ipv4Addr, SocketAddr, TcpListener},
        sync::{atomic::AtomicUsize, Arc},
        time::Duration,
    };
    use tokio::time::sleep;

    fn localhost() -> IpAddr {
        IpAddr::V4(Ipv4Addr::LOCALHOST)
    }

    fn remote_addr() -> SocketAddr {
        SocketAddr::new(localhost(), 8080)
    }

    fn test_config() -> JsonRpcServerConfig {
        JsonRpcServerConfig {
            max_requests_per_second: 0,
            max_concurrent_per_ip: 10,
            max_total_concurrent: 10,
        }
    }

    fn build_request(body: Body) -> Request<Body> {
        Request::builder()
            .method(Method::POST)
            .uri("/rpc")
            .header(CONTENT_TYPE, "application/json")
            .body(body)
            .expect("build request")
    }

    async fn parse_body(response: Response<Body>) -> Value {
        let bytes = to_bytes(response.into_body())
            .await
            .expect("expected body bytes");
        serde_json::from_slice(&bytes).expect("valid json")
    }

    #[test]
    fn json_rpc_server__config_getter__then_returns_config() {
        let config = JsonRpcServerConfig {
            max_requests_per_second: 42,
            max_concurrent_per_ip: 24,
            max_total_concurrent: 100,
        };
        let server = JsonRpcServer::with_config(config.clone());

        let retrieved = server.config();
        assert_eq!(retrieved.max_requests_per_second, 42);
        assert_eq!(retrieved.max_concurrent_per_ip, 24);
        assert_eq!(retrieved.max_total_concurrent, 100);
    }

    #[test]
    fn json_rpc_server__handler_registry_getter__then_returns_registry() {
        let server = JsonRpcServer::new();
        server.register_sync("test_method", |_| Ok(json!({})));

        let registry = server.handler_registry();
        assert!(registry.contains("test_method"));
    }

    // Note: serve() method uses pending::<()>().await which would run forever
    // Coverage for lines 101-104 is achieved through serve_with_shutdown tests

    #[tokio::test]
    async fn json_rpc_server__serve_with_shutdown__then_serves_until_shutdown() {
        use hyper::{Client, StatusCode};

        let server = JsonRpcServer::new();
        server.register_sync("test", |_| Ok(json!({"result": "ok"})));

        let listener = TcpListener::bind("127.0.0.1:0").expect("bind");
        let addr = listener.local_addr().expect("addr");

        // Start server with shutdown signal
        let (tx, rx) = tokio::sync::oneshot::channel();
        let handle = tokio::spawn(async move {
            let shutdown = async move {
                let _ = rx.await;
            };
            let _ = server.serve_on_listener(listener, shutdown).await;
        });

        // Wait for server to start
        sleep(Duration::from_millis(10)).await;

        // Make a request to ensure serve_on_listener and serve_from_builder are called
        let client = Client::new();
        let uri = format!("http://{}/rpc", addr)
            .parse::<hyper::Uri>()
            .expect("uri");
        let request = hyper::Request::post(uri)
            .header("content-type", "application/json")
            .body(hyper::Body::from(
                r#"{"jsonrpc":"2.0","method":"test","id":1}"#,
            ))
            .expect("request");

        let response = client.request(request).await.expect("response");
        assert_eq!(response.status(), StatusCode::OK);

        // Shutdown server
        let _ = tx.send(());
        sleep(Duration::from_millis(10)).await;
        assert!(handle.is_finished());
    }

    #[tokio::test]
    async fn json_rpc_server__serve_with_shutdown_via_addr__then_serves() {
        use hyper::{Client, StatusCode};

        let server = JsonRpcServer::new();
        server.register_sync("ping", |_| Ok(json!({"pong": true})));

        let listener = TcpListener::bind("127.0.0.1:0").expect("bind");
        let addr = listener.local_addr().expect("addr");
        drop(listener); // Close listener so we can bind to same port

        // Start server using serve_with_shutdown directly with address
        let (tx, rx) = tokio::sync::oneshot::channel();
        let handle = tokio::spawn(async move {
            let shutdown = async move {
                let _ = rx.await;
            };
            let _ = server.serve_with_shutdown(addr, shutdown).await;
        });

        // Wait for server to start
        sleep(Duration::from_millis(50)).await;

        // Make a request to trigger line 115
        let client = Client::new();
        let uri = format!("http://{}/rpc", addr)
            .parse::<hyper::Uri>()
            .expect("uri");
        let request = hyper::Request::post(uri)
            .header("content-type", "application/json")
            .body(hyper::Body::from(
                r#"{"jsonrpc":"2.0","method":"ping","id":2}"#,
            ))
            .expect("request");

        let response = client.request(request).await.expect("response");
        assert_eq!(response.status(), StatusCode::OK);

        // Shutdown
        let _ = tx.send(());
        sleep(Duration::from_millis(10)).await;
        assert!(handle.is_finished());
    }

    #[tokio::test]
    async fn json_rpc_server__non_post_request__then_returns_not_found() {
        let server = JsonRpcServer::with_config(test_config());
        let request = Request::builder()
            .method(Method::GET)
            .uri("/other")
            .body(Body::empty())
            .unwrap();

        let response = server
            .handle_http_request(request, remote_addr())
            .await
            .expect("http response");

        assert_eq!(response.status(), StatusCode::NOT_FOUND);
    }

    #[tokio::test]
    async fn json_rpc_server__rate_limit_exceeded__then_returns_error_payload() {
        let server = JsonRpcServer::with_config(JsonRpcServerConfig {
            max_requests_per_second: 1,
            max_concurrent_per_ip: 10,
            max_total_concurrent: 10,
        });
        let body = build_request(Body::from(
            r#"{"jsonrpc":"2.0","method":"trace.info","id":1}"#,
        ));

        let first = server
            .handle_http_request(body, remote_addr())
            .await
            .expect("first response");
        assert_eq!(first.status(), StatusCode::OK);

        let second_request = build_request(Body::from(
            r#"{"jsonrpc":"2.0","method":"trace.info","id":2}"#,
        ));
        let second = server
            .handle_http_request(second_request, remote_addr())
            .await
            .expect("second response");

        let payload = parse_body(second).await;
        assert_eq!(payload["error"]["code"], -32001);
        assert_eq!(payload["error"]["message"], "Too many requests");
    }

    #[tokio::test]
    async fn json_rpc_server__connection_limit_hit__then_returns_limit_error() {
        let server = JsonRpcServer::with_config(JsonRpcServerConfig {
            max_requests_per_second: 0,
            max_concurrent_per_ip: 1,
            max_total_concurrent: 1,
        });
        let ip = localhost();
        let guard = server
            .inner
            .connections
            .acquire(ip)
            .expect("pre-acquire should succeed");

        let response = server
            .handle_http_request(
                build_request(Body::from(
                    r#"{"jsonrpc":"2.0","method":"trace.info","id":1}"#,
                )),
                SocketAddr::new(ip, 8080),
            )
            .await
            .expect("http response");

        let payload = parse_body(response).await;
        assert_eq!(payload["error"]["code"], -32002);
        assert_eq!(
            payload["error"]["message"],
            "Too many concurrent connections"
        );
        drop(guard);
    }

    #[tokio::test]
    async fn json_rpc_server__empty_body__then_invalid_request_error() {
        let server = JsonRpcServer::with_config(test_config());
        let response = server
            .handle_http_request(build_request(Body::empty()), remote_addr())
            .await
            .expect("http response");

        let payload = parse_body(response).await;
        assert_eq!(payload["error"]["code"], -32600);
        assert_eq!(payload["error"]["message"], "Invalid request");
        assert_eq!(payload["error"]["data"], "empty body");
    }

    #[tokio::test]
    async fn json_rpc_server__invalid_json_body__then_parse_error() {
        let server = JsonRpcServer::with_config(test_config());
        let response = server
            .handle_http_request(build_request(Body::from("{")), remote_addr())
            .await
            .expect("http response");

        let payload = parse_body(response).await;
        assert_eq!(payload["error"]["code"], -32700);
        assert_eq!(payload["error"]["message"], "Parse error");
    }

    #[tokio::test]
    async fn json_rpc_server__batch_requests__then_invalid_request_error() {
        let server = JsonRpcServer::with_config(test_config());
        let response = server
            .handle_http_request(build_request(Body::from("[]")), remote_addr())
            .await
            .expect("http response");

        let payload = parse_body(response).await;
        assert_eq!(payload["error"]["code"], -32600);
        assert_eq!(payload["error"]["data"], "batch requests are not supported");
    }

    #[tokio::test]
    async fn json_rpc_server__invalid_request_shape__then_invalid_request_error() {
        let server = JsonRpcServer::with_config(test_config());
        let response = server
            .handle_http_request(
                build_request(Body::from(r#"{"jsonrpc":"2.0"}"#)),
                remote_addr(),
            )
            .await
            .expect("http response");

        let payload = parse_body(response).await;
        assert_eq!(payload["error"]["code"], -32600);
        assert!(payload["error"]["data"]
            .as_str()
            .unwrap()
            .contains("method"));
    }

    #[tokio::test]
    async fn json_rpc_server__validation_error__then_invalid_request() {
        let server = JsonRpcServer::with_config(test_config());
        let response = server
            .handle_http_request(
                build_request(Body::from(r#"{"jsonrpc":"2.0","method":" ","id":1}"#)),
                remote_addr(),
            )
            .await
            .expect("http response");

        let payload = parse_body(response).await;
        assert_eq!(payload["error"]["code"], -32600);
        assert_eq!(payload["error"]["data"], "method must not be empty");
    }

    #[tokio::test]
    async fn json_rpc_server__notification_request__then_returns_no_content() {
        let server = JsonRpcServer::with_config(test_config());
        let hits = Arc::new(AtomicUsize::new(0));
        let hits_clone = Arc::clone(&hits);
        server.register_sync("trace.notify", move |_| {
            hits_clone.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            Ok(json!(null))
        });

        let response = server
            .handle_http_request(
                build_request(Body::from(r#"{"jsonrpc":"2.0","method":"trace.notify"}"#)),
                remote_addr(),
            )
            .await
            .expect("http response");

        assert_eq!(response.status(), StatusCode::NO_CONTENT);
        assert_eq!(hits.load(std::sync::atomic::Ordering::SeqCst), 1);
    }

    #[tokio::test]
    async fn json_rpc_server__method_dispatch_success__then_returns_result() {
        let server = JsonRpcServer::with_config(test_config());
        server.register_sync("trace.echo", |params| {
            Ok(params.unwrap_or_else(|| json!({})))
        });

        let response = server
            .handle_http_request(
                build_request(Body::from(
                    r#"{"jsonrpc":"2.0","method":"trace.echo","params":{"value":42},"id":1}"#,
                )),
                remote_addr(),
            )
            .await
            .expect("http response");

        let payload = parse_body(response).await;
        assert_eq!(payload["result"], json!({"value": 42}));
        assert_eq!(payload["id"], 1);
    }

    #[tokio::test]
    async fn json_rpc_server__method_dispatch_error__then_returns_error() {
        let server = JsonRpcServer::with_config(test_config());
        server.register_async("trace.fail", |_params| async {
            Err(JsonRpcError::internal("boom"))
        });

        let response = server
            .handle_http_request(
                build_request(Body::from(
                    r#"{"jsonrpc":"2.0","method":"trace.fail","id":9}"#,
                )),
                remote_addr(),
            )
            .await
            .expect("http response");

        let payload = parse_body(response).await;
        assert_eq!(payload["error"]["code"], -32603);
        assert_eq!(payload["error"]["message"], "Internal error");
        assert_eq!(payload["id"], 9);
    }

    #[tokio::test]
    async fn json_rpc_server__unknown_method__then_returns_method_not_found() {
        let server = JsonRpcServer::with_config(test_config());
        let response = server
            .handle_http_request(
                build_request(Body::from(
                    r#"{"jsonrpc":"2.0","method":"trace.unknown","id":"abc"}"#,
                )),
                remote_addr(),
            )
            .await
            .expect("http response");

        let payload = parse_body(response).await;
        assert_eq!(payload["error"]["code"], -32601);
        assert_eq!(payload["error"]["data"], "trace.unknown");
    }

    #[tokio::test]
    async fn json_rpc_server__json_response_helper__then_sets_headers() {
        let response = json_response(JsonRpcResponse::success(None, json!({"ok": true})));
        assert_eq!(response.status(), StatusCode::OK);
        assert_eq!(
            response.headers().get(CONTENT_TYPE).unwrap(),
            "application/json"
        );
        let payload = parse_body(response).await;
        assert_eq!(payload["result"], json!({"ok": true}));
    }

    #[tokio::test]
    async fn json_rpc_server__body_read_failure__then_internal_error() {
        let server = JsonRpcServer::with_config(test_config());
        let (sender, body) = Body::channel();
        sender.abort();

        let response = server
            .handle_http_request(build_request(body), remote_addr())
            .await
            .expect("http response");

        let payload = parse_body(response).await;
        assert_eq!(payload["error"]["code"], -32603);
        assert_eq!(payload["error"]["message"], "Internal error");
        assert!(payload["error"]["data"]
            .as_str()
            .unwrap()
            .contains("failed to read body"));
    }

    #[tokio::test]
    async fn json_rpc_server__serve_with_shutdown__then_returns_ok() {
        let server = JsonRpcServer::with_config(test_config());
        let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), 0);
        server
            .serve_with_shutdown(addr, async {})
            .await
            .expect("server should bind and exit");
    }

    #[tokio::test]
    async fn json_rpc_server__serve_on_listener__then_returns_ok() {
        let server = JsonRpcServer::with_config(test_config());
        let listener = TcpListener::bind(SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), 0))
            .expect("listener should bind");
        server
            .serve_on_listener(listener, async {})
            .await
            .expect("serve_on_listener should exit");
    }

    #[tokio::test]
    async fn json_rpc_server__serve_future_can_be_aborted__then_does_not_panic() {
        let server = JsonRpcServer::with_config(test_config());
        let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), 0);
        let task = tokio::spawn({
            let server = server.clone();
            async move { server.serve(addr).await }
        });

        sleep(Duration::from_millis(50)).await;
        task.abort();
        assert!(task.await.unwrap_err().is_cancelled());
    }
}
