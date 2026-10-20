
/var/tmp/cnetmod-h3-mixed-prefix-candidate-4a739b77e7584ead85cbf02304fb77ff/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000034d720 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const>:
  34d720:	55                   	push   %rbp
  34d721:	48 89 e5             	mov    %rsp,%rbp
  34d724:	41 56                	push   %r14
  34d726:	53                   	push   %rbx
  34d727:	48 89 f3             	mov    %rsi,%rbx
  34d72a:	49 89 fe             	mov    %rdi,%r14
  34d72d:	48 8b 17             	mov    (%rdi),%rdx
  34d730:	48 8b 4f 08          	mov    0x8(%rdi),%rcx
  34d734:	48 39 ca             	cmp    %rcx,%rdx
  34d737:	74 12                	je     34d74b <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x2b>
  34d739:	48 8b 73 08          	mov    0x8(%rbx),%rsi
  34d73d:	49 89 c8             	mov    %rcx,%r8
  34d740:	49 29 d0             	sub    %rdx,%r8
  34d743:	48 89 df             	mov    %rbx,%rdi
  34d746:	e8 15 d4 f6 ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  34d74b:	49 83 7e 18 00       	cmpq   $0x0,0x18(%r14)
  34d750:	74 2f                	je     34d781 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x61>
  34d752:	48 8b 73 08          	mov    0x8(%rbx),%rsi
  34d756:	49 8d 56 28          	lea    0x28(%r14),%rdx
  34d75a:	4d 8b 46 48          	mov    0x48(%r14),%r8
  34d75e:	4b 8d 0c 06          	lea    (%r14,%r8,1),%rcx
  34d762:	48 83 c1 28          	add    $0x28,%rcx
  34d766:	48 89 df             	mov    %rbx,%rdi
  34d769:	e8 e2 cd f6 ff       	call   2ba550 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::byte const*, std::byte const*>(std::__1::__wrap_iter<std::byte const*>, std::byte const*, std::byte const*, long)>
  34d76e:	49 8b 56 18          	mov    0x18(%r14),%rdx
  34d772:	4d 8b 46 58          	mov    0x58(%r14),%r8
  34d776:	f6 02 01             	testb  $0x1,(%rdx)
  34d779:	74 0b                	je     34d786 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x66>
  34d77b:	48 8b 52 10          	mov    0x10(%rdx),%rdx
  34d77f:	eb 08                	jmp    34d789 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const+0x69>
  34d781:	5b                   	pop    %rbx
  34d782:	41 5e                	pop    %r14
  34d784:	5d                   	pop    %rbp
  34d785:	c3                   	ret
  34d786:	48 ff c2             	inc    %rdx
  34d789:	49 03 56 50          	add    0x50(%r14),%rdx
  34d78d:	48 8b 73 08          	mov    0x8(%rbx),%rsi
  34d791:	4a 8d 0c 02          	lea    (%rdx,%r8,1),%rcx
  34d795:	48 89 df             	mov    %rbx,%rdi
  34d798:	5b                   	pop    %rbx
  34d799:	41 5e                	pop    %r14
  34d79b:	5d                   	pop    %rbp
  34d79c:	e9 bf d3 f6 ff       	jmp    2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  34d7a1:	cc                   	int3
  34d7a2:	cc                   	int3
  34d7a3:	cc                   	int3
  34d7a4:	cc                   	int3
  34d7a5:	cc                   	int3
  34d7a6:	cc                   	int3
  34d7a7:	cc                   	int3
  34d7a8:	cc                   	int3
  34d7a9:	cc                   	int3
  34d7aa:	cc                   	int3
  34d7ab:	cc                   	int3
  34d7ac:	cc                   	int3
  34d7ad:	cc                   	int3
  34d7ae:	cc                   	int3
  34d7af:	cc                   	int3
