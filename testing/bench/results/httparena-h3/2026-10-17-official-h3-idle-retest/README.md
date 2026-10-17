# e20010d2 官方 HTTP/3 复测

用户重新交付共享测试令牌后，对提交
`e20010d20b2df1e3f0c17d82b1b66c5592a0ca5c` 完成官方 baseline-h3 和 static-h3
各 3 轮，严格串行，没有为改善分数重跑。6 轮结束后停止负载并释放令牌。
本次只记录复测结果，没有新的生产代码修改。

结论：baseline 三轮零失败；static 三轮共 120,128 个已完成请求，3 个失败，
未达到零失败要求。两项 profile 的三轮内存峰值仍上升，未证明内存平台期。
日志未出现此前的 `ERR_QPACK_DECOMPRESSION_FAILED`，但不能因此把剩余失败
归因于客户端或统计边界；仍需请求级证据。

## 条件与来源

沿用[前 27 轮](../2026-10-17-official-h3-retest/README.md)的工具、数据和统计口径。
HttpArena 固定 `8977be2721a5aba7b320345cddfcd6f6b654e698`，h2load 1.69.0
源码 `68cb6900fde14c77f0cd7add0e094a862960eb99`，后端 OpenSSL 3.6.4 /
ngtcp2 1.25.0 / nghttp3 1.18.0。58 个数据文件的哈希与前组一致。
只重新构造两个 cnetmod 镜像，其他框架没有在本组重新测量。

同一台 i9-14900K，32 vCPU WSL Arch Linux，内核
`6.18.33.2-microsoft-standard-WSL2`；Clang 23.1.3 / libc++ 22.1.8，Release
`-O3 -DNDEBUG -g -fno-omit-frame-pointer`，BoringSSL / mimalloc 3.5.3。
TLS 1.3、AES-128-GCM、X25519、ALPN h3。

官方参数：64 连接、每连接 64 流、64 客户端线程、每轮 `-D5s`、3 轮、冷却 2 秒，
不增加预热。客户端 CPU 16–31，服务端 32 workers；官方小主机 CPU 回退为
`--cpus=32`。静态使用官方 20 文件 URI 列表和
`Accept-Encoding: br;q=1, gzip;q=0.8`。官方脚本负责负载、计时、资源采样和选优；
评分仍是三轮最高的 `2xx / elapsed`，完整响应吞吐量单独记录。

与前组相同，使用 `SKIP_TUNE=true`，清理范围和就绪/校验端口的审计补丁保存在
`records/official-driver.patch`，独立挂载命名空间让原始 localhost URI 优先 IPv4。
原生 h2load 后端与官方 Dockerfile 的 quictls 后端有差别。本记录是本地官方
profile 复现，不是排行榜认证。计时期间没有其他压测或构建。

正式镜像的二进制哈希已在启动前与 e20010d2 提交中的低负载回归记录核对。
最新空闲回收修复此前通过 Linux 9 项、Windows 5 项回归：3 个静默退出客户端
在约 30 秒后被清理，活动连接数从 4 回到 1。这证明特定回收路径修正有效，
并不等价于本次高负载进程 RSS 已经稳定。

## 全部轮次

| profile | 轮次 | 官方 RPS | 完整响应/s | 已完成请求 | failed | CPU | 官方内存峰值 | p99 |
|---|---:|---:|---:|---:|---:|---:|---|---|
| baseline-h3 | 1 | 1,573,045 | 1,573,041 | 8,934,873 | 0 | 1562.9% | 790 MiB | 7.22 ms |
| baseline-h3 | 2 | 1,510,440 | 1,510,438 | 8,579,289 | 0 | 1598.3% | 856 MiB | 7.45 ms |
| baseline-h3 | 3 | 1,557,222 | 1,557,222 | 8,845,025 | 0 | 1648.1% | 944 MiB | 7.36 ms |
| static-h3 | 1 | 4,991 | 4,849 | 27,885 | 3 | 2213.1% | 975 MiB | 198.17 ms |
| static-h3 | 2 | 9,216 | 9,076 | 52,551 | 0 | 2666.5% | 1.1 GiB | 221.51 ms |
| static-h3 | 3 | 6,846 | 6,682 | 39,692 | 0 | 2880.9% | 1.3 GiB | 244.48 ms |

所有轮次的 errored、timeout 和非 2xx 计数均为零；static 第一轮的 3 个 failed
仍按失败保留。每轮另有 4096 个 started 尚未完成，不能当作成功请求。
官方最佳轮为 baseline 第 1 轮和 static 第 2 轮；没有合并此前轮次重新选优。
static 最佳轮的官方 RPS 为 9,216，完整响应吞吐量为 9,076/s。

原始日志、官方选中 JSON、完整计数与分位数分别保存在 `records/`、
`official-selected/` 和 `records/summary.json`。额外 `/proc` RSS 记录在
`records/resource-samples.jsonl` 和 `resource-summary.json`，采样间隔为名义
1 秒加 Docker 查询开销，口径与官方 Docker 内存峰值不同。

本组不能给出持续运行内存平台期、长期稳定性、CPU 节省或吞吐提升结论。
后续需要关联失败请求的状态/关闭事件、活动连接数和分配内存，避免仅凭 RSS
峰值或较好的一轮分数推断原因。
