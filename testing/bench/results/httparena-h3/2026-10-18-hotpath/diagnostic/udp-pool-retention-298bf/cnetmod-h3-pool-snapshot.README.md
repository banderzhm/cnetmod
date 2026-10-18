# 冷时 UDP 池与 inbox 快照（仅准备，未 attach/执行）

脚本：`C:/Users/25007/.codex/tmp/cnetmod-h3-pool-snapshot.py`。
只使用 GDB 的 DWARF、内存读取和 `/proc`，不调用目标函数，不写目标内存。
没有调用 `pool_size()`/池 getter、`statistics()`、`mi_collect`、`mi_usable_size`、
`mi_stats_print` 或 stats merge；不存在的池只输出 `constructed=false`。

## 下一次独占执行窗口

先保存 PID、`/proc/PID/exe` 指向的二进制 SHA、编译输入 SHA、
`/proc/PID/status`、`smaps_rollup`、工作负载结束时间与采样时刻。
GDB 会暂停全部线程，因此只能在请求已结束的冷窗口使用，不能把暂停期间计入性能结果。
这次准备依据的原生二进制为
`/var/tmp/cnetmod-h3-handshake-retention-1792320533/arena_h3_server`，
SHA `298bf8277e7b1edc32f7e1ab1ce64729a52e8884e97e9486a77a56a20f4cabc4`；
已只读确认含 DWARF 和两个 UDP 池静态指针符号。

在 WSL 本地使用以下模板（替换 PID/输出路径；本次未运行）：

```sh
gdb -q -nx -batch \
  -iex 'set auto-load off' \
  -iex 'set pagination off' \
  -iex 'set non-stop off' \
  -iex 'set may-call-functions off' \
  -se /proc/PID/exe -p PID \
  -ex 'source /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-pool-snapshot.py' \
  -ex 'cnetmod-cold-pools' \
  -ex detach > /var/tmp/cnetmod-pool-idle-snapshot.log 2>&1
```

脚本输出一行 `CNETMOD_COLD_SNAPSHOT <JSON>`。自动搜索 `main` 栈帧里的
`server`，直接取其 `impl_`；若 Release 调试信息将变量优化掉，输出
`server.unavailable`，池快照仍有效。若已从受控调试环境获得 **现存** impl 地址，
改命令为 `cnetmod-cold-pools 0xADDRESS`；不扫描/猜测 heap，不构造对象。
类型查找、智能指针布局不匹配也会明确输出 unavailable，不把缺失当零。
当前未执行 GDB，DWARF/容器布局兼容性仍待该窗口验证。

## 输出解释

- `cached_blocks_approx`：和现有 `pool_size()` 相同的
  `min(capacity, max(enqueue-dequeue,0))`；不是累计分配数，也不是活跃 lease 数。
  全线程暂停仍可能恰好停在队列操作中途，脚本另抽查最多4个 ready slot，报告未提交位置。
- `cached_payload_bytes` 是池拥有的有效 payload 容量；这些空闲于网络的缓存块
  **仍是分配器的 live allocation**，不能算 mimalloc 已释放缓存。
- `queue_storage_bytes` 为实际 `sizeof(cell) × capacity`，small 默认2MiB，独立于缓存块数。
- `sampled_raw_allocation_addresses` 只读取已有指针；当前指向1字节 empty header，
  `pooled_buffer::data()` 则为 raw+1。脚本不调用任何 usable-size 函数。
- `shared_budget` 只统计一次共享 inbox budget，包括 active/queued/peak/drop。
  每个 shard 的 active connections/pooled/heap receive counters 单独读取后汇总。
  不调用会拿 latch 的 `http3_server::statistics()`，避免暂停持锁线程后死锁。
- `mimalloc.reserved/committed` 是 subprocess 的原始 OS-accounting 数字；
  `proc_smaps_rollup` 的 Rss/Private/Anonymous 才是实际映射驻留证据。
  reserved、committed、RSS 和 live allocation 不相等。

## 当前 Linux 路径修正

