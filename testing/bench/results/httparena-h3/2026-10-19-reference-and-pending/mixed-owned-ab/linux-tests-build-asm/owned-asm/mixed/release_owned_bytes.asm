
/var/tmp/cnetmod-h3-mixed-prefix-candidate-4a739b77e7584ead85cbf02304fb77ff/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d6e0 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::release_owned_bytes()>:
  34d6e0:	55                   	push   %rbp
  34d6e1:	48 89 e5             	mov    %rsp,%rbp
  34d6e4:	48 89 f8             	mov    %rdi,%rax
  34d6e7:	48 83 7e 18 00       	cmpq   $0x0,0x18(%rsi)
  34d6ec:	74 05                	je     34d6f3 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::release_owned_bytes()+0x13>
  34d6ee:	48 89 c6             	mov    %rax,%rsi
  34d6f1:	eb 10                	jmp    34d703 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::release_owned_bytes()+0x23>
  34d6f3:	c5 f8 10 06          	vmovups (%rsi),%xmm0
  34d6f7:	c5 f8 11 00          	vmovups %xmm0,(%rax)
  34d6fb:	48 8b 4e 10          	mov    0x10(%rsi),%rcx
  34d6ff:	48 89 48 10          	mov    %rcx,0x10(%rax)
  34d703:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
  34d707:	c5 f8 11 06          	vmovups %xmm0,(%rsi)
  34d70b:	48 c7 46 10 00 00 00 	movq   $0x0,0x10(%rsi)
  34d712:	00 
  34d713:	5d                   	pop    %rbp
  34d714:	c3                   	ret
  34d715:	cc                   	int3
  34d716:	cc                   	int3
  34d717:	cc                   	int3
  34d718:	cc                   	int3
  34d719:	cc                   	int3
  34d71a:	cc                   	int3
  34d71b:	cc                   	int3
  34d71c:	cc                   	int3
  34d71d:	cc                   	int3
  34d71e:	cc                   	int3
  34d71f:	cc                   	int3
