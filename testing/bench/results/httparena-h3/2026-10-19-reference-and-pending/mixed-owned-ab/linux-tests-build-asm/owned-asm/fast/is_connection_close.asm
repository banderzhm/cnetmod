
/var/tmp/cnetmod-h3-owned-fast-candidate-fbaeb04a21614732b24a7fdae850fcd0/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d5e0 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::is_connection_close() const>:
  34d5e0:	48 8b 07             	mov    (%rdi),%rax
  34d5e3:	48 3b 47 08          	cmp    0x8(%rdi),%rax
  34d5e7:	74 17                	je     34d600 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::is_connection_close() const+0x20>
  34d5e9:	55                   	push   %rbp
  34d5ea:	48 89 e5             	mov    %rsp,%rbp
  34d5ed:	0f b6 00             	movzbl (%rax),%eax
  34d5f0:	24 fe                	and    $0xfe,%al
  34d5f2:	3c 1c                	cmp    $0x1c,%al
  34d5f4:	75 0d                	jne    34d603 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::is_connection_close() const+0x23>
  34d5f6:	48 83 7f 18 00       	cmpq   $0x0,0x18(%rdi)
  34d5fb:	0f 94 c0             	sete   %al
  34d5fe:	5d                   	pop    %rbp
  34d5ff:	c3                   	ret
  34d600:	31 c0                	xor    %eax,%eax
  34d602:	c3                   	ret
  34d603:	31 c0                	xor    %eax,%eax
  34d605:	5d                   	pop    %rbp
  34d606:	c3                   	ret
  34d607:	cc                   	int3
  34d608:	cc                   	int3
  34d609:	cc                   	int3
  34d60a:	cc                   	int3
  34d60b:	cc                   	int3
  34d60c:	cc                   	int3
  34d60d:	cc                   	int3
  34d60e:	cc                   	int3
  34d60f:	cc                   	int3
