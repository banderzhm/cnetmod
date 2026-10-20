
/var/tmp/cnetmod-h3-owned-fast-candidate-fbaeb04a21614732b24a7fdae850fcd0/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d590 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::size() const>:
  34d590:	55                   	push   %rbp
  34d591:	48 89 e5             	mov    %rsp,%rbp
  34d594:	48 8b 4f 08          	mov    0x8(%rdi),%rcx
  34d598:	48 2b 0f             	sub    (%rdi),%rcx
  34d59b:	48 8b 47 48          	mov    0x48(%rdi),%rax
  34d59f:	48 01 c8             	add    %rcx,%rax
  34d5a2:	48 03 47 58          	add    0x58(%rdi),%rax
  34d5a6:	48 83 7f 18 00       	cmpq   $0x0,0x18(%rdi)
  34d5ab:	48 0f 44 c1          	cmove  %rcx,%rax
  34d5af:	5d                   	pop    %rbp
  34d5b0:	c3                   	ret
  34d5b1:	cc                   	int3
  34d5b2:	cc                   	int3
  34d5b3:	cc                   	int3
  34d5b4:	cc                   	int3
  34d5b5:	cc                   	int3
  34d5b6:	cc                   	int3
  34d5b7:	cc                   	int3
  34d5b8:	cc                   	int3
  34d5b9:	cc                   	int3
  34d5ba:	cc                   	int3
  34d5bb:	cc                   	int3
  34d5bc:	cc                   	int3
  34d5bd:	cc                   	int3
  34d5be:	cc                   	int3
  34d5bf:	cc                   	int3
