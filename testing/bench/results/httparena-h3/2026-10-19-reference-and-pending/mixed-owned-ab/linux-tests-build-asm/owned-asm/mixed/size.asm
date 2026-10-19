
/var/tmp/cnetmod-h3-mixed-prefix-candidate-4a739b77e7584ead85cbf02304fb77ff/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d640 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::size() const>:
  34d640:	55                   	push   %rbp
  34d641:	48 89 e5             	mov    %rsp,%rbp
  34d644:	48 8b 4f 08          	mov    0x8(%rdi),%rcx
  34d648:	48 2b 0f             	sub    (%rdi),%rcx
  34d64b:	48 8b 47 48          	mov    0x48(%rdi),%rax
  34d64f:	48 01 c8             	add    %rcx,%rax
  34d652:	48 03 47 58          	add    0x58(%rdi),%rax
  34d656:	48 83 7f 18 00       	cmpq   $0x0,0x18(%rdi)
  34d65b:	48 0f 44 c1          	cmove  %rcx,%rax
  34d65f:	5d                   	pop    %rbp
  34d660:	c3                   	ret
  34d661:	cc                   	int3
  34d662:	cc                   	int3
  34d663:	cc                   	int3
  34d664:	cc                   	int3
  34d665:	cc                   	int3
  34d666:	cc                   	int3
  34d667:	cc                   	int3
  34d668:	cc                   	int3
  34d669:	cc                   	int3
  34d66a:	cc                   	int3
  34d66b:	cc                   	int3
  34d66c:	cc                   	int3
  34d66d:	cc                   	int3
  34d66e:	cc                   	int3
  34d66f:	cc                   	int3
