# HTTP/3 性能基准

`h3_benchmark` 使用 cnetmod 的公开 HTTP/3 客户端，通过真实 UDP、QUIC、
TLS 1.3、QPACK 和 HTTP/3 请求流测试一个正在运行的 HTTP/3 服务端。它不是
编解码器微基准，也不会生成全零占位结果：连接、预热或正式请求失败都会使
进程返回非零状态，并记录在 JSON 结果中。

## 构建

使用 Release 配置并启用 QUIC、TLS 和 benchmark：

```sh
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCNETMOD_ENABLE_QUIC=ON \
  -DCNETMOD_BUILD_BENCH=ON
cmake --build build-release --target h3_interop_server h3_benchmark --parallel 1
```

## 启动测试服务端

`h3_interop_server` 从当前目录读取 `cert.pem` 和 `key.pem`：

```sh
mkdir -p build-release/h3-bench-runtime
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -subj /CN=localhost \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' \
  -keyout build-release/h3-bench-runtime/key.pem \
  -out build-release/h3-bench-runtime/cert.pem

cd build-release/h3-bench-runtime
../bin/h3_interop_server --port 45443 --workers 16
```

## 运行

在另一个终端运行：

```sh
build-release/testing/bench/h3_benchmark \
  --host 127.0.0.1 \
  --port 45443 \
  --path /health \
  --connections 256 \
  --client-workers 16 \
  --concurrency 2 \
  --requests 1000 \
  --warmup 25 \
  --runs 5 \
  --timeout 30000 \
  --output build-release/h3-linux-release.json
```

参数含义：

- `--connections`：每轮建立的持久 QUIC 连接数。
- `--client-workers`：承载客户端连接的 `io_context` 工作线程数。
- `--concurrency`：每条 QUIC 连接上的最大并发请求流数。
- `--requests`：每条连接每轮正式计量的请求数，不包含预热。
- `--warmup`：每条连接建立后、正式计时前发送的请求数。
- `--runs`：使用新连接重复测试的轮数。
- `--timeout`：连接和请求超时，单位为毫秒。
- `--output`：包含环境、配置、每轮成功率、QPS、P50/P95/P99、CPU、RSS
  和错误信息的 JSON 文件。

## README 基线

2026-08-04 的本地基线环境：Intel Core i9-14900K、Arch Linux/WSL2、
Clang 22.1.8、Release、io_uring、本地回环、16 个服务端 worker、16 个客户端
worker、256 条持久 QUIC 连接、每连接并发 2、预热 25 次并计量 1,000 次，
共 5 轮。

| 指标 | 结果 |
|------|------|
| 成功率 | 5 × 256000/256000，0 失败 |
| 平均 QPS | 约 123.22K req/s |
| 单轮 QPS 范围 | 117.96K–131.19K req/s |
| 平均 P50 | 3.259 ms |
| 平均 P99 | 7.216 ms |

`/health` 的响应体只有三个字节，因此该场景用于衡量请求率和延迟，不把
响应体 MiB/s 解读为链路带宽。跨机器、弱网和大对象吞吐应使用独立场景，
并同时记录硬件、操作系统、编译器、I/O 后端、并发度、负载大小和失败率。
该基线表示多连接、多核服务端的聚合容量。单连接、单 `io_context`
的协议热路径诊断基线约为 21.77K req/s，不能与多核 HTTP/1.1 总容量直接
比较。固定并发 worker 会持续补充请求，因此延迟数据包含完整并发排队时间。
