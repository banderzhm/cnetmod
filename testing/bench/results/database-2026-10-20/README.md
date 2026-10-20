# PostgreSQL / MySQL / Redis 深度优化验证（2026-10-20）

三个连接池已优化，协议侧完成 PostgreSQL 预读、MySQL 小包合并和 Redis 整批解析。
最终同机矩阵共 72 个有效结果，12 个场景中 11 个的三轮吞吐中位数提升超过 10%。
Redis 64 命令 Pipeline 吞吐没有提升，保留其负结果；进程 CPU/计时批次数下降约 21%。
这不是所有负载都提升 10% 的声明。

| 数据库 / 场景 | 容量 | 对照 ops/s | 优化 ops/s | 中位数变化 |
|---|---:|---:|---:|---:|
| PostgreSQL 纯借还 | 1 | 2,989,938 | 9,591,259 | +220.79% |
| PostgreSQL 纯借还 | 64 | 2,167,549 | 9,638,656 | +344.68% |
| MySQL 纯借还，无 reset | 1 | 3,312,548 | 10,833,973 | +227.06% |
| MySQL 纯借还，无 reset | 64 | 3,296,504 | 10,818,077 | +228.17% |
| Redis 纯借还 | 1 | 2,984,008 | 7,958,007 | +166.69% |
| Redis 纯借还 | 64 | 2,913,771 | 7,974,156 | +173.67% |
| PostgreSQL SELECT 42 | 1 | 8,015 | 9,012 | +12.43% |
| PostgreSQL 256 行结果 | 1 | 2,854 | 5,682 | +99.11% |
| MySQL SELECT 42 | 1 | 22.738 | 7,656.619 | +33,573.23%（336.73 倍） |
| MySQL 默认归还，要求 reset | 1 | 23.070 | 5,333.579 | +23,019.11% |
| Redis 单 ECHO | 1 | 7,816 | 9,050 | +15.78% |
| Redis 64 ECHO Pipeline | 1 | 8,836 | 8,738 | **−1.10%** |

原始三轮、CPU 数据和未四舍五入数值见 [summary.json](summary.json)、
[全部 72 轮](measured/rounds.json) 和 [完整日志](measured/)。
每个格子都是最终同一组二进制的三轮中位数，没有混入早期矩阵峰值。
gRPC 的独立四项最快对手 +10% 验收见 [gRPC 报告](../grpc-official-2026-10-20/README.md)。

## 源码与构建

- 性能实现：`21e84d9f796080d6c0ffba222c2f4f47ece722de`。
- 匹配对照：`301fb8275d562c19598f791847650bc80ab651e6`，分支
  `codex/database-control-20261020`，仅用于测量，**不要合并**。
- 完整回归及修复后的测试驱动：`3f39afb58f99109ae4c6063f53ae84e10c313a72`。
- 再次 fetch 的最新 develop：`80e40a01c2092e12c5b851a6ef87676e854a6540`，已包含在分支中。

对照使用与候选相同的基准代码、工具链、参数和已有 gRPC/core 改动，只不包含本次
数据库运行时优化。对照可以从本分支可达的 `b5f2b8bc` 应用
[control-fixture.patch](control-fixture.patch) 重建，不依赖发布测量分支。
[源码核对](source-parity.json) 按 CRLF→LF 规范化后，两个版本各 939 个运行时文件
全部匹配对应 Git 提交；基准源文件 SHA256 也相同。

同一 i9-14900K / 32 逻辑 CPU WSL2 主机，Clang 23、libc++、io_uring、native CPU、
Release O3、frame pointer、mimalloc 开启，ThinLTO 关闭。客户端固定逻辑 CPU 0；
PostgreSQL / MySQL / Redis 容器分别固定 16–19 / 20–23 / 24–27，预算各 4 CPU、
1536 MiB。每次测量三个服务都在场，镜像按不可变 ID 冻结。
构建始终最多 `-j4`，链接池为 1、lld 线程 4；没有修改宿主网络或调度参数。

