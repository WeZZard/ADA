use std::{net::SocketAddr, time::Duration};

use hyper::{body, client::Client, Body, Request, StatusCode};
use query_engine::server::{JsonRpcError, JsonRpcResponse, JsonRpcServer, JsonRpcServerConfig};
use serde_json::{json, Value};
use tokio::{sync::oneshot, time::sleep};

async fn spawn_server(server: JsonRpcServer) -> (SocketAddr, oneshot::Sender<()>) {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").expect("bind listener");
    let addr = listener.local_addr().expect("listener addr");
    let (tx, rx) = oneshot::channel();

    tokio::spawn(async move {
        let shutdown = async {
            let _ = rx.await;
        };
        if let Err(err) = server.serve_on_listener(listener, shutdown).await {
            panic!("server terminated unexpectedly: {err}");
        }
    });

    (addr, tx)
}

async fn send_raw(addr: SocketAddr, payload: &str) -> (StatusCode, Vec<u8>) {
    let client = Client::new();
    let uri: hyper::Uri = format!("http://{addr}/rpc").parse().expect("uri parse");
    let request = Request::post(uri)
        .header(hyper::header::CONTENT_TYPE, "application/json")
        .body(Body::from(payload.to_string()))
        .expect("build request");

    let response = client.request(request).await.expect("http request");
    let status = response.status();
    let body_bytes = body::to_bytes(response.into_body())
        .await
        .expect("body bytes")
        .to_vec();
    (status, body_bytes)
}

async fn send_json(addr: SocketAddr, payload: Value) -> JsonRpcResponseEnvelope {
    let (status, body) = send_raw(addr, &payload.to_string()).await;
    JsonRpcResponseEnvelope { status, body }
}

struct JsonRpcResponseEnvelope {
    status: StatusCode,
    body: Vec<u8>,
}

impl JsonRpcResponseEnvelope {
    fn into_response(self) -> JsonRpcResponsePayload {
        let response: JsonRpcResponse = serde_json::from_slice(&self.body).expect("json decode");
        JsonRpcResponsePayload {
            status: self.status,
            response,
        }
    }
}

struct JsonRpcResponsePayload {
    status: StatusCode,
    response: query_engine::server::JsonRpcResponse,
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn json_rpc_invalid_json_returns_parse_error() {
    let server = JsonRpcServer::new();
    let (addr, shutdown) = spawn_server(server).await;

    let (status, body) = send_raw(addr, "not-json").await;
    assert_eq!(status, StatusCode::OK);

    let response: JsonRpcResponse = serde_json::from_slice(&body).expect("response json");
    let error = response.error.expect("expected error");
    assert_eq!(error.code, -32700);

    let _ = shutdown.send(());
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn json_rpc_method_dispatch_returns_result() {
    let server = JsonRpcServer::new();
    server.register_sync("echo", |params| {
        params.ok_or_else(|| JsonRpcError::invalid_params("missing params"))
    });

    let (addr, shutdown) = spawn_server(server).await;

    let payload = json!({
        "jsonrpc": "2.0",
        "method": "echo",
        "params": {"value": 42},
        "id": 1
    });

    let response = send_json(addr, payload).await.into_response();
    assert_eq!(response.status, StatusCode::OK);
    let result = response.response.result.expect("result present");
    assert_eq!(result["value"], 42);

    let _ = shutdown.send(());
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn json_rpc_unknown_method_returns_error() {
    let server = JsonRpcServer::new();
    let (addr, shutdown) = spawn_server(server).await;

    let payload = json!({
        "jsonrpc": "2.0",
        "method": "missing",
        "id": 2
    });

    let response = send_json(addr, payload).await.into_response();
    let error = response.response.error.expect("error present");
    assert_eq!(error.code, -32601);

    let _ = shutdown.send(());
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn connection_limit_exceeded_returns_error() {
    let config = JsonRpcServerConfig {
        max_requests_per_second: 100,
        max_concurrent_per_ip: 1,
        max_total_concurrent: 1,
    };
    let server = JsonRpcServer::with_config(config);
    server.register_async("slow", |_params| async move {
        sleep(Duration::from_millis(120)).await;
        Ok(Value::Null)
    });

    let (addr, shutdown) = spawn_server(server).await;

    let first_payload = json!({
        "jsonrpc": "2.0",
        "method": "slow",
        "id": 10
    });

    let second_payload = json!({
        "jsonrpc": "2.0",
        "method": "slow",
        "id": 11
    });

    let first = tokio::spawn(send_json(addr, first_payload));
    sleep(Duration::from_millis(10)).await;
    let second_response = send_json(addr, second_payload).await.into_response();

    let first_response = first.await.expect("join first").into_response();
    assert!(first_response.response.error.is_none());
    let error = second_response.response.error.expect("error expected");
    assert_eq!(error.code, -32002);

    let _ = shutdown.send(());
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn rate_limiter_blocks_excess_requests() {
    let config = JsonRpcServerConfig {
        max_requests_per_second: 1,
        max_concurrent_per_ip: 10,
        max_total_concurrent: 10,
    };
    let server = JsonRpcServer::with_config(config);
    server.register_sync("noop", |_params| Ok(Value::Null));

    let (addr, shutdown) = spawn_server(server).await;

    let payload = json!({
        "jsonrpc": "2.0",
        "method": "noop",
        "id": 21
    });

    let first = send_json(addr, payload.clone()).await.into_response();
    assert!(first.response.error.is_none());

    let second = send_json(addr, payload).await.into_response();
    let error = second.response.error.expect("rate limit error");
    assert_eq!(error.code, -32001);

    let _ = shutdown.send(());
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn handles_concurrent_requests_successfully() {
    let server = JsonRpcServer::new();
    server.register_async("sum", |params| async move {
        let params = params.ok_or_else(|| JsonRpcError::invalid_params("missing params"))?;
        let numbers = params
            .as_array()
            .ok_or_else(|| JsonRpcError::invalid_params("expected array"))?;
        let sum = numbers
            .iter()
            .map(|value| {
                value
                    .as_i64()
                    .ok_or_else(|| JsonRpcError::invalid_params("expected integers"))
            })
            .collect::<Result<Vec<_>, _>>()?
            .into_iter()
            .sum::<i64>();
        Ok(Value::Number(sum.into()))
    });

    let (addr, shutdown) = spawn_server(server).await;

    let mut handles = Vec::new();
    for i in 0..8 {
        let payload = json!({
            "jsonrpc": "2.0",
            "method": "sum",
            "params": [i, i + 1],
            "id": i,
        });
        handles.push(tokio::spawn(send_json(addr, payload)));
    }

    for handle in handles {
        let response = handle.await.expect("join task").into_response();
        assert!(response.response.error.is_none());
        let result = response.response.result.expect("result");
        let expected = result.as_i64().expect("integer result");
        assert!(expected > 0);
    }

    let _ = shutdown.send(());
}
