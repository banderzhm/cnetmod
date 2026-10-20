
/var/tmp/cnetmod-h3-owned-fast-candidate-fbaeb04a21614732b24a7fdae850fcd0/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d670 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const>:
  34d670:	55                   	push   %rbp
  34d671:	48 89 e5             	mov    %rsp,%rbp
  34d674:	41 56                	push   %r14
  34d676:	53                   	push   %rbx
  34d677:	48 89 f3             	mov    %rsi,%rbx
  34d67a:	49 89 fe             	mov    %rdi,%r14
  34d67d:	48 83 7f 18 00       	cmpq   $0x0,0x18(%rdi)
  34d682:	74 4d                	je     34d6d1 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x61>
  34d684:	49 8b 16             	mov    (%r14),%rdx
  34d687:	49 8b 4e 08          	mov    0x8(%r14),%rcx
  34d68b:	48 39 ca             	cmp    %rcx,%rdx
  34d68e:	74 12                	je     34d6a2 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x32>
  34d690:	48 8b 73 08          	mov    0x8(%rbx),%rsi
  34d694:	49 89 c8             	mov    %rcx,%r8
  34d697:	49 29 d0             	sub    %rdx,%r8
  34d69a:	48 89 df             	mov    %rbx,%rdi
  34d69d:	e8 be d4 f6 ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  34d6a2:	48 8b 73 08          	mov    0x8(%rbx),%rsi
  34d6a6:	49 8d 56 28          	lea    0x28(%r14),%rdx
  34d6aa:	4d 8b 46 48          	mov    0x48(%r14),%r8
  34d6ae:	4b 8d 0c 06          	lea    (%r14,%r8,1),%rcx
  34d6b2:	48 83 c1 28          	add    $0x28,%rcx
  34d6b6:	48 89 df             	mov    %rbx,%rdi
  34d6b9:	e8 92 ce f6 ff       	call   2ba550 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::byte const*, std::byte const*>(std::__1::__wrap_iter<std::byte const*>, std::byte const*, std::byte const*, long)>
  34d6be:	49 8b 56 18          	mov    0x18(%r14),%rdx
  34d6c2:	4d 8b 46 58          	mov    0x58(%r14),%r8
  34d6c6:	f6 02 01             	testb  $0x1,(%rdx)
  34d6c9:	74 19                	je     34d6e4 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x74>
  34d6cb:	48 8b 52 10          	mov    0x10(%rdx),%rdx
  34d6cf:	eb 16                	jmp    34d6e7 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x77>
  34d6d1:	48 8b 73 08          	mov    0x8(%rbx),%rsi
  34d6d5:	49 8b 16             	mov    (%r14),%rdx
  34d6d8:	49 8b 4e 08          	mov    0x8(%r14),%rcx
  34d6dc:	49 89 c8             	mov    %rcx,%r8
  34d6df:	49 29 d0             	sub    %rdx,%r8
  34d6e2:	eb 0f                	jmp    34d6f3 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x83>
  34d6e4:	48 ff c2             	inc    %rdx
  34d6e7:	49 03 56 50          	add    0x50(%r14),%rdx
  34d6eb:	48 8b 73 08          	mov    0x8(%rbx),%rsi
  34d6ef:	4a 8d 0c 02          	lea    (%rdx,%r8,1),%rcx
  34d6f3:	48 89 df             	mov    %rbx,%rdi
  34d6f6:	5b                   	pop    %rbx
  34d6f7:	41 5e                	pop    %r14
  34d6f9:	5d                   	pop    %rbp
  34d6fa:	e9 61 d4 f6 ff       	jmp    2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  34d6ff:	cc                   	int3
