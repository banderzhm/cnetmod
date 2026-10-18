# 冻结原生二进制 UDP 池 / allocator 冷快照

日期：2026-10-18。结论：本次 Linux 单轮200k后的高驻留不能归因于 UDP 池满缓存。
有效轮全部200000 started/done/succeeded/2xx，零 failed/errored/timeout；
加载器、二进制、输入资产及快照脚本的前后散列一致。该实验含冷时 GDB 暂停，不用于吞吐排名。

## 两次执行严格分开

- `valid-ipv4-wrapper/`：有效。外部原始目录
  `/var/tmp/cnetmod-h3-pool-native-9e0f2b12b7394d8e87b2c7eca1776430`。
- `invalid-ipv6-environment/`：无效，不是应用失败证据。首次遗漏既有私有 mount
  `gai.conf` wrapper，localhost 首选 `::1` 而服务端32 sockets为 `0.0.0.0:8443`；
  0 started、0 received、200000 failed。原始目录
  `/var/tmp/cnetmod-h3-pool-native-bcb1fbf900c04f2d8d2095a989ddae41`。
  三次池/allocator/连接读数均未变化。保留其原始 runner、日志和快照说明环境失配。

有效执行命令：

```sh
unshare --mount bash /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-isolated.sh \
  python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-pool-native.py --execute \
  --output /var/tmp/cnetmod-h3-pool-native-9e0f2b12b7394d8e87b2c7eca1776430
```

wrapper 只在私有 mount namespace 中绑定既有 IPv4 优先 `gai.conf`，没有修改 URI。
runner 验证该配置散列、localhost IPv4 优先级、lo UP/MTU65536、32 sockets，
创建 nonce 独占 network namespace。cleanup 仅针对本次 Popen process-group，
核验 PID start-time 及 container nonce label；有效轮退出0并清理结束。

## 数值（bytes）

| 指标 | 初始（无 warm-up 请求） | 200k后2s | 200k后37s |
| --- | ---: | ---: | ---: |
| RSS | 48,508,928 | 183,930,880 | 183,930,880 |
| mimalloc committed | 43,974,656 | 180,355,072 | 180,355,072 |
| mimalloc reserved | 1,076,166,656 | 1,076,166,656 | 1,076,166,656 |
| small池缓存块 | 0 | 64 | 64 |
| large池已构造 | false | false | false |
| active connections总和 | 0 | 0 | 0 |
| active/queued inbox | 0 / 0 | 0 / 0 | 0 / 0 |
| owned-page used字节上界 | 3,207,632 | 4,331,040 | 4,322,848 |
| owned空页reserved block capacity | 0 | 96,500,240 | 98,985,744 |

GDB暂停分别约0.800/0.869/0.736秒，后两次从请求结束起实际约2.001/37.001秒启动。
37s表示2s采样后继续空闲到总37s，而非第二轮请求。原始 `/proc` 与 GDB 内采样均归档。

small block2048、cache capacity32768、queue cell64字节；64块缓存只包含131072字节
payload，当前2049字节分配落2560 class，对应163840字节分配容量；另有固定2MiB queue storage。
Linux arena当前走32个独立 reuseport shard、每批8包，不走 Windows shared dispatcher/inbox。
初始32个阻塞接收 lease 的存在由调用路径推断，脚本没有声称直接测得全体 lease 数量。

故去除 empty header 的理论 free-cache class收益仅64×512=32768字节；
再计正常32个 idle receive lease 约另16384字节。不能以满池16MiB上限当本次预期RSS收益。

## owned-page 数据的范围

只遍历既有 subproc/heaps/theaps/page queues及 metadata theap，带循环检测与
256 heaps/2048 theaps/32768 pages/5秒硬界。三次均在该声明范围内完整，无 collector 调用。
`used` 包含尚未处理的 thread-free，因此是所遍历页的 live block bytes 上界；
不是全进程 live 上界。abandoned页、arena bitmap未遍历，不能声称全进程精确 live bytes。
空页 capacity 也不是OS committed或RSS，不可直接从RSS减去它来算残余泄漏。

37s空容量最大bins：12288字节class约14.97MB；40960、49152、81920各约14.25MB；
224约5.54MB；64约4.58MB。这里只能证明大量已空 owned pages仍存在，不能凭bin大小
断定具体 C++ 对象或协程类型。`MI_STATS=OFF` 下未把零 allocation统计当零存活对象。

## 可复查的实现和完整性

- frozen server：`298bf8277e7b1edc32f7e1ab1ce64729a52e8884e97e9486a77a56a20f4cabc4`。
- 有效轮 `runner.py`：`98be8601584bf8951ad2a0d0f1f9820a362b911bb9e16e9025ae175b3ddcb4f0`。
- 有效轮 `pool-snapshot.py`：`12f121976fc75cff80ab5eb65382715e30015614be036d2e62131a50f057a731`。
- 无效轮 runner：`3ca40a05002c87f6d02eb921a048fdda812ca96ec0b2f64266e680533a8c0290`。
- 每轮 `summary.json` 保存精确 commands、输入及加载库SHA、进程identity和采样时刻。
- `artifact-sha256.json` 为本归档的文件校验；不重复提交40MB可执行文件。

只读脚本设置 `may-call-functions off`，不写 inferior memory、不构造池、不调用
allocator/统计函数。离线 DWARF 审核先验证裸 module type名称、unique_ptr匿名成员、
pointer typedef解引用及8字节atomic；此后真实快照成功验证 main/server定位与32 shards读取。
附带的 preparation README 是执行前的设计记录；实际完成状态以本文件和原始 summary 为准。
