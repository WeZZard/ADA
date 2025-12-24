pub mod app;
pub mod atf;
// TODO: Update handlers to use ATF V2 API
// pub mod handlers;
pub mod server;

/// Simple ping function for testing
pub fn ping() -> &'static str {
    "pong"
}

#[cfg(feature = "python")]
use pyo3::prelude::*;

#[cfg(feature = "python")]
use pyo3::exceptions::{PyRuntimeError, PyValueError};

#[cfg(feature = "python")]
use std::net::SocketAddr;

#[cfg(feature = "python")]
use crate::server::JsonRpcServer;

#[cfg(feature = "python")]
#[pyfunction]
fn ping_py() -> &'static str {
    ping()
}

#[cfg(feature = "python")]
#[pyfunction]
fn start_json_rpc_server(host: String, port: u16) -> PyResult<()> {
    let addr: SocketAddr = format!("{host}:{port}")
        .parse()
        .map_err(|err| PyValueError::new_err(format!("invalid address: {err}")))?;

    let server = JsonRpcServer::new();
    server.register_sync("ping", |_params| {
        Ok(serde_json::Value::String("pong".to_string()))
    });

    let runtime = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .map_err(|err| PyRuntimeError::new_err(format!("failed to create tokio runtime: {err}")))?;

    let server_handle = server.clone();
    std::thread::spawn(move || {
        runtime.block_on(async move {
            if let Err(err) = server_handle.serve(addr).await {
                eprintln!("JSON-RPC server terminated: {err}");
            }
        });
    });

    Ok(())
}

/// Python module definition
#[cfg(feature = "python")]
#[pymodule]
fn query_engine(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(ping_py, m)?)?;
    m.add_function(wrap_pyfunction!(start_json_rpc_server, m)?)?;
    m.add("__version__", "0.1.0")?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ping() {
        assert_eq!(ping(), "pong");
    }
}
