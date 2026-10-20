
/var/tmp/cnetmod-h3-owned-fast-candidate-fbaeb04a21614732b24a7fdae850fcd0/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d630 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::release_owned_bytes()>:
  34d630:	55                   	push   %rbp
  34d631:	48 89 e5             	mov    %rsp,%rbp
  34d634:	48 89 f8             	mov    %rdi,%rax
  34d637:	48 83 7e 18 00       	cmpq   $0x0,0x18(%rsi)
  34d63c:	74 05                	je     34d643 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::release_owned_bytes()+0x13>
  34d63e:	48 89 c6             	mov    %rax,%rsi
  34d641:	eb 10                	jmp    34d653 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::release_owned_bytes()+0x23>
  34d643:	c5 f8 10 06          	vmovups (%rsi),%xmm0
  34d647:	c5 f8 11 00          	vmovups %xmm0,(%rax)
  34d64b:	48 8b 4e 10          	mov    0x10(%rsi),%rcx
  34d64f:	48 89 48 10          	mov    %rcx,0x10(%rax)
  34d653:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
  34d657:	c5 f8 11 06          	vmovups %xmm0,(%rsi)
  34d65b:	48 c7 46 10 00 00 00 	movq   $0x0,0x10(%rsi)
  34d662:	00 
  34d663:	5d                   	pop    %rbp
  34d664:	c3                   	ret
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
