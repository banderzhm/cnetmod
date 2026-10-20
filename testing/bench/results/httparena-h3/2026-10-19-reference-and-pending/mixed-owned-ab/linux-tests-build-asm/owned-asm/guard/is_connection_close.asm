
/var/tmp/cnetmod-pmtu-deadline-5afc506d/baseline-guard/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d200 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::is_connection_close() const>:
  34d200:	48 8b 07             	mov    (%rdi),%rax
  34d203:	48 3b 47 08          	cmp    0x8(%rdi),%rax
  34d207:	74 10                	je     34d219 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::is_connection_close() const+0x19>
  34d209:	55                   	push   %rbp
  34d20a:	48 89 e5             	mov    %rsp,%rbp
  34d20d:	0f b6 00             	movzbl (%rax),%eax
  34d210:	24 fe                	and    $0xfe,%al
  34d212:	3c 1c                	cmp    $0x1c,%al
  34d214:	0f 94 c0             	sete   %al
  34d217:	5d                   	pop    %rbp
  34d218:	c3                   	ret
  34d219:	31 c0                	xor    %eax,%eax
  34d21b:	c3                   	ret
  34d21c:	cc                   	int3
  34d21d:	cc                   	int3
  34d21e:	cc                   	int3
  34d21f:	cc                   	int3
