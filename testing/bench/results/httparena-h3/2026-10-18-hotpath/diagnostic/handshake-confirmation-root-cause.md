# HANDSHAKE_DONE 缺失与冷关闭失败

记录日期：2026-10-18。本文记录修复前的证据；不是修复后通过报告。

服务端没有发送 `HANDSHAKE_DONE`，使 ngtcp2 客户端始终没有确认握手。请求 FIN 的 ACK 丢失后，客户端不能依靠应用空间 PTO 恢复；即使响应内容及 FIN 已完整接收，h2load 仍等待双向流关闭。该链条解释了本次“服务端恢复队列为空、响应完整、2xx 多于 done”的停滞。ACK 载体为何丢失仍是独立问题，不能仅凭本次证据认定 ACK history 实现有误。

## 可复查的现场

外部证据根目录：`/var/tmp/cnetmod-h3-cold-qlog-0578225f3b154de899376961c761b54a`。

| 文件 | 作用 |
| --- | --- |
| `round-6/command.json` | 原始 h2load 命令：HTTP/3、200000 请求、64 连接、每连接并发64、64线程 |
| `round-6/h2load.stdout` | 197036 started、196972 done/succeeded、197013 2xx、3028 failed/errored、0 timeout |
| `round-6/qlog-summary.json` | 全部197013个实际上线请求均收到连续响应范围及 FIN；只验证传输范围，不验证 HTTP body 内容 |
| `round-6/client.5.0.sqlog` | 失败连接客户端原始 qlog，CID 包含 `15b7290e4d4f5dfe` |
| `round-6/cid-15b7290e4d4f5dfe-audit.json` | 双向 FIN、副本、ACK、末包及恢复指标交叉核对 |
| `server.log` | 同 CID 的冷关闭状态 |

其余63连接各完成3125请求；失败连接只有97 done，但收到138个完整响应。差额41正好是请求 FIN 所有发送副本均未被服务端 ACK 的41条流。该连接最高请求流ID为548，已发出138个请求；197036 started 也包含已交给客户端栈但尚未上线的请求，不能与197013条线上请求混为一谈。

失败连接最后收到的服务端 ACK 在10ms、服务端PN1177，只确认客户端PN31。客户端PN33–36为每包1444字节的 ACK/STREAM/PADDING，仍未获 ACK；末尾44ms指标 `bytes_in_flight=congestion_window=5776`、`pto_count=0`，最后客户端PN48仅含ACK。没有后续应用探测包。服务端最终按 idle 关闭，错误为 generic/110；该 CID 已退休138条双向流，所有发送队列、应用 ledger/detector/cc flight 均为零，流信用为200。

失败 qlog 与完整健康对照 `round-6/client.0.0.sqlog` 均无 `handshake_done` 帧。修复前整个 `src/protocol/quic` 仅有该帧的枚举、结构体、编码和解码，`quic_connection` 没有发送排队或接收状态处理引用。故正常连接也缺少确认，只是在 ACK 正常时仍能继续进展。

## 实际加载依赖

负载发生在本机 WSL `archlinux`。h2load 为 `/var/tmp/cnetmod-h3-loadgen169/h2load`，1.69.0 源码对应的独立客户端。`readelf -d` 显示它动态依赖 `libngtcp2.so.16`、`libnghttp3.so.9` 和 `libngtcp2_crypto_ossl.so.0`；RUNPATH 是 `/tmp/cnetmod-h3-loadgen169/lib`，核验时该目录不存在。用动态加载器 `--list`（只解析依赖，不运行客户端）确认当前解析到 `/usr/lib`，版本字符串分别为 ngtcp2 **1.25.0**、nghttp3 **1.18.0**，TLS 适配器为 OpenSSL。

| 核验对象 | SHA-256 |
| --- | --- |
| h2load | `9a35cefa52e809a32f1898d65575a2cbbf54555031604830b852e2f6b5a4484f` |
| `/usr/lib/libngtcp2.so.16` | `524535e51383d20818e3878e0c819cae6249662dcfc168c0bfbb30aa8ba4f2b3` |
| `/usr/lib/libnghttp3.so.9` | `7d826dfe4128cab08a32609c56d67c9ce1383295cbce7ac0fa50449704b7479a` |
| `/usr/lib/libngtcp2_crypto_ossl.so.0` | `867135c8d1f2787c1bd5eca09f204e086d6c2c1e3030613f1b5792d92034fb2f` |

这是事后当前环境的依赖核验，不是失败进程的 `/proc/PID/maps` 快照；后续复现须继续记录库路径、环境和散列，不能只记录 h2load 二进制。

## 官方实现形成的根因链

