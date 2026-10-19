# agensio alpha.23 与当前 cnetmod QUIC 发包路径只读对照

状态：源码核查；没有构建、负载、修改仓库或应用候选。参考 tag v0.1.0-alpha.23，HEAD e60c61c7643e47c046fbb5136f8b46f7f7f9289b。Windows checkout 的 tests/nul 不完整不影响本报告读取的源文件：connection.hpp、recovery.hpp、udp.hpp、stream.hpp 由 native git show 与 checkout 按 CRLF 归一化逐字核对；git blob/物理 SHA 在 reference-inputs.json。HTTP3 connection.hpp 也做了相同 git show 核对，存为 http3_connection.hpp。cnetmod 所读文件物理 SHA 在 cnetmod-read-inputs.json，这是本次只读对照身份，不是新构建/性能快照。

根代理提供并已完成的额外镜像绑定：从 dd75 runtime 和 e544 builder 通过 docker create/cp（未启动）提取的 ELF SHA 均为 8c0da17c36f77b67357f3769be3af19e36d39ed04aec5286f64c329e6db3e986；builder .git HEAD=e60c61，下载的 src 130 文件和 CMakeLists 与 builder 逐 SHA 相等。该独立证据保存在 /var/tmp/agensio-reference-identity-20261019/identity.json，因而这些参考源码可对应根记录的 1,046,455 中位镜像，而非仅依据 tag 名推测。本代理未重新提取镜像或运行该基准；吞吐差距的具体成因仍不可由源码直接推出。

## 一、已证事实：批处理边界和数据存储

agensio 调用链：src/quic/udp.hpp wait_read:438→drain:447→recvmmsg:462→dispatch:492/receive→touched_ 去重:551-553→所有 touched connection produce:482→flush:483→SendBatch::flush/sendmmsg:112。timer fire:616 也对整批 due connections 先 produce 后统一 flush。异步 upstream completion 的 flush_connection:427 则会立即 produce/flush，不能声称所有来源永远等同一个收包批次。

SendBatch 在 udp.hpp:50-54 预留 worker 级 1MiB buf；reserve:59 给出最终连续空间指针；QuicConnection::produce(connection.hpp:359) 在这块空间上 build_packet:373/420，commit:376/426。普通连接每次 produce 上限128 datagrams（383）；批满 ep_.flush 后继续（397-400）。receive 是16个65536槽（udp.hpp:275-276），一唤醒最多8轮recvmmsg（450）；UDP_GRO cmsg分割后交给dispatch（466-476）。这是上限和源码结构，不是实际每次都满批的证据。

GSO commit 检查同peer、连续buf位置、相同或短尾段、最多64段/65535B（udp.hpp:79-93）；每个GSO group一个iovec（155-162），通过 UDP_SEGMENT cmsg（163-172）。buf只重置pos，msgs清空保留vector容量（136-140）。这不是把已生成的独立packet再次拷进slab，而是最初就在batch buf建包。

cnetmod 已有批处理，不能描述为逐包 syscall：quic_connection.cpp:625-633 每连接保留16个ciphertext vector及16个plaintext payload vector；flush_send_queue:4506→pack_and_send_packets:3691。后者在 receive_mutex 内构造最多16包（3721-3743），出锁一次总字节pacing（3758-3768），构造借用packet内存的udp_send_datagram描述符（3770-3775），send_datagram_batch:1302→async_sendto_segmented_batch或async_sendto_batch:1338-1348。成功/非终止处理后clear packet并move回连接storage（3783-3787），保留capacity。Multipath或不同socket/context保留单包/切executor路径（3708-3716、1316-1333），是API语义边界。

**排除的假设：当前 cnetmod GSO 没有“16 packet vectors 再复制整包到 GSO slab”。** io_uring_async_op.cpp:1745-1749 iovecs直接引用datagrams[index].bytes.data；1780-1781 group使用iovecs[start]及多iov长度；1811直接sendmmsg。segment_size的memcpy只有2字节（1791附近）。真实结构差异是agensio worker共享连续最终存储/跨连接聚合、一个group一个iov，而cnetmod connection16包/多iov描述符，不是额外用户态全包GSO拷贝。

