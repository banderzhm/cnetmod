# agensio HTTP/3 static → packet → release 只读审查

证据基线：`/var/tmp/agensio-reference-alpha23-20261019`，tag
`v0.1.0-alpha.23`，HEAD `e60c61c7643e47c046fbb5136f8b46f7f7f9289b`。
根 agent 的 `/var/tmp/agensio-reference-identity-20261019/identity.json`
把该树130个src文件和CMakeLists与builder精确绑定；builder/runtime ELF同为
`8c0da17c36f77b67357f3769be3af19e36d39ed04aec5286f64c329e6db3e986`。
因此这是根报告1046455中位对应源码，非仅按tag推测。这里未复测该数字。
本文行号来自WSL完整树；Windows checkout不作为依据。
cnetmod对应行号来自当前工作树只读快照，不代表未来最终PMTU baseline已冻结。
未编译、运行新binary或负载；未修改任何项目源码。

## 一次加载、每次响应引用、一次包内物化

|阶段|agensio真实代码|拷贝、分配或持有事实|
|---|---|---|
|缓存miss|`src/handlers/static.cpp:554–572`|`make_shared<CacheEntry>`；`data.resize(fi.size)`后`read_all`读文件；构造header/压缩变体；入共享cache和worker local index。不是每次请求读文件。|
|缓存hit|`static.cpp:492–533`|先查worker-local index；需要时进入FileCache。命中仅取一次response强引用；`last_access`至多按秒store。仍可能按配置revalidate文件/压缩twin，不能说完全没有stat。|
|压缩body选择|`static.cpp:129–179`|br/gzip选择独立CacheEntry；`MemoryBody`是已有data的string_view；`Response::entry`持有选中entry，防止发送中被cache eviction释放。`src/core/response.hpp:50–54`明确资源owner。|
|预编码headers|`static.cpp:217–222`|缓存构建时生成QPACK静态tail，存入`CacheEntry::h3_block`。`static.cpp:170–178`只把views放入response。不是每次重编码全部静态字段。|
|每次H3 headers|`src/http3/connection.hpp:597–621`|`s.scratch`构造section；status/server/date/content-type等仍按连接编码；`sec.append(r.prebuilt_h3)`复制tail；随后`out.append(px)`、`out.append(sec)`复制到`q.head`。不能称headers完全零拷贝。|
|body交QUIC|`http3/connection.hpp:554–590`|`q.mem=m->data`仅view；HEADERS/DATA header进入q.head；`q.total=head.size+body_len`、FIN，stream_ready。此步没有整body拼接到新的response字符串。|
|packet body物化|`src/quic/connection.hpp:1390–1400`、`1486–1522`|直接向packet payload写STREAM header，然后`copy_stream_bytes`从q.head或q.mem memcpy到同一packet；不是零拷贝，内存body至少有这次packet materialization。|
|加密|`src/quic/crypto.cpp:141–151`|body指针`pkt+hdr_len`同时作为EVP_EncryptUpdate输入/输出；tag写在body+plain_len；原地AEAD。`connection.hpp:1458–1467`随后header protection。|

包存储不是每包临时新vector：`src/quic/udp.hpp:50–58`的SendBatch默认构造
1MiB存储，`reserve(max)`返回其中已有区域，不resize。
`src/quic/connection.hpp:396–420`把batch.reserve得到的地址交build_packet，
`connection.hpp:1213`的payload就是buf+header偏移。
必须初始化的padding仍有真实memset：`connection.hpp:1438–1454`。
这里不展开UDP提交/recovery实现，另有PMTU agent审查。

大文件不是上述memory-body路径：`static.cpp:577–590`缓存descriptor entry；
`quic/connection.hpp:1507–1519`为流文件读入至少65536字节chunk并再memcpy到packet。
chunk在需要时resize，不能把这种路径当作零拷贝sendfile的H3路径。

## 资源释放和有意保留的容量

- cache由进程共享map与worker-local map共同保有entry：`src/cache.hpp:4–22`、
  `159–180`，实际`FileCache::find/insert/erase`见`src/cache.cpp:18–47`。
  eviction先标stale再从共享map删；worker-local条目惰性删；在途response引用仍有效。
  因此共享cache预算下降不等于物理body已释放；其他强引用可继续持有。
