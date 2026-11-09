# HttpArena 榜首源码对照

## 对照版本

| 框架 | 本地目录 | 提交 |
|---|---|---|
| kron | `E:\github\kron-reference` | `02f662f688aee3a73337c644e7a4841bf279d936` |
| zix | `E:\github\zix-reference` | `8b849c6a48c0867a2388ce2e92697d1a6760777b` |

这两个提交与当前 HttpArena 适配器锁定版本一致。对照只提取可迁移的热路径做法，不复制榜单专用接口。

## 源码对照

| 主题 | kron | zix | cnetmod 结论 |
|---|---|---|---|
| 事件循环 | 每核一个 reactor，并固定 CPU | 每 worker 一个 ring 和 fd 表 | cnetmod 已按事件循环分片；减少 worker 会降低 async 扩展性 |
| io_uring 队列 | 8192 项，`NO_SQARRAY`、`SINGLE_ISSUER`、`DEFER_TASKRUN` | SQ 4096、CQ 16384，最多批处理 512 | 单独扩大 ring 或打开 `NO_SQARRAY` 未通过 1% 门槛 |
| 接收 | 共享 provided-buffer ring，multishot accept/recv | HTTP 撤回 multishot recv，WebSocket 保留 | cnetmod 的 H1 multishot 有正收益；长期借用 provided buffer 会降低周转率 |
| 连接状态 | fd 直接索引，连接 slab | worker 本地 fd 表和 slab | 全局连接表分片没有稳定收益；连接本地可变状态仍是正确边界 |
| HTTP/1 解析 | reactor 内复用解析状态，手写同步热路径 | 按协议保留专用热路径 | cnetmod 的拥有型 parser 跨碎片输入更通用；不能用短生命周期视图替代所有存储 |
| 响应写入 | 复用连接 slab，累计后提交 | 小响应走 worker 本地存储 | cnetmod 已累计 pipelined 响应；额外 prepared cache 会引入共享引用计数 |
| 关闭连接 | 连接状态机统一回收 | 异步 close | cnetmod 改成异步 close 只有约 0.1%，不保留额外状态 |
| 忙轮询 | 没有作为通用默认值 | `SO_BUSY_POLL=50` | 本机只提高约 0.4%，P99 变差，不应成为框架默认值 |

## 本轮逐项验证

所有吞吐对照都使用官方混合 GET、Content-Length POST 和 chunked POST。正式决策使用 Clang 23、Release、ThinLTO、mold 和 bundled mimalloc。

| 候选 | 4 worker | 8 worker | 16 worker | 决策 |
|---|---:|---:|---:|---|
| ring 深度 8192 | 约 -0.6% | 未继续 | 未继续 | 撤回 |
| `NO_SQARRAY` | 约 +0.7% | 未继续 | 未继续 | 未达到门槛 |
| 自适应 completion wait | 约 +2.7% | 约 -0.1% | 未继续 | 撤回 |
| `SO_BUSY_POLL=50` | 约 +0.4%，P99 变差 | 未继续 | 未继续 | 撤回 |
| 异步 close | 约 +0.1% | 未继续 | 未继续 | 撤回 |
| 8 KiB 线程本地缓冲池 | 约 +0.4%，P99 变差 | 未继续 | 未继续 | 撤回 |
| 连接表分片 | 接近持平 | 复测约 -2.0% | 首轮约 +1.3% | 撤回 |
| 常用响应头只用位图 | 诊断构建约 -3.3% | 约 +0.6% | 约 -5.0% | 撤回 |
| parser 字段改为 32 位 | 约 -2.3% | 约 -1.7% | 约 +0.7% | 撤回 |
| 借用 provided buffer 解析 H1 | 中位约 +2.8%，P99 变差 | 接近持平 | 约 -4.2% | 撤回 |
| 初始探测 string 移交 parser | 中位回退 | 中位约 -6.5% | 停止 | 撤回 |

## `std::string` 与缓冲池边界

请求和响应 parser 中的 string 不是同一种用途。

- 服务端接收数据优先来自 io_uring provided-buffer ring。
- 请求 parser 的 `buf_` 保存跨 recv 的碎片，并在连接内复用容量。
- 响应 parser 的 `buf_` 负责跨 recv 拼接；`status_msg_` 与 `body_` 还要向调用方提供稳定所有权。
- 只有数据不会跨 await，且消费方在缓冲块归还前结束时，才可以直接借用池化块。
- 需要跨接收、协议升级或 `take_*` 移交时，必须进入拥有型存储。

固定块池不能直接替换拥有型 string。强行替换会造成悬空视图，或者让内核缓冲长期不能归还。若以后引入 parser 专用 arena，它必须支持可增长连续存储，并用正式 ThinLTO 端到端对照证明收益。