ngtcp2 1.25.0 的客户端只有收到 `HANDSHAKE_DONE` 才设置 `HANDSHAKE_CONFIRMED` 并重启恢复定时器。TLS 完成回调只为服务器额外设置该标志；客户端收到1RTT ACK只设置 `SERVER_ADDR_VERIFIED`。当 Initial/Handshake 无待探测包、握手未确认且服务器地址已验证时，恢复定时器忽略应用空间并取消；最早 PTO 计算也排除未确认的应用空间。这与现场未获 ACK 的请求、零 PTO 和持续停滞一致。普通流关闭还要求本端数据及 FIN 全部获 ACK。[ngtcp2 1.25.0：接收 DONE](https://github.com/ngtcp2/ngtcp2/blob/v1.25.0/lib/ngtcp2_conn.c#L8660)、[TLS 完成](https://github.com/ngtcp2/ngtcp2/blob/v1.25.0/lib/ngtcp2_conn.c#L10915)、[PTO 选择](https://github.com/ngtcp2/ngtcp2/blob/v1.25.0/lib/ngtcp2_conn.c#L13377)、[定时器取消条件](https://github.com/ngtcp2/ngtcp2/blob/v1.25.0/lib/ngtcp2_conn.c#L13417)、[流关闭条件](https://github.com/ngtcp2/ngtcp2/blob/v1.25.0/lib/ngtcp2_conn.c#L12965)。

h2load 的 HTTP/3 `end_stream` 只记录时间；`done` 在 stream_close 回调中增加。因此响应 FIN、状态码计数与请求完成数可以不同。已排除“拥塞阻塞返回零时漏更新 expiry”：`write_quic` 在判断零返回前先调用 `quic_restart_pkt_timer`，读回调也会唤醒写处理。[HTTP/3 回调](https://github.com/nghttp2/nghttp2/blob/v1.69.0/src/h2load_http3_session.cc#L141)、[计数](https://github.com/nghttp2/nghttp2/blob/v1.69.0/src/h2load.cc#L1186)、[写与定时器](https://github.com/nghttp2/nghttp2/blob/v1.69.0/src/h2load_quic.cc#L741)。

nghttp3 的 close_stream 入口转交删除及关闭回调；这里没有发现独立的“响应 FIN 已收到但计数丢失”缺陷。堵点在其上游 QUIC 双向关闭尚未成立。[nghttp3 1.18.0](https://github.com/ngtcp2/nghttp3/blob/v1.18.0/lib/nghttp3_conn.c)。

## 协议要求与修复验收

TLS 完成要求本端已发 Finished 并验证对方 Finished；服务器应立即发送 DONE。客户端可选择以1RTT ACK确认握手，但这只是可选行为，不能依赖 ngtcp2 采用。DONE 只允许服务器在握手完成后发送，且必须可靠重传直到获 ACK。[RFC 9001 §4.1.1–4.1.2](https://www.rfc-editor.org/rfc/rfc9001.html#section-4.1.2)、[RFC 9000 §19.20](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.20)、[§13.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.3)。

最小修复应在服务器完成握手时只排队一次，并经过既有1RTT发送、恢复、丢包和 PTO 路径。不能仅改客户端负载工具、延长 idle 或仅在响应中偶尔补发来规避。接收服务器侧的客户端 DONE 应作为协议错误处理。

定向回归需要真实丢弃首个携带 DONE 的服务器数据报，观察未收到客户端额外业务请求时仍重传 DONE；明确断言发生一次实际丢弃、客户端从未确认变为已确认、重传有新 PN、最终收到 ACK 后不再持续重发。再丢弃已确认后的一个请求数据报，验证客户端应用 PTO 恢复并完成请求，避免只证明握手成功。原二进制应在确认断言失败，新二进制应通过；执行者应保留原始计数、时间线与测试窗口。

已只读审核 `testing/http3/quic_handshake_confirmation.py` 候选（SHA-256 `22abad24f53d216602d585f056732d1d1c388ad354f4175c41538ddc2650b326`）：它使用独立解密上下文识别实际 DONE，不向真实客户端旁路投递被丢报文；首丢之后直到确认前，阻断全部客户端入站，隔离服务器自主恢复。第二阶段丢请求 STREAM，并断言新 PN、客户端 PTO 计数增长、精确响应内容及请求 FIN 获 ACK。保存 sender 引用以避免 aioquic 正常删除完成流造成假失败。当前代理跳过长包开头的数据报，因此不覆盖长短包合并数据报中的 DONE；也尚未严格断言 ACK 后停止重复 DONE。这些限制不能冒充已覆盖项。生产候选只读审核未发现阻断问题；构建、负例、正例与重复负载结果由执行者另行归档。

限制：缺失 DONE 是确定的协议缺陷，源码与 qlog 解释了本次停滞链条；修复后的 oldFAIL/newPASS、重复原负载及有损回归尚须独立执行。qlog 不包含进程内 confirmed 标志快照，也不证明所有网络丢包原因；无证据把本次现象归为响应 STREAM 尾部丢失或客户端定时器实现错误。
