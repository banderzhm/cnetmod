
/var/tmp/cnetmod-h3-mixed-prefix-candidate-4a739b77e7584ead85cbf02304fb77ff/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

0000000000317f00 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const>:
  317f00:	55                   	push   %rbp
  317f01:	48 89 e5             	mov    %rsp,%rbp
  317f04:	41 57                	push   %r15
  317f06:	41 56                	push   %r14
  317f08:	41 55                	push   %r13
  317f0a:	41 54                	push   %r12
  317f0c:	53                   	push   %rbx
  317f0d:	48 83 ec 28          	sub    $0x28,%rsp
  317f11:	48 b9 aa aa aa aa aa 	movabs $0xaaaaaaaaaaaaaaa,%rcx
  317f18:	aa aa 0a 
  317f1b:	4c 8b 67 18          	mov    0x18(%rdi),%r12
  317f1f:	49 8b 34 24          	mov    (%r12),%rsi
  317f23:	4d 8b 7c 24 08       	mov    0x8(%r12),%r15
  317f28:	49 29 f7             	sub    %rsi,%r15
  317f2b:	4c 89 f8             	mov    %r15,%rax
  317f2e:	48 c1 f8 03          	sar    $0x3,%rax
  317f32:	48 ba ab aa aa aa aa 	movabs $0xaaaaaaaaaaaaaaab,%rdx
  317f39:	aa aa aa 
  317f3c:	48 0f af c2          	imul   %rdx,%rax
  317f40:	48 ff c0             	inc    %rax
  317f43:	48 39 c8             	cmp    %rcx,%rax
  317f46:	0f 87 d3 00 00 00    	ja     31801f <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const+0x11f>
  317f4c:	4d 8b 44 24 10       	mov    0x10(%r12),%r8
  317f51:	48 89 75 b8          	mov    %rsi,-0x48(%rbp)
  317f55:	49 29 f0             	sub    %rsi,%r8
  317f58:	4c 89 45 c8          	mov    %r8,-0x38(%rbp)
  317f5c:	4c 89 c6             	mov    %r8,%rsi
  317f5f:	48 c1 fe 03          	sar    $0x3,%rsi
  317f63:	48 0f af f2          	imul   %rdx,%rsi
  317f67:	48 8d 14 36          	lea    (%rsi,%rsi,1),%rdx
  317f6b:	48 39 c2             	cmp    %rax,%rdx
  317f6e:	48 0f 47 c2          	cmova  %rdx,%rax
  317f72:	48 ba 55 55 55 55 55 	movabs $0x555555555555555,%rdx
  317f79:	55 55 05 
  317f7c:	48 39 d6             	cmp    %rdx,%rsi
  317f7f:	48 0f 43 c1          	cmovae %rcx,%rax
  317f83:	48 39 c8             	cmp    %rcx,%rax
  317f86:	0f 87 98 00 00 00    	ja     318024 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const+0x124>
  317f8c:	48 8b 4f 08          	mov    0x8(%rdi),%rcx
  317f90:	48 89 7d c0          	mov    %rdi,-0x40(%rbp)
  317f94:	48 8b 57 10          	mov    0x10(%rdi),%rdx
  317f98:	48 89 55 d0          	mov    %rdx,-0x30(%rbp)
  317f9c:	4c 8b 31             	mov    (%rcx),%r14
  317f9f:	48 c1 e0 03          	shl    $0x3,%rax
  317fa3:	4c 8d 2c 40          	lea    (%rax,%rax,2),%r13
  317fa7:	4c 89 ef             	mov    %r13,%rdi
  317faa:	e8 81 2a dd ff       	call   eaa30 <_Znwm$plt>
  317faf:	48 89 c3             	mov    %rax,%rbx
  317fb2:	49 01 c5             	add    %rax,%r13
  317fb5:	4e 89 34 38          	mov    %r14,(%rax,%r15,1)
  317fb9:	48 8b 45 d0          	mov    -0x30(%rbp),%rax
  317fbd:	c5 f8 10 00          	vmovups (%rax),%xmm0
  317fc1:	c4 a1 78 11 44 3b 08 	vmovups %xmm0,0x8(%rbx,%r15,1)
  317fc8:	4a 8d 44 3b 18       	lea    0x18(%rbx,%r15,1),%rax
  317fcd:	48 89 45 d0          	mov    %rax,-0x30(%rbp)
  317fd1:	48 89 df             	mov    %rbx,%rdi
  317fd4:	4c 8b 75 b8          	mov    -0x48(%rbp),%r14
  317fd8:	4c 89 f6             	mov    %r14,%rsi
  317fdb:	4c 89 fa             	mov    %r15,%rdx
  317fde:	e8 bd 2e dd ff       	call   eaea0 <memcpy$plt>
  317fe3:	49 89 1c 24          	mov    %rbx,(%r12)
  317fe7:	48 8b 5d d0          	mov    -0x30(%rbp),%rbx
  317feb:	49 89 5c 24 08       	mov    %rbx,0x8(%r12)
  317ff0:	4d 89 6c 24 10       	mov    %r13,0x10(%r12)
  317ff5:	4d 85 f6             	test   %r14,%r14
  317ff8:	74 0c                	je     318006 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const+0x106>
  317ffa:	4c 89 f7             	mov    %r14,%rdi
  317ffd:	48 8b 75 c8          	mov    -0x38(%rbp),%rsi
  318001:	e8 ba 29 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  318006:	48 8b 45 c0          	mov    -0x40(%rbp),%rax
  31800a:	48 8b 00             	mov    (%rax),%rax
  31800d:	48 89 18             	mov    %rbx,(%rax)
  318010:	48 83 c4 28          	add    $0x28,%rsp
  318014:	5b                   	pop    %rbx
  318015:	41 5c                	pop    %r12
  318017:	41 5d                	pop    %r13
  318019:	41 5e                	pop    %r14
  31801b:	41 5f                	pop    %r15
  31801d:	5d                   	pop    %rbp
  31801e:	c3                   	ret
  31801f:	e8 0c 00 00 00       	call   318030 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::__throw_length_error[abi:nqe220108]()>
  318024:	e8 17 0c f9 ff       	call   2a8c40 <std::__throw_bad_array_new_length[abi:nqe220108]()>
  318029:	cc                   	int3
  31802a:	cc                   	int3
  31802b:	cc                   	int3
  31802c:	cc                   	int3
  31802d:	cc                   	int3
  31802e:	cc                   	int3
  31802f:	cc                   	int3
