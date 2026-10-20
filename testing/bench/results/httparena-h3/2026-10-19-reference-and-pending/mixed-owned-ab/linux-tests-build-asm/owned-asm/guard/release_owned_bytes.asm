
/var/tmp/cnetmod-pmtu-deadline-5afc506d/baseline-guard/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d230 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::release_owned_bytes()>:
  34d230:	55                   	push   %rbp
  34d231:	48 89 e5             	mov    %rsp,%rbp
  34d234:	48 89 f8             	mov    %rdi,%rax
  34d237:	c5 f8 10 06          	vmovups (%rsi),%xmm0
  34d23b:	c5 f8 11 07          	vmovups %xmm0,(%rdi)
  34d23f:	48 8b 4e 10          	mov    0x10(%rsi),%rcx
  34d243:	48 89 4f 10          	mov    %rcx,0x10(%rdi)
  34d247:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
  34d24b:	c5 f8 11 06          	vmovups %xmm0,(%rsi)
  34d24f:	48 c7 46 10 00 00 00 	movq   $0x0,0x10(%rsi)
  34d256:	00 
  34d257:	5d                   	pop    %rbp
  34d258:	c3                   	ret
  34d259:	cc                   	int3
  34d25a:	cc                   	int3
  34d25b:	cc                   	int3
  34d25c:	cc                   	int3
  34d25d:	cc                   	int3
  34d25e:	cc                   	int3
  34d25f:	cc                   	int3