cnetmod当前每个分组尝试会读取UDP_CORK和UDP_SEGMENT两个getsockopt（io_uring_async_op.cpp:1801-1807），防止破坏应用原生socket选项；agensio SendBatch没有这两项动态查询，内置gso_状态及EIO/EINVAL fallback（udp.hpp:107-131）。省掉查询必须先设计生命周期安全的socket选项状态契约，不能直接删当前保护。

agensio SendBatch对EAGAIN等未发送suffix计drop并clear，让recovery修复（udp.hpp:100-134）；cnetmod普通batch先即时sendmmsg，再真实背压时异步等待并保留partial prefix（io_uring_async_op.cpp:1907-1934、1966后取消版本；quic_connection.cpp:1336-1356循环suffix）。这会影响延迟/CPU及恢复语义；不能为了缩短路径直接改成丢弃。

## 二、已证事实：拥塞、ACK、PMTU、PTO

agensio produce先单独尝试MTU target（connection.hpp:369-381），再普通max_datagram循环。普通cap按cwnd_room缩小但至少1200（393-395）；wants_to_send中ACK/PTO不依赖ready数据（1166-1174）；STREAM只有cwnd_room>=1200或rec_.probes>0才取（1353）。这不是每个普通packet按最后精确ciphertext bytes重新做统一admission的同一实现。

PMTU want_ping_ 分支跳过CRYPTO（1231）及普通application control/STREAM（1257-1409），随后添加独立mtu_probe item（1422-1424）。它可携已有ACK（1224-1228）；末尾某些MAX_STREAMS逻辑仍位于want_ping大分支之外（1410），因此不把源码注释“alone”扩大成无条件仅PING/PADDING/ACK保证。rec.in_flight=in_flight&&!want_ping_（1471），Recovery仅对in_flight累计flight（recovery.hpp:145-154）。ACK MTU后扩大max_datagram并倍增下一target（1070-1083），loss则停止本次search（1109-1110）。这是对方具体策略，不是允许cnetmod绕过其现有PMTU拥塞计费的结论。

cnetmod pack_one_rtt_packet:4187 在已有ACK和可靠frame选择同一路径中根据pending target加PING、padding（4268-4279、4356-4367）；非PTO统一按estimated_packet_size admission（4395-4425），成功才清ACK及pending并将probe tag传recovery（4448-4462）。quic_recovery.cpp:123-135将recovery_tracked包计flight；probe丢失从flight扣除但不作为可靠拥塞事件（205-230）。这与agensio“PMTU不进flight”不等价，不能照搬对方更大target/绕cwnd取得性能数字。

本次cnetmod源码仍是已冻结mixed+owned待整合的PMTU前路径；先前真实12身份wire已经独立证明optional pending target阻挡普通ACK和纯ACK清空flight后缺writer retry两个缺陷，接受patch仍待根整合。本对照不重新解释为性能瓶颈或声称已在当前repo修复。

agensio实际PTO链：on_timer:432→Recovery::on_timeout:315（probes[which]=2:337）→probe_with_data:460选最老仍inflight/ack-eliciting packet元数据，转lost范围→apply_lost:1092→produce按正常max_datagram重发STREAM（1352-1404）；probes允许合法超cwnd并递减（1427-1433）。cnetmod链：process_pto相关函数约3504→recovery.on_pto_expired:3568→固定2包循环:3570→复制可靠quic_send_frame到队列→pack_one_rtt_packet(true):3589。现有待验证独立PTO角色patch仅限制可选PMTU选择，不改变合法PTO例外。参考策略可帮助明确角色，但不能替代真实old/new PTO wire验收。

ACK唤醒区别：agensio receive处理ACK调用rec_.on_ack_received→apply_acked（connection.hpp:569-573），外层touched连接统一produce；因此writer wake来自endpoint整轮调度。cnetmod依赖request bit+单writer token（4508-4522），frame/burst结束重新检查请求（4664后），ACK纯控制情形必须显式保留调度edge。把agensio的“无显式flush request”移植到cnetmod会忽略架构差异。

