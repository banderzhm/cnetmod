# static-h3 失败与内存增长修复复测

本记录保留从错误方案到最终修复的四组官方串行复测。每组均先跑
`baseline-h3` 3 轮，再跑 `static-h3` 3 轮；不同组之间不合并选优，也不删除
失败轮。最终提交为 `0af7f2f14499972da79fc070d556acb5997f1c87`。

最终结论：baseline 与 static 共 6 轮均为 0 failed、0 errored、0 timeout、
无非 2xx。static 官方内存峰值为 1.0/1.1/1.1 GiB，第三轮不再像修复前一样
从 1.1 GiB 继续升到 1.3 GiB；额外进程 RSS 采样在第二轮后约 995 MiB，
第三轮后稳定在约 1062 MiB。10 轮低负载回归也在首轮后稳定于约
166–171 MiB。以上证明本次官方三轮与低负载十轮没有持续增长，不扩展为无限时长结论。

## 修复内容

- arena 启动时一次性加载 20 个静态文件及 Brotli 版本，去掉逐请求异步文件读取。
- HTTP/3 响应增加不可变 `shared_body`，预加载内容不再逐请求复制。
- DATA 帧直接追加到最终 HEADERS + DATA + FIN 缓冲，去掉中间大块 DATA 缓冲。
- arena 空闲窗口设为 1.5 秒，在官方 2 秒轮间冷却内回收 h2load 静默退出的连接。
- `http3_server::set_idle_timeout()` 成为启动前可配置的公开 API，并覆盖多 worker。

## 固定条件

HttpArena 固定在 `8977be2721a5aba7b320345cddfcd6f6b654e698`。h2load 为
nghttp2 1.69.0，二进制 SHA-256 为
`9a35cefa52e809a32f1898d65575a2cbbf54555031604830b852e2f6b5a4484f`。
同一台 i9-14900K、32 vCPU WSL Arch Linux；Clang 23.1.3，Release 构建，
BoringSSL 与 mimalloc 3.5.3。官方参数为 64 连接、每连接 64 流、64 客户端线程、
每轮 5 秒、3 轮、冷却 2 秒、服务端 32 workers；静态请求使用官方 20 文件 URI
和 `Accept-Encoding: br;q=1, gzip;q=0.8`。计时阶段没有并行压测或构建。

官方 RPS 仍按最高一轮的 `2xx / elapsed` 选择。原始 h2load headline、完整响应数、
状态码、CPU、内存和延迟全部保存在各阶段 `records/summary.json` 与原始日志中。
这是本地官方 profile 复现，不是排行榜认证。

## 四组结果

| 阶段 | static RPS 1/2/3 | failed 1/2/3 | 官方内存峰值 1/2/3 | 有效 |
|---|---|---|---|---|
| `b1635afd` 16 KiB 流式 body | 1,111 / 1,117 / 1,086 | 0 / 0 / 1 | 851 / 945 / 976 MiB | 否 |
| `050c0f40` 恢复单写快路径 | 10,168 / 7,640 / 7,662 | 0 / 0 / 0 | 1000 MiB / 1.1 / 1.2 GiB | 是 |
| `74d21f2c` 轮间空闲回收 | 15,113 / 15,056 / 5,111 | 0 / 0 / 0 | 994 MiB / 1.0 / 1.1 GiB | 是 |
| `0af7f2f1` 共享 body 与直接 DATA framing | 14,097 / 11,453 / 5,081 | 0 / 0 / 0 | 1.0 / 1.1 / 1.1 GiB | 是 |

`b1635afd` 将静态内容拆成流式发送，产生 HEADERS、多个 DATA 和单独 FIN，吞吐退化
且第三轮仍有失败，因此完整保留为反例。`050c0f40` 恢复单次写入后消除了失败，
但 8 秒空闲窗口让上一轮连接跨过冷却期。`74d21f2c` 将窗口缩短到 1.5 秒后，
内存增长明显收敛。最终组继续去掉请求级静态内容与中间 DATA 帧复制。

最终 baseline 三轮为 1,675,431 / 1,676,594 / 1,633,250 RPS，全部零失败，
官方峰值 800/847/854 MiB；官方最佳为 1,676,594 RPS，p99 6.32 ms。
最终 static 官方最佳为 14,097 RPS，p99 229.22 ms。第三轮 static 吞吐仍有明显
波动，因此不据此声称稳定吞吐提升；本组的通过条件是请求正确性与内存收敛。

## 回归验证

- Linux 与 Windows Release 均成功构建 `arena_h3_server`、`test_http3_frame`。
- 两端 `test_http3_frame` 通过，包含新增共享响应体契约测试。
- 两端 5 项 HTTP/3 回归全部通过：early data、QPACK、空闲连接清理、普通客户端
  端到端、动态 QPACK 端到端。
- 低负载 static 10 轮全部零失败，40 次原始/Brotli 精确字节校验全部通过。
- 文档生成检查、版本契约与 `git diff --check` 通过。

各阶段目录中的 `records/` 保存验证日志、正式日志、二进制哈希、镜像 ID、命令、
环境、数据集哈希及每秒 RSS；`official-selected/` 保存官方生成的结果 JSON。
`resource-summary.json` 汇总额外 RSS 采样，`manifest.json` 列出归档文件哈希。

