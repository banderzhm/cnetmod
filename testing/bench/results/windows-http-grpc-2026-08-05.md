# Windows Release HTTP / gRPC benchmark — 2026-08-05

- Host: Intel Core i9-14900K, local loopback
- Build: Visual Studio 2026 / MSVC Release, IOCP, bundled BoringSSL
- Server/client workers: `16 / 16`
- TLS certificate: local test certificate supplied through `CNETMOD_BENCH_TLS_DIR`
- Each entry below is a successful three-run mean.

| Workload | Per-run success | Run 1 | Run 2 | Run 3 | Mean |
|---|---:|---:|---:|---:|---:|
| HTTP/1.1 cleartext | 16,000 / 16,000 | 195.74K req/s | 475.83K req/s | 344.40K req/s | **338.66K req/s** |
| HTTP/2 h2c | 16,000 / 16,000 | 233.73K req/s | 280.17K req/s | 251.23K req/s | **255.04K req/s** |
| HTTPS/1.1 | 16,000 / 16,000 | 269.29K req/s | 329.84K req/s | 319.70K req/s | **306.28K req/s** |
| HTTPS/2 | 16,000 / 16,000 | 220.32K req/s | 241.59K req/s | 244.57K req/s | **235.49K req/s** |
| WS echo | 16,000 / 16,000 | 387.03K msg/s | 537.26K msg/s | 445.48K msg/s | **456.59K msg/s** |
| WSS echo | 16,000 / 16,000 | 390.56K msg/s | 398.67K msg/s | 412.86K msg/s | **400.70K msg/s** |
| gRPC unary over h2c | 80,000 / 80,000 | 220.97K req/s | 224.53K req/s | 217.01K req/s | **220.84K req/s** |

HTTP requests use `GET /hello` with the exact `Hello, World!` 13-byte body and
keep-alive enabled. HTTP/2 uses one in-flight stream per connection. WebSocket
uses a 16-byte text message and validates every echoed payload. gRPC uses the
16-byte unary echo handler over persistent h2c connections. These conditions
are functional checks, not merely transport-open checks.