构建命令、镜像 ID、宿主和软件包信息分别保存在
[对照配置](control-configure.json)、[候选配置](candidate3-configure.json)、
[测量环境](measured/environment.json)、[宿主信息](environment.json) 中。
共享构建目录后来用于扩展验证，测量用二进制已单独冻结，SHA256 和原生保留路径见
[artifact-manifest.json](artifact-manifest.json)。

## 方法与解释边界

每场景三轮、每轮计时 3 秒，对照/候选顺序为 AB、BA、AB，进程间休息 0.5 秒。
建连、预热池容量和认证在计时前完成；无额外查询预热。按实际完成操作数除以实际计时
墙钟计算吞吐，每次操作检查响应内容，任何异常使进程失败。池场景只借还，不执行查询；
容量 64 表示保有 64 条连接，计时循环仍在单执行器上逐次借还，不是 64 路并发查询。

协议场景保持一条直接连接。PostgreSQL 行集 SQL 为
`SELECT i FROM generate_series(1,256) AS i`；MySQL 为 `SELECT 42`；Redis 为 ECHO。
Pipeline 的单位是每批 64 个命令，不能把批次吞吐直接当成命令吞吐。
MySQL 无 reset 与默认归还分别报告，生产默认 reset 没有被取消。默认归还异步安排
reset，下一次借用等待它完成，计时末尾可能尚有一个 reset；该行不是同步 reset 完成率。

MySQL 8.4 的首次 caching_sha2 完整认证在现有客户端中要求 TLS。使用官方 MySQL
客户端先做一次 TLS 认证，再让两版在相同缓存条件下测量明文协议，认证不计时。
Redis 两版都使用已有的可取消建连接口，避免旧普通 HELLO 解码缺陷污染建连前提。

MySQL 的大幅收益对应本机约 44 ms 的旧小包往返。结合两次拆分写和默认关闭的
TCP_NODELAY，判断主要来自避免 Nagle/延迟 ACK 交互；没有抓包证明每一次延迟的成因。
这不是任意网络、TLS、SQL 或生产业务都能获得数百倍收益的声明。

Redis Pipeline 的进程 CPU/计时批次数从约 15.26 μs 降为 12.06 μs（−20.96%）。
该 CPU 指标包含子进程建连和退出，用计时完成批次数归一化，不是严格隔离计时区间的
CPU 计数。单连接吞吐仍为 −1.10%，因此不宣称 Pipeline 吞吐达到 +10%。
三轮结果没有建立置信区间或真实 p99，也不是数据库服务端峰值或官方排行榜成绩。

Linux `/proc` 每 0.5 秒检查编译活动，[检测结果](measured/quiet-host.json) 为有效；
Windows 和其他 VM 活动不在监测范围内。基准和构建串行执行。

## 定位与实现

1. PostgreSQL 原先逐消息分别读 5 字节头和正文。现在每连接 8 KiB 预读缓冲跨消息
   复用，仍逐条验证消息长度并返回拥有数据的结果；取消、断线、重连清空未读位置。
   普通借还按槽位增量更新统计，避免每次扫描整个池；扩容、重连和关闭仍完整刷新。
2. 三种池都在预算为正、连接可立即借出时避开无用定时器。排队、争用、过期预算、
   FIFO、取消和关闭仍走已有归属路径。MySQL/Redis 保留已有位图和快速归还机制。
3. MySQL 小于等于 4096 字节的正文和四字节包头合并写出并复用缓冲，大包保持分包
   路径，序列号和恰好最大包长度时的空终止包都保留。
4. Redis 普通命令、聚合响应和 Pipeline 共用完整响应解析引擎，整批共用一个协程帧
   和结果容器。分片保持累计偏移，节点保留实际 depth；统一执行已有解析器的
   `max_embedded_depth=5` 限制，修复旧递归路径丢失嵌套兄弟节点、HELLO 留尾数据的问题。

