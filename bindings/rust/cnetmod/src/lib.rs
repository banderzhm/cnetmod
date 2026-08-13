//! Safe, caller-driven Rust bindings for the cnetmod C ABI.
//!
//! Drive [`Runtime`] from the thread which created it. Completion callbacks
//! run on that same thread; [`Request::cancel`] is safe from another thread.

use std::ffi::{CStr, CString, NulError};
use std::marker::PhantomData;
use std::os::raw::{c_char, c_int};
use std::ptr::NonNull;
use std::sync::Arc;

#[repr(C)]
struct RawRuntime {
    _private: [u8; 0],
}
#[repr(C)]
struct RawClient {
    _private: [u8; 0],
}
#[repr(C)]
struct RawRequest {
    _private: [u8; 0],
}
#[repr(C)]
struct RawResponse {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Method {
    Get,
    Post,
    Put,
    Delete,
    Patch,
    Head,
    Options,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VersionPreference {
    Http1Only,
    Http2Only,
    Http2Preferred,
    Http1Preferred,
    Http3Only,
    Http3Preferred,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct RawClientOptions {
    struct_size: u32,
    connect_timeout_ms: u32,
    request_timeout_ms: u32,
    max_redirects: u32,
    follow_redirects: u8,
    verify_peer: u8,
    keep_alive: u8,
    enable_cookies: u8,
    version_preference: VersionPreference,
    user_agent: *const c_char,
}

type Completion =
    unsafe extern "C" fn(*mut core::ffi::c_void, *mut RawResponse, c_int, *const c_char);

unsafe extern "C" {
    fn cnetmod_runtime_create() -> *mut RawRuntime;
    fn cnetmod_runtime_destroy(runtime: *mut RawRuntime);
    fn cnetmod_runtime_poll(runtime: *mut RawRuntime) -> usize;
    fn cnetmod_runtime_run_one(runtime: *mut RawRuntime) -> usize;
    fn cnetmod_runtime_stop(runtime: *mut RawRuntime);
    fn cnetmod_runtime_restart(runtime: *mut RawRuntime);
    fn cnetmod_http_client_create(
        runtime: *mut RawRuntime,
        options: *const RawClientOptions,
    ) -> *mut RawClient;
    fn cnetmod_http_client_destroy(client: *mut RawClient);
    fn cnetmod_http_request_start(
        client: *mut RawClient,
        method: Method,
        url: *const c_char,
        body: *const u8,
        body_size: usize,
        completion: Option<Completion>,
        user_data: *mut core::ffi::c_void,
    ) -> *mut RawRequest;
    fn cnetmod_http_request_cancel(request: *mut RawRequest);
    fn cnetmod_http_request_destroy(request: *mut RawRequest);
    fn cnetmod_http_response_status(response: *const RawResponse) -> c_int;
    fn cnetmod_http_response_body(response: *const RawResponse, body_size: *mut usize)
        -> *const u8;
    fn cnetmod_http_response_free(response: *mut RawResponse);
}

/// HTTP client configuration. Use [`Default`] unless an explicit protocol or
/// timeout policy is needed.
#[derive(Clone, Debug)]
pub struct ClientOptions {
    pub connect_timeout_ms: u32,
    pub request_timeout_ms: u32,
    pub max_redirects: u32,
    pub follow_redirects: bool,
    pub verify_peer: bool,
    pub keep_alive: bool,
    pub enable_cookies: bool,
    pub version_preference: VersionPreference,
    pub user_agent: Option<String>,
}

impl Default for ClientOptions {
    fn default() -> Self {
        Self {
            connect_timeout_ms: 5_000,
            request_timeout_ms: 30_000,
            max_redirects: 10,
            follow_redirects: true,
            verify_peer: true,
            keep_alive: true,
            enable_cookies: true,
            version_preference: VersionPreference::Http2Preferred,
            user_agent: None,
        }
    }
}

#[derive(Debug)]
pub enum CreateError {
    NativeAllocationFailed,
    InteriorNul(NulError),
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct Error {
    pub code: i32,
    pub message: String,
}

/// Owns a cnetmod I/O runtime. This type is intentionally !Send/!Sync:
/// ordinary calls must stay on its owner thread, matching cnetmod's executor.
pub struct Runtime {
    inner: Arc<RuntimeHandle>,
}

struct RuntimeHandle {
    raw: NonNull<RawRuntime>,
    _thread_affine: PhantomData<*mut ()>,
}

impl Runtime {
    pub fn new() -> Result<Self, CreateError> {
        let raw = NonNull::new(unsafe { cnetmod_runtime_create() })
            .ok_or(CreateError::NativeAllocationFailed)?;
        Ok(Self {
            inner: Arc::new(RuntimeHandle {
                raw,
                _thread_affine: PhantomData,
            }),
        })
    }

    /// Process ready work without blocking.
    pub fn poll(&mut self) -> usize {
        unsafe { cnetmod_runtime_poll(self.inner.raw.as_ptr()) }
    }

    /// Wait for and process one I/O completion or posted task.
    pub fn run_one(&mut self) -> usize {
        unsafe { cnetmod_runtime_run_one(self.inner.raw.as_ptr()) }
    }

    pub fn stop(&mut self) {
        unsafe { cnetmod_runtime_stop(self.inner.raw.as_ptr()) }
    }

    pub fn restart(&mut self) {
        unsafe { cnetmod_runtime_restart(self.inner.raw.as_ptr()) }
    }

    /// Create a client sharing this runtime. The client is deliberately not a
    /// Rust borrow of `self`, so the same owner thread can call `run_one()`
    /// while requests are in flight. The native runtime remains alive until
    /// the last client is dropped.
    pub fn http_client(&self, options: ClientOptions) -> Result<HttpClient, CreateError> {
        let user_agent = match options.user_agent {
            Some(value) => Some(CString::new(value).map_err(CreateError::InteriorNul)?),
            None => None,
        };
        let raw_options = RawClientOptions {
            struct_size: core::mem::size_of::<RawClientOptions>() as u32,
            connect_timeout_ms: options.connect_timeout_ms,
            request_timeout_ms: options.request_timeout_ms,
            max_redirects: options.max_redirects,
            follow_redirects: options.follow_redirects as u8,
            verify_peer: options.verify_peer as u8,
            keep_alive: options.keep_alive as u8,
            enable_cookies: options.enable_cookies as u8,
            version_preference: options.version_preference,
            user_agent: user_agent
                .as_ref()
                .map_or(core::ptr::null(), |value| value.as_ptr()),
        };
        let raw = NonNull::new(unsafe {
            cnetmod_http_client_create(self.inner.raw.as_ptr(), &raw_options)
        })
        .ok_or(CreateError::NativeAllocationFailed)?;
        Ok(HttpClient {
            raw,
            _runtime: Arc::clone(&self.inner),
            _thread_affine: PhantomData,
        })
    }
}

impl Drop for RuntimeHandle {
    fn drop(&mut self) {
        unsafe { cnetmod_runtime_destroy(self.raw.as_ptr()) }
    }
}

pub struct HttpClient {
    raw: NonNull<RawClient>,
    _runtime: Arc<RuntimeHandle>,
    _thread_affine: PhantomData<*mut ()>,
}

struct CallbackState<F: FnOnce(Result<Response, Error>)> {
    callback: Option<F>,
}

unsafe extern "C" fn complete<F>(
    raw: *mut core::ffi::c_void,
    response: *mut RawResponse,
    error_code: c_int,
    error_message: *const c_char,
) where
    F: FnOnce(Result<Response, Error>),
{
    let mut state = unsafe { Box::from_raw(raw.cast::<CallbackState<F>>()) };
    let callback = state
        .callback
        .take()
        .expect("cnetmod completion called once");
    // A Rust panic must never unwind into the C++ coroutine frame.
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if let Some(response) = NonNull::new(response) {
            callback(Ok(Response { raw: response }));
        } else {
            let message = if error_message.is_null() {
                String::new()
            } else {
                unsafe { CStr::from_ptr(error_message) }
                    .to_string_lossy()
                    .into_owned()
            };
            callback(Err(Error {
                code: error_code,
                message,
            }));
        }
    }));
}

impl HttpClient {
    pub fn start<F>(
        &mut self,
        method: Method,
        url: &str,
        body: &[u8],
        callback: F,
    ) -> Result<Request, CreateError>
    where
        F: FnOnce(Result<Response, Error>) + 'static,
    {
        let url = CString::new(url).map_err(CreateError::InteriorNul)?;
        let state = Box::new(CallbackState {
            callback: Some(callback),
        });
        let raw_state = Box::into_raw(state);
        let raw = NonNull::new(unsafe {
            cnetmod_http_request_start(
                self.raw.as_ptr(),
                method,
                url.as_ptr(),
                if body.is_empty() {
                    core::ptr::null()
                } else {
                    body.as_ptr()
                },
                body.len(),
                Some(complete::<F>),
                raw_state.cast(),
            )
        });
        match raw {
            Some(raw) => Ok(Request { raw }),
            None => {
                drop(unsafe { Box::from_raw(raw_state) });
                Err(CreateError::NativeAllocationFailed)
            }
        }
    }
}

impl Drop for HttpClient {
    fn drop(&mut self) {
        unsafe { cnetmod_http_client_destroy(self.raw.as_ptr()) }
    }
}

/// A started native request. It may be dropped before completion; cnetmod
/// retains the callback state until the completion path releases it.
pub struct Request {
    raw: NonNull<RawRequest>,
}

impl Request {
    /// Thread-safe; the native cancel token is forwarded to in-flight I/O.
    pub fn cancel(&self) {
        unsafe { cnetmod_http_request_cancel(self.raw.as_ptr()) }
    }
}

impl Drop for Request {
    fn drop(&mut self) {
        unsafe { cnetmod_http_request_destroy(self.raw.as_ptr()) }
    }
}

pub struct Response {
    raw: NonNull<RawResponse>,
}

impl Response {
    pub fn status(&self) -> i32 {
        unsafe { cnetmod_http_response_status(self.raw.as_ptr()) }
    }