最新源码 `http3_server.cpp` 的 Linux server-context 分支为 SO_REUSEPORT 独立 shard，
走 `run_loop()` 每次8包；共享 socket dispatcher 每批64及连接 inbox 是 Windows 路径。
32 workers 下普通 Linux arena 同时接收 lease 约受 `32×8=256` 的批次容量约束，
idle 正常每 shard 有1个已挂起的接收 lease。此估计只适用于当前独立 shard 路径，
并不覆盖其它使用相同全局 UDP 池的 API/线程。
arena 将 receive max size 配为1472，所以这些 lease 走2048 small pool；
默认65527不是该调用点的实际参数。large pool 正常应未构造，inbox budget 应为零。

因此 **32768 是缓存上限，不是本次已占用数量**。不能先用其80MiB满池分配量解释
idle37s的216358912字节RSS；必须先读 actual cached count。

## 如何区分分配器驻留与存活对象

当前 Release `CMakeCache.txt` 为 `MI_STATS=OFF`、`MI_GUARDED=OFF`。
`page.c` 的 normal allocation 统计受 `MI_STATS` 门控；`malloc_normal.current=0`
不能证明零 live。脚本刻意输出 `malloc_live_bytes=null`。
OS reserved/committed 更新仍存在于 `os.c`，但也不能代表应用存活字节。
本次不调用 stats print/merge，它们可能更新/合并状态、取锁和输出，已不属于纯读取。

先比较相同冷时刻：

1. pool cache 数量、large pool 是否存在、queue storage；这些能直接归属到已知 owner。
2. active connections、active/queued inbox 是否归零；结合 life 的 weak-owner 回归，
   只排除这些 owner，并不能证明进程所有分配均已释放。
3. RSS 与 allocator committed 在多轮同负载后是否进入平台。对象 owner 已释放而驻留不降
   与页缓存/线程本地缓存相符，但仍不是精确的 allocator-free 字节结论。

要给出全进程 exact live bytes，现有无统计原生二进制加上述少量字段不够。
后续若需要，另开授权诊断实验使用带 allocation stats 的构建，读取所有 heap/theap
未合并统计及跨线程释放延迟；或独立实现与此版 mimalloc 元数据匹配的只读 page/free-list
枚举。两者都需额外验证，不能把 RSS 减去 pool 容量的差额直接命名为 allocator cache。
当前脚本不做通用 heap walker，也不改变原生比较条件。

## empty header 优化后的量化比较（本次不改生产）

现状 `sizeof(block_node)==1`，new/delete 持有 raw pointer，lease 暴露 raw+1。
最小候选可以去掉无用途 header，让队列直接存 allocation base，并保持原有 RAII、
跨线程返回、队列容量、超容立即 delete、可见 buffer size 和取消完成前保 buffer 语义。
不同时改 cache cap、PMTU、分配器选项或工作负载。

脚本读取 `_mi_theap_empty` 静态 bin 表，并在 GDB 主进程算 bin，不调用目标函数。
当前 Release/无 padding 预期如下（以实际输出核验）：

| 请求字节 | 预期 class |
| --- | --- |
| 2048 | 2048 |
| 2049 | 2560 |
| 65536 | 65536 |
| 65537 | 81920 |

对等 cache 数量 N，small class 理论差额为 `512×N`，large 为 `16384×N`。
small 满池上限差16MiB，但若当前 Linux 仅缓存约256块，差额只有约128KiB；
active lease 也会受益，不能用 free-cache 数量假装全体 lease 数量。
这是一项分配 class/alignment 优化，不承诺 RSS 必然按该差额下降。

后续 A/B 用全新进程、相同32workers/64connections/64并发、12×200k请求、
相同轮间2s与最终37s idle，分别保存正确性、throughput/p99、RSS/private 时序和冷快照。
核对 binary/source SHA、allocator flags、static资产、接收路径一致；不运行 mi_collect。
池数量若明显不同，先报告差异，不能把整个 RSS 差额全归 header。
保留跨线程归还、超 cap 释放、析构与取消完成后的 lease 回收测试；测试执行由 root/life
的串行窗口安排。