## 三、已证事实：重传数据所有权与回收粒度

agensio SentPacket保存vector<SentItem>，每item只有kind/FIN/id/offset/len（recovery.hpp:37-53）；PnSpace初64槽ring，覆盖活槽时翻倍，record清items保留capacity（78-92、114-118），ACK/loss时forget槽（99-103、196-247、254-276）。PTO复制metadata而非帧bytes（connection.hpp:468-470），丢失范围回到QuicStream::lost，再由head/mem/file按offset重新取bytes（1095-1102、1484-1518）。QuicStream mem是string_view，file是const File*（stream.hpp:64-78），不是每packet shared_ptr owner。

HTTP3 respond将Response内MemoryBody数据视图设q.mem（http3/connection.hpp:554-568）；stream字节全ACK及FIN_ACK之后设置send_done并回调app_.on_stream_sent（quic connection.hpp:1056-1059），HTTP3再maybe_close（http3 connection.hpp:201-206）。它的生命周期保障在应用stream/Response层，不能凭metadata小就直接将cnetmod每frame shared_ptr删成裸view。

cnetmod quic_send_frame保留owned bytes或shared_body+offset/size/header/owned prefix（quic_send_pipeline.cppm:26-70）；append_to把各段写入plaintext payload（cpp:158-171），随后构建sealed packet。recovery保存可重传frame，ACK释放、loss转移frame ownership、PTO alias去重，缓存node回收前清mapped避免retain owner（quic_recovery.cpp:105-119、123-225）；loss_detector另有inflight map及node缓存（loss_detection.cpp:105-114）。当前owned fastpath只改变共有frame表示的常见访问路线，不意味着已改成agensio stream范围重建。

这不是“对方零拷贝”：agensio copy_stream_bytes也memcpy body到最终packet plaintext（1495/1503），再seal；cnetmod shared正文也最终必须进入加密输入。已证差异是中间状态/引用次数、payload与cipher vector分离、元数据与body所有权层级、worker/connection存储粒度。具体字节拷贝和分配的动态次数仍需分阶段计量。

## 四、待测假设与有界验证次序

1. worker-wide聚合及减少post可能改善跨多连接、小响应批大小及syscall/loop-turn摊销。需先记录actual datagrams/sendmmsg、GSO group数、iovcnt、每连接burst长度、每listener wake处理连接数及post数；相同socket选项/路径MDS/pacing/并发量做单变量A/B。不能仅把batch16提高128，也不能牺牲timer/cancellation公平性。
2. 直写final packet存储可减少某些中间vector管理/布局成本，但当前GSO没有slab复制可删。需追踪plaintext append/seal的bytes、allocation/capacity与cache/CPU，保持crypto失败、borrowed buffers、partial send及跨executor生命周期。先局部prototype独立测，不能将复制次数当已证收益。
3. ring compact metadata可能减少map/node与shared_ptr成本，但agensio把body可靠生命周期放在stream/Response层。需保留cnetmod原ACK/PTO alias、loss/discard、临时prefix和CLOSE行为，通过真实重传/ownership regression后才对比allocations/CPU；不是本轮可无语义改变直接替换的微优化。
4. 每GSO两次getsockopt、connection batch临时pending/datagram vectors是可定位成本点。需用实际批次分布与profile确定占比；省查询要有明确native option变更/代际契约。无profile前不报节省百分比。
5. agensio PMTU可升到peer允许的大datagram，loopback/GSO路径与cnetmod当前1452等配置不等。应先核实实际wire datagram-size分布、benchmark peer max_udp、GRO/GSO及pacing启用，才把1,046,455差距归因于某代码路径。镜像绑定已证，路径设置及因果仍需测。

结论范围：下载参考使我们可以准确点名可测结构差异；不支持“没有batch”“GSO又全包复制”“shared_body就是零拷贝”“PMTU可直接不计flight”这些泛化。当前报告与PMTU两hunk、owned54round结果相互独立，不占用它们的构建/负载窗口。
