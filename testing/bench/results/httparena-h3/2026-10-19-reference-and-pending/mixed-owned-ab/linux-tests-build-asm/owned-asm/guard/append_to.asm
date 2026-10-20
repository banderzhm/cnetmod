
/var/tmp/cnetmod-pmtu-deadline-5afc506d/baseline-guard/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d260 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const>:
  34d260:	55                   	push   %rbp
  34d261:	48 89 e5             	mov    %rsp,%rbp
  34d264:	41 56                	push   %r14
  34d266:	53                   	push   %rbx
  34d267:	48 89 f3             	mov    %rsi,%rbx
  34d26a:	48 8b 17             	mov    (%rdi),%rdx
  34d26d:	48 8b 4f 08          	mov    0x8(%rdi),%rcx
  34d271:	48 8b 76 08          	mov    0x8(%rsi),%rsi
  34d275:	48 39 ca             	cmp    %rcx,%rdx
  34d278:	74 08                	je     34d282 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x22>
  34d27a:	49 89 c8             	mov    %rcx,%r8
  34d27d:	49 29 d0             	sub    %rdx,%r8
  34d280:	eb 3d                	jmp    34d2bf <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x5f>
  34d282:	49 89 fe             	mov    %rdi,%r14
  34d285:	48 8d 57 28          	lea    0x28(%rdi),%rdx
  34d289:	4c 8b 47 48          	mov    0x48(%rdi),%r8
  34d28d:	4a 8d 0c 07          	lea    (%rdi,%r8,1),%rcx
  34d291:	48 83 c1 28          	add    $0x28,%rcx
  34d295:	48 89 df             	mov    %rbx,%rdi
  34d298:	e8 73 d2 f6 ff       	call   2ba510 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::byte const*, std::byte const*>(std::__1::__wrap_iter<std::byte const*>, std::byte const*, std::byte const*, long)>
  34d29d:	49 8b 56 18          	mov    0x18(%r14),%rdx
  34d2a1:	4d 8b 46 58          	mov    0x58(%r14),%r8
  34d2a5:	f6 02 01             	testb  $0x1,(%rdx)
  34d2a8:	74 06                	je     34d2b0 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x50>
  34d2aa:	48 8b 52 10          	mov    0x10(%rdx),%rdx
  34d2ae:	eb 03                	jmp    34d2b3 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x53>
  34d2b0:	48 ff c2             	inc    %rdx
  34d2b3:	49 03 56 50          	add    0x50(%r14),%rdx
  34d2b7:	48 8b 73 08          	mov    0x8(%rbx),%rsi
  34d2bb:	4a 8d 0c 02          	lea    (%rdx,%r8,1),%rcx
  34d2bf:	48 89 df             	mov    %rbx,%rdi
  34d2c2:	5b                   	pop    %rbx
  34d2c3:	41 5e                	pop    %r14
  34d2c5:	5d                   	pop    %rbp
  34d2c6:	e9 55 d8 f6 ff       	jmp    2bab20 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  34d2cb:	cc                   	int3
  34d2cc:	cc                   	int3
  34d2cd:	cc                   	int3
  34d2ce:	cc                   	int3
  34d2cf:	cc                   	int3
