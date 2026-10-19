
/var/tmp/cnetmod-h3-mixed-prefix-candidate-4a739b77e7584ead85cbf02304fb77ff/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d690 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::is_connection_close() const>:
  34d690:	48 83 7f 18 00       	cmpq   $0x0,0x18(%rdi)
  34d695:	74 03                	je     34d69a <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::is_connection_close() const+0xa>
  34d697:	31 c0                	xor    %eax,%eax
  34d699:	c3                   	ret
  34d69a:	48 8b 07             	mov    (%rdi),%rax
  34d69d:	48 3b 47 08          	cmp    0x8(%rdi),%rax
  34d6a1:	74 f4                	je     34d697 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::is_connection_close() const+0x7>
  34d6a3:	55                   	push   %rbp
  34d6a4:	48 89 e5             	mov    %rsp,%rbp
  34d6a7:	0f b6 00             	movzbl (%rax),%eax
  34d6aa:	24 fe                	and    $0xfe,%al
  34d6ac:	3c 1c                	cmp    $0x1c,%al
  34d6ae:	0f 94 c0             	sete   %al
  34d6b1:	5d                   	pop    %rbp
  34d6b2:	c3                   	ret
  34d6b3:	cc                   	int3
  34d6b4:	cc                   	int3
  34d6b5:	cc                   	int3
  34d6b6:	cc                   	int3
  34d6b7:	cc                   	int3
  34d6b8:	cc                   	int3
  34d6b9:	cc                   	int3
  34d6ba:	cc                   	int3
  34d6bb:	cc                   	int3
  34d6bc:	cc                   	int3
  34d6bd:	cc                   	int3
  34d6be:	cc                   	int3
  34d6bf:	cc                   	int3
