use std::{env, net::SocketAddr, sync::Arc, time::Duration};

use bytes::Buf;
use http::Request;
use tokio::task::JoinSet;
use tokio_rustls::rustls;
use tokio_rustls::rustls::pki_types::{CertificateDer, ServerName, UnixTime};

#[derive(Debug)]
struct SkipServerVerification(Arc<rustls::crypto::CryptoProvider>);

impl SkipServerVerification {
    fn new() -> Arc<Self> {
        Arc::new(Self(
            Arc::new(rustls::crypto::aws_lc_rs::default_provider()),
        ))
    }
}

impl rustls::client::danger::ServerCertVerifier for SkipServerVerification {
    fn verify_server_cert(
        &self,
        _end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _server_name: &ServerName<'_>,
        _ocsp: &[u8],
        _now: UnixTime,
    ) -> Result<rustls::client::danger::ServerCertVerified, rustls::Error> {
        Ok(rustls::client::danger::ServerCertVerified::assertion())
    }

    fn verify_tls12_signature(
        &self,
        message: &[u8],
        certificate: &CertificateDer<'_>,
        signature: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls12_signature(
            message,
            certificate,
            signature,
            &self.0.signature_verification_algorithms,
        )
    }

    fn verify_tls13_signature(
        &self,
        message: &[u8],
        certificate: &CertificateDer<'_>,
        signature: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls13_signature(
            message,
            certificate,
            signature,
            &self.0.signature_verification_algorithms,
        )
    }

    fn supported_verify_schemes(&self) -> Vec<rustls::SignatureScheme> {
        self.0.signature_verification_algorithms.supported_schemes()
    }
}

#[tokio::main]
async fn main() {
    env_logger::init();
    let _ = rustls::crypto::aws_lc_rs::default_provider().install_default();

    let mut arguments = env::args().skip(1);
    let address: SocketAddr = arguments.next().unwrap().parse().unwrap();
    let _certificate = arguments.next().unwrap();
    let requests: usize = arguments.next().unwrap().parse().unwrap();
    let parallel: usize = arguments.next().unwrap().parse().unwrap();

    let mut tls = rustls::ClientConfig::builder()
        .dangerous()
        .with_custom_certificate_verifier(SkipServerVerification::new())
        .with_no_client_auth();
    tls.alpn_protocols = vec![b"h3".to_vec()];
    let quic = quinn::crypto::rustls::QuicClientConfig::try_from(tls).unwrap();
    let mut endpoint = quinn::Endpoint::client(SocketAddr::from(([0, 0, 0, 0], 0))).unwrap();
    endpoint.set_default_client_config(quinn::ClientConfig::new(Arc::new(quic)));

    let connection = endpoint
        .connect(address, "localhost")
        .unwrap()
        .await
        .unwrap();
    let (mut driver, sender) = h3::client::new(h3_quinn::Connection::new(connection))
        .await
        .unwrap();
    let driver_task = tokio::spawn(async move {
        let error = driver.wait_idle().await;
        eprintln!("HTTP/3 driver closed: {error:?}");
    });

    let uri = format!("https://localhost:{}/hello", address.port());
    let mut tasks = JoinSet::new();
    let mut completed = 0usize;
    let mut failed = 0usize;

    for sequence in 0..requests {
        while tasks.len() >= parallel {
            let result = tokio::time::timeout(Duration::from_secs(10), tasks.join_next())
                .await
                .expect("request batch timed out")
                .expect("request task set ended")
                .expect("request task panicked");
            completed += 1;
            if let Err(error) = result {
                failed += 1;
                eprintln!("request failed after {completed} completions: {error}");
            }
        }

        let mut request_sender = sender.clone();
        let request_uri = uri.clone();
        tasks.spawn(async move {
            let request = Request::builder()
                .method("GET")
                .uri(request_uri)
                .body(())
                .map_err(|error| format!("request {sequence} build: {error}"))?;
            let mut stream = request_sender
                .send_request(request)
                .await
                .map_err(|error| format!("request {sequence} open: {error:?}"))?;
            stream
                .finish()
                .await
                .map_err(|error| format!("request {sequence} finish: {error:?}"))?;
            let response = stream
                .recv_response()
                .await
                .map_err(|error| format!("request {sequence} response: {error:?}"))?;
            if response.status() != 200 {
                return Err(format!("request {sequence} status: {}", response.status()));
            }
            let mut body_size = 0usize;
            while let Some(data) = stream
                .recv_data()
                .await
                .map_err(|error| format!("request {sequence} body: {error:?}"))?
            {
                body_size += data.remaining();
            }
            if body_size != 13 {
                return Err(format!("request {sequence} body size: {body_size}"));
            }
            Ok::<(), String>(())
        });
    }

    while let Some(joined) = tokio::time::timeout(Duration::from_secs(10), tasks.join_next())
        .await
        .expect("final request batch timed out")
    {
        let result = joined.expect("request task panicked");
        completed += 1;
        if let Err(error) = result {
            failed += 1;
            eprintln!("request failed after {completed} completions: {error}");
        }
    }

    println!("completed={completed} failed={failed}");
    drop(sender);
    endpoint.close(0u32.into(), b"diagnostic complete");
    let _ = tokio::time::timeout(Duration::from_secs(1), driver_task).await;
    if failed != 0 || completed != requests {
        std::process::exit(1);
    }
}
