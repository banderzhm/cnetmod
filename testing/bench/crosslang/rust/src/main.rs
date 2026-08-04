use std::{convert::Infallible, env, fs::File, io::BufReader, net::SocketAddr, sync::Arc};

use bytes::Bytes;
use http_body_util::Full;
use hyper::{Request, Response, body::Incoming, service::service_fn};
use hyper_util::rt::{TokioExecutor, TokioIo};
use tokio::net::TcpListener;
use tokio_rustls::{TlsAcceptor, rustls};

#[derive(Clone, Copy)]
enum Mode {
    Http1,
    Http2,
    Http3,
}

struct Options {
    port: u16,
    mode: Mode,
    cert: Option<String>,
    key: Option<String>,
}

fn options() -> Options {
    let mut port = 18081;
    let mut mode = Mode::Http1;
    let mut cert = None;
    let mut key = None;
    let mut arguments = env::args().skip(1);
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--port" => port = arguments.next().unwrap().parse().unwrap(),
            "--http1" => mode = Mode::Http1,
            "--http2" => mode = Mode::Http2,
            "--http3" => mode = Mode::Http3,
            "--cert" => cert = arguments.next(),
            "--key" => key = arguments.next(),
            _ => panic!("unknown argument: {argument}"),
        }
    }
    Options {
        port,
        mode,
        cert,
        key,
    }
}

fn tls_config(options: &Options) -> rustls::ServerConfig {
    let mut cert_reader = BufReader::new(File::open(options.cert.as_ref().unwrap()).unwrap());
    let certs = rustls_pemfile::certs(&mut cert_reader)
        .collect::<Result<Vec<_>, _>>()
        .unwrap();
    let mut key_reader = BufReader::new(File::open(options.key.as_ref().unwrap()).unwrap());
    let key = rustls_pemfile::private_key(&mut key_reader)
        .unwrap()
        .unwrap();
    rustls::ServerConfig::builder()
        .with_no_client_auth()
        .with_single_cert(certs, key)
        .unwrap()
}

async fn response(_: Request<Incoming>) -> Result<Response<Full<Bytes>>, Infallible> {
    Ok(Response::new(Full::new(Bytes::from_static(
        b"Hello, World!",
    ))))
}

async fn serve_tcp(options: Options) {
    let listener = TcpListener::bind(("127.0.0.1", options.port))
        .await
        .unwrap();
    let tls = options.cert.as_ref().map(|_| {
        let mut config = tls_config(&options);
        config.alpn_protocols = match options.mode {
            Mode::Http2 => vec![b"h2".to_vec()],
            _ => vec![b"http/1.1".to_vec()],
        };
        TlsAcceptor::from(Arc::new(config))
    });
    println!("ready {}", options.port);
    loop {
        let (stream, _) = listener.accept().await.unwrap();
        let tls = tls.clone();
        let mode = options.mode;
        tokio::spawn(async move {
            if let Some(acceptor) = tls {
                let stream = acceptor.accept(stream).await.unwrap();
                serve_connection(TokioIo::new(stream), mode).await;
            } else {
                serve_connection(TokioIo::new(stream), mode).await;
            }
        });
    }
}

async fn serve_connection<I>(io: TokioIo<I>, mode: Mode)
where
    I: tokio::io::AsyncRead + tokio::io::AsyncWrite + Unpin + Send + 'static,
{
    match mode {
        Mode::Http1 => {
            let _ = hyper::server::conn::http1::Builder::new()
                .serve_connection(io, service_fn(response))
                .await;
        }
        Mode::Http2 => {
            let _ = hyper::server::conn::http2::Builder::new(TokioExecutor::new())
                .serve_connection(io, service_fn(response))
                .await;
        }
        Mode::Http3 => unreachable!(),
    }
}

async fn serve_http3(options: Options) {
    let mut config = tls_config(&options);
    config.alpn_protocols = vec![b"h3".to_vec()];
    let quic = quinn::ServerConfig::with_crypto(Arc::new(
        quinn::crypto::rustls::QuicServerConfig::try_from(config).unwrap(),
    ));
    let endpoint =
        quinn::Endpoint::server(quic, SocketAddr::from(([127, 0, 0, 1], options.port))).unwrap();
    println!("ready {}", options.port);
    while let Some(incoming) = endpoint.accept().await {
        tokio::spawn(async move {
            let connection = incoming.await.unwrap();
            let mut server = h3::server::Connection::new(h3_quinn::Connection::new(connection))
                .await
                .unwrap();
            while let Ok(Some(resolver)) = server.accept().await {
                tokio::spawn(async move {
                    let (request, mut stream) = resolver.resolve_request().await.unwrap();
                    let _ = request;
                    stream
                        .send_response(Response::builder().status(200).body(()).unwrap())
                        .await
                        .unwrap();
                    stream
                        .send_data(Bytes::from_static(b"Hello, World!"))
                        .await
                        .unwrap();
                    stream.finish().await.unwrap();
                });
            }
        });
    }
}

#[tokio::main]
async fn main() {
    let _ = rustls::crypto::aws_lc_rs::default_provider().install_default();
    let options = options();
    match options.mode {
        Mode::Http3 => serve_http3(options).await,
        _ => serve_tcp(options).await,
    }
}
