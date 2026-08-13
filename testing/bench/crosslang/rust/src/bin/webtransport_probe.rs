use std::env;
use std::error::Error;
use std::time::Duration;

use wtransport::{ClientConfig, Endpoint};

const CHILD_PAYLOAD: &[u8] = b"strm";
const DATAGRAM_PAYLOAD: &[u8] = b"ping";

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let url = env::args()
        .nth(1)
        .unwrap_or_else(|| "https://127.0.0.1:4433/webtransport".to_owned());

    let config = ClientConfig::builder()
        .with_bind_default()
        // The interop fixture uses a generated local certificate.  Production
        // clients must keep certificate validation enabled.
        .with_no_cert_validation()
        .build();
    let endpoint = Endpoint::client(config)?;
    let connection = tokio::time::timeout(Duration::from_secs(10), endpoint.connect(&url))
        .await
        .map_err(|_| "WebTransport CONNECT timeout")??;

    let opening_bi = tokio::time::timeout(Duration::from_secs(5), connection.open_bi())
        .await
        .map_err(|_| "WebTransport bidirectional stream open timeout")??;
    let (mut writer, mut reader) = tokio::time::timeout(Duration::from_secs(5), opening_bi)
        .await
        .map_err(|_| "WebTransport bidirectional stream result timeout")??;
    writer.write_all(CHILD_PAYLOAD).await?;

    let opening_uni = tokio::time::timeout(Duration::from_secs(5), connection.open_uni())
        .await
        .map_err(|_| "WebTransport unidirectional stream open timeout")??;
    let mut uni_writer = tokio::time::timeout(Duration::from_secs(5), opening_uni)
        .await
        .map_err(|_| "WebTransport unidirectional stream result timeout")??;
    uni_writer.write_all(b"uni").await?;
    uni_writer.finish().await?;

    connection.send_datagram(DATAGRAM_PAYLOAD)?;

    let mut echoed_stream = [0_u8; CHILD_PAYLOAD.len()];
    tokio::time::timeout(
        Duration::from_secs(5),
        reader.read_exact(&mut echoed_stream),
    )
    .await
    .map_err(|_| "WebTransport child-stream echo timeout")??;
    if echoed_stream != *CHILD_PAYLOAD {
        return Err(format!("child-stream echo mismatch: {echoed_stream:?}").into());
    }

    let datagram = tokio::time::timeout(Duration::from_secs(5), connection.receive_datagram())
        .await
        .map_err(|_| "WebTransport Datagram echo timeout")??;
    if datagram.payload().as_ref() != DATAGRAM_PAYLOAD {
        return Err(format!("Datagram echo mismatch: {:?}", datagram.payload()).into());
    }

    // The fixture sends its Close Capsule only after this acknowledgement.
    // DATAGRAM delivery is not ordered against the CONNECT stream, so this
    // avoids treating a correct close as an early connection failure.
    connection.send_datagram(b"close-ack")?;

    println!("WebTransport wtransport -> cnetmod: CONNECT, child streams, Datagram: ok");
    Ok(())
}