- H3 `maybe_close`要求发送侧完成和接收侧完成，见
  `http3/connection.hpp:858–886`。正常响应发送完成依赖q.send_done，而非仅交给socket。
  `quic/connection.hpp:1056–1057`在FIN ACK且全部byte range ACK后置send_done。
  `close_stream`取消QPACK引用、关闭QUIC stream，再交StreamPool释放。
- `src/http/stream_pool.hpp:46–55`先reset，再在pool<keep时保留对象；
  新请求`take`优先重用，池空才make_unique（29–41）。
  `http3/stream.hpp:102–128`reset会清view、request/response并clear各scratch。
  `core/stream.hpp:41–44`→`core/response.hpp:56–75`重置entry和owned_file；
  `quic/stream.hpp:96–125`清head/mem并重置chunk位置。
  **clear不等于归还capacity**，chunk甚至不clear；pool会保留峰值buffer容量。
- 不能套用StreamPool文件头“idle sheds pool”的评论来宣称H3 idle会shed：
  `StreamPool::shed`定义在58行，但本次全src/http3查无`.shed()`调用；
  实际调用发现于HTTP/2 `src/http2/connection.hpp:276`。本审查不据评论承诺H3归还RSS。
- AEAD context不是每包new：`crypto.cpp:107–108`分配Keys上下文，
  `85–87`在Keys::clear释放；每seal重置nonce/执行加密。

## 与当前cnetmod路径逐段对照

1. 应用body缓存：`testing/bench/arena_h3_server.cpp:29–40,45–90`在event loops
   之前读identity/br并预编码静态headers。`96–119`命中后给response.static_headers和
   response.shared_body赋shared_ptr。两边都避免热请求重复文件读取和整body拷贝；
   agensio是一般化分层可失效cache，cnetmod此adapter是基准进程不可变全量asset map，
   生命周期/功能范围不同。
2. H3：`src/protocol/http/v3/http3_session.cpp:2215–2272`构建HEADERS+DATA前缀，
   body保持shared_ptr；前缀另构`vector<byte>`后async_send_shared。
   静态header快捷编码见`http3_session.cpp:753–758`。
   两边都预编码静态部分；agensio还对server/date/content-type使用连接动态表。
   不可从字段或header策略差异直接推吞吐因果。
3. cnetmod混合共享frame：`src/protocol/quic/quic_connection.cpp:4815–4830`
   根据前缀交界选择shared_stream/shared_stream_with_prefix。
   `quic_send_pipeline.cpp:77–96`边界frame会为header+prefix reserve/insert owned bytes，
   body仍为shared_ptr范围；`158–171`append_to把owned/header/body复制到payload。
   agensio没有该独立frame-owned body对象阶段：它以QuicStream的head+mem源范围
   在包构造时直接写packet。两边都必须保有可靠重传所需的源数据，具体策略另审。
4. 当前cnetmod包构造：`quic_connection.cpp:4203–4211`复用独立payload和packet两vector；
   `4341`frame.append_to(payload)物化明文；`4424–4436`写packet header后seal。
   `quic_crypto.cpp:1637–1654`先packet.resize(header+N+tag)，再从**独立payload**
   读明文、直接写**最终packet**密文。不存在“先写临时ciphertext再复制一次到packet”
   可省掉的拷贝；存在被resize初始化、随后由AEAD覆盖的N+tag存储。
5. 因此agensio源码直接支持后续in-place候选方向：在最终包中物化明文、原地seal，
   避免使用独立明文scratch并减少冗余输出初始化。已准备的首轮候选为兼容保留旧scratch
   参数/字段，不承诺释放已有capacity或降低RSS；仍保留原frame.append_to
   明文物化copy，不应把它宣传为消灭所有copy或直接获得agensio吞吐。
   agensio的worker batch布局也不同，单独in-place候选并不自动复制其完整架构。
6. cnetmod目前也缓存AEAD context：`quic_crypto.cpp:1565–1582`，不是当前每包都新建context。
   cnetmod使用BoringSSL EVP_AEAD_CTX，agensio此处使用EVP_CIPHER_CTX接口；
   不能仅由API名称判断哪边crypto更快。

## 可以据此提出的验证问题

代码已证明两种packet存储/原地AEAD数据流不同，且agensio也有header/body memcpy、
分配与pool保留容量。它没有证明这些差异是1046455与cnetmod结果之差的根因。
后续只应在最终baseline上对单一候选完成模块一致性、crypto等价/负例、实际fault、
H3语义回归，再做相同条件A/B和固定180轮retention；不能根据源码形状报RPS收益，
也不能从clear或cache预算推导RSS已释放。