    pub fn body(&self) -> &[u8] {
        let mut length = 0;
        let bytes = unsafe { cnetmod_http_response_body(self.raw.as_ptr(), &mut length) };
        if bytes.is_null() {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(bytes, length) }
        }
    }
}

impl Drop for Response {
    fn drop(&mut self) {
        unsafe { cnetmod_http_response_free(self.raw.as_ptr()) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use std::thread;

    #[test]
    fn drives_a_real_loopback_http_request() {
        let listener = TcpListener::bind("127.0.0.1:0").expect("bind loopback server");
        let address = listener.local_addr().expect("read loopback address");
        let server = thread::spawn(move || {
            let (mut connection, _) = listener.accept().expect("accept cnetmod client");
            let mut request = [0_u8; 1024];
            let _ = connection.read(&mut request).expect("read request");
            connection
                .write_all(
                    b"HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\nrust-e2e",
                )
                .expect("write response");
        });

        let mut runtime = Runtime::new().expect("create runtime");
        let options = ClientOptions {
            version_preference: VersionPreference::Http1Only,
            ..ClientOptions::default()
        };
        let mut client = runtime.http_client(options).expect("create HTTP client");
        let completed = Arc::new(AtomicBool::new(false));
        let succeeded = Arc::new(AtomicBool::new(false));
        let completed_callback = Arc::clone(&completed);
        let succeeded_callback = Arc::clone(&succeeded);
        let url = format!("http://{address}/rust");
        let request = client
            .start(Method::Get, &url, b"", move |result| {
                let is_expected_response = result.is_ok_and(|response| {
                    response.status() == 200 && response.body() == b"rust-e2e"
                });
                succeeded_callback.store(is_expected_response, Ordering::Release);
                completed_callback.store(true, Ordering::Release);
            })
            .expect("start HTTP request");

        while !completed.load(Ordering::Acquire) {
            runtime.run_one();
            if !completed.load(Ordering::Acquire) {
                runtime.restart();
            }
        }

        assert!(succeeded.load(Ordering::Acquire));
        drop(request);
        drop(client);
        server.join().expect("join loopback server");
    }
}
