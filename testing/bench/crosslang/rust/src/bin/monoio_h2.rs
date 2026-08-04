use std::{env, error::Error, net::SocketAddr, thread};

use bytes::Bytes;
use monoio::{
    IoUringDriver, RuntimeBuilder,
    net::{TcpListener, TcpStream},
    time::TimeDriver,
};
use monoio_http::h2::{
    RecvStream,
    server::{self, SendResponse},
};
use socket2::{Domain, Protocol, Socket, Type};

fn options() -> (u16, usize) {
    let mut port = 18086;
    let mut workers = 16;
    let mut arguments = env::args().skip(1);
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--port" => port = arguments.next().unwrap().parse().unwrap(),
            "--workers" => workers = arguments.next().unwrap().parse().unwrap(),
            _ => panic!("unknown argument: {argument}"),
        }
    }
    (port, workers)
}

fn listener(port: u16) -> TcpListener {
    let socket = Socket::new(Domain::IPV4, Type::STREAM, Some(Protocol::TCP)).unwrap();
    socket.set_reuse_address(true).unwrap();
    socket.set_reuse_port(true).unwrap();
    socket
        .bind(&SocketAddr::from(([127, 0, 0, 1], port)).into())
        .unwrap();
    socket.listen(4096).unwrap();
    socket.set_nonblocking(true).unwrap();
    TcpListener::from_std(socket.into()).unwrap()
}

async fn serve(socket: TcpStream) -> Result<(), Box<dyn Error + Send + Sync>> {
    let mut connection = server::handshake(socket).await?;
    while let Some(result) = connection.accept().await {
        let (request, respond) = result?;
        monoio::spawn(handle_request(request, respond));
    }
    Ok(())
}

async fn handle_request(mut request: http::Request<RecvStream>, mut respond: SendResponse<Bytes>) {
    while let Some(data) = request.body_mut().data().await {
        match data {
            Ok(bytes) => {
                let _ = request
                    .body_mut()
                    .flow_control()
                    .release_capacity(bytes.len());
            }
            Err(_) => return,
        }
    }
    let response = http::Response::builder()
        .status(200)
        .header("content-type", "text/plain")
        .body(())
        .unwrap();
    let Ok(mut stream) = respond.send_response(response, false) else {
        return;
    };
    let _ = stream.send_data(Bytes::from_static(b"Hello, World!"), true);
}

fn run_worker(port: u16) {
    let builder: RuntimeBuilder<TimeDriver<IoUringDriver>> = RuntimeBuilder::new().enable_all();
    let mut runtime = builder.build().unwrap();
    runtime.block_on(async move {
        let listener = listener(port);
        loop {
            let Ok((socket, _)) = listener.accept().await else {
                continue;
            };
            monoio::spawn(async move {
                let _ = serve(socket).await;
            });
        }
    });
}

fn main() {
    let (port, workers) = options();
    println!("ready {port} h2c {workers}");
    let handles: Vec<_> = (0..workers)
        .map(|_| thread::spawn(move || run_worker(port)))
        .collect();
    for handle in handles {
        handle.join().unwrap();
    }
}
