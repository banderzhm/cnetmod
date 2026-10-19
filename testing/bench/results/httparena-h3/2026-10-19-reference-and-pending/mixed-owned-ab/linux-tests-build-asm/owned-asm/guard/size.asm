
/var/tmp/cnetmod-pmtu-deadline-5afc506d/baseline-guard/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d1c0 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::size() const>:
  34d1c0:	55                   	push   %rbp
  34d1c1:	48 89 e5             	mov    %rsp,%rbp
  34d1c4:	48 8b 47 08          	mov    0x8(%rdi),%rax
  34d1c8:	48 8b 4f 58          	mov    0x58(%rdi),%rcx
  34d1cc:	48 03 4f 48          	add    0x48(%rdi),%rcx
  34d1d0:	48 2b 07             	sub    (%rdi),%rax
  34d1d3:	48 0f 44 c1          	cmove  %rcx,%rax
  34d1d7:	5d                   	pop    %rbp
  34d1d8:	c3                   	ret
  34d1d9:	cc                   	int3
  34d1da:	cc                   	int3
  34d1db:	cc                   	int3
  34d1dc:	cc                   	int3
  34d1dd:	cc                   	int3
  34d1de:	cc                   	int3
  34d1df:	cc                   	int3