采用现有 Client/Pool 职责边界，解析引擎复用拥有结果与追加结果两种策略；没有增加
线程锁或改变公开异步接口。PostgreSQL 每条连接增加约 8 KiB 接收存储，这是明确的内存取舍。
协议依据：[PostgreSQL 格式](https://www.postgresql.org/docs/current/protocol-message-formats.html)、
[MySQL 包格式](https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_basic_packets.html)、
[RESP 规范](https://redis.io/docs/latest/develop/reference/protocol-spec/)。

配对 perf 的池诊断：对照 1499 样本、候选 1496 样本，均零丢样。
对照中可见 with_deadline、when_all 与分配开销；候选热点转向借还和时钟。
完整 self/inclusive 报告在 [measured](measured/) 中，采样运行不并入吞吐矩阵。
strace 诊断中 PostgreSQL 256 行的 io_uring_enter/计时查询约 520.38→2.99，MySQL
查询约 4.24→2.93。追踪显著改变墙钟吞吐，只用来解释调用次数，不作为成绩。

## 正确性与真实服务验证

- [7 个 Release 套件](extended-release-tests.log) 与 [相同 7 个 ASan/UBSan 套件](asan-tests.log) 全通过，
  包括争用、故障注入、取消、关闭、重连、回收及 TLS。
- 新增 PostgreSQL 粘包/分片、13 KiB 字段、重复查询与池计数；MySQL 二进制字节、
  4 KiB 阈值、16 MiB 分包和空终止包；Redis 嵌套兄弟节点、分片及空闲池预取消测试。
- 真实 MySQL/Redis 池的 [Release](live/release-tests.log) 和 [ASan](live/asan-tests.log) 各 2 套件通过，
  包括 MySQL TLS、多执行器分片以及受监督退出。
- PostgreSQL [12 次独立驱动检查](live/postgresql-driver-checks.json) 全通过：两种构建 ×
  明文/require/verify-full × 小字段/超过 13 KiB 字段，包含 Prepared Statement 和 UTF-8。
- psycopg 对照互操作明文/TLS 各 [4 项](live/psycopg-disable.log)、[4 项](live/psycopg-require.log) 通过；
  [libpq 参考客户端](live/libpq-reference.log) 确认实际 TLS 服务和返回值。

ASan 使用 portable O1、mimalloc/ThinLTO 关闭、lld 4 线程；
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`，UBSan 遇错停止并打印栈。
上述进程正常退出。证书私钥没有进入 Git。

[审计记录](audit.json) 和前期原始结果保留了认证前提错误、不完整矩阵，以及先前 Redis
协议收益不足的完整矩阵。扩展配置修复了测试缺少直接 spawn 导入的问题；真实服务检查
还定位并修复了 PostgreSQL 测试驱动临时协程 lambda 的 stack-use-after-scope。
[原始 ASan 报告](driver-asan-diagnostic.log) 保留，修复后重新完成全部真实服务检查。
这些修复没有改变最终计时的数据库实现。

## 复跑、合并与资源

复跑前必须重新取得测试令牌。在隔离的对照/候选源码目录使用上述保存的 CMake 参数，
以 `cmake --build ... --target bench_database -j4` 构建，然后运行：

```sh
python3 testing/bench/run_database_bench.py \
  --control /path/to/control --candidate /path/to/candidate \
  --output /path/to/new-output --seconds 3 --rounds 3 --profiles
```

需要 Docker、taskset、strace、perf 及已冻结的三个镜像。驱动只清理自己创建的容器，
不调整宿主或清理其他任务。[reproduce](reproduce/) 保存了本次精确辅助脚本；其中本机
绝对目录在迁移机器时需要替换，TLS 证书应按脚本重新生成。

`codex/database-performance` 可提交评审，包含 gRPC 前序改动和完整数据库优化/回归提交。
建议按依赖顺序合并，控制分支不进入产品。Linux 验证通过；原生 Windows CI 尚未执行，
应作为合并前提。没有 push、创建 PR 或实际合并 develop。

数据库重负载完成，测试令牌于 **2026-10-20 22:55:04 上海时间**释放；
本任务容器和保活进程已清理，[最终清理记录](cleanup-final.json) 可核对。
