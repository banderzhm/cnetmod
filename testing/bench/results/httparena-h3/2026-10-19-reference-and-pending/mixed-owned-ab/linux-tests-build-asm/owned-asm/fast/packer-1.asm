
/var/tmp/cnetmod-h3-owned-fast-candidate-fbaeb04a21614732b24a7fdae850fcd0/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

0000000000317e50 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const>:
  317e50:	55                   	push   %rbp
  317e51:	48 89 e5             	mov    %rsp,%rbp
  317e54:	41 57                	push   %r15
  317e56:	41 56                	push   %r14
  317e58:	41 55                	push   %r13
  317e5a:	41 54                	push   %r12
  317e5c:	53                   	push   %rbx
  317e5d:	48 83 ec 28          	sub    $0x28,%rsp
  317e61:	48 b9 aa aa aa aa aa 	movabs $0xaaaaaaaaaaaaaaa,%rcx
  317e68:	aa aa 0a 
  317e6b:	4c 8b 67 18          	mov    0x18(%rdi),%r12
  317e6f:	49 8b 34 24          	mov    (%r12),%rsi
  317e73:	4d 8b 7c 24 08       	mov    0x8(%r12),%r15
  317e78:	49 29 f7             	sub    %rsi,%r15
  317e7b:	4c 89 f8             	mov    %r15,%rax
  317e7e:	48 c1 f8 03          	sar    $0x3,%rax
  317e82:	48 ba ab aa aa aa aa 	movabs $0xaaaaaaaaaaaaaaab,%rdx
  317e89:	aa aa aa 
  317e8c:	48 0f af c2          	imul   %rdx,%rax
  317e90:	48 ff c0             	inc    %rax
  317e93:	48 39 c8             	cmp    %rcx,%rax
  317e96:	0f 87 d3 00 00 00    	ja     317f6f <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const+0x11f>
  317e9c:	4d 8b 44 24 10       	mov    0x10(%r12),%r8
  317ea1:	48 89 75 b8          	mov    %rsi,-0x48(%rbp)
  317ea5:	49 29 f0             	sub    %rsi,%r8
  317ea8:	4c 89 45 c8          	mov    %r8,-0x38(%rbp)
  317eac:	4c 89 c6             	mov    %r8,%rsi
  317eaf:	48 c1 fe 03          	sar    $0x3,%rsi
  317eb3:	48 0f af f2          	imul   %rdx,%rsi
  317eb7:	48 8d 14 36          	lea    (%rsi,%rsi,1),%rdx
  317ebb:	48 39 c2             	cmp    %rax,%rdx
  317ebe:	48 0f 47 c2          	cmova  %rdx,%rax
  317ec2:	48 ba 55 55 55 55 55 	movabs $0x555555555555555,%rdx
  317ec9:	55 55 05 
  317ecc:	48 39 d6             	cmp    %rdx,%rsi
  317ecf:	48 0f 43 c1          	cmovae %rcx,%rax
  317ed3:	48 39 c8             	cmp    %rcx,%rax
  317ed6:	0f 87 98 00 00 00    	ja     317f74 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const+0x124>
  317edc:	48 8b 4f 08          	mov    0x8(%rdi),%rcx
  317ee0:	48 89 7d c0          	mov    %rdi,-0x40(%rbp)
  317ee4:	48 8b 57 10          	mov    0x10(%rdi),%rdx
  317ee8:	48 89 55 d0          	mov    %rdx,-0x30(%rbp)
  317eec:	4c 8b 31             	mov    (%rcx),%r14
  317eef:	48 c1 e0 03          	shl    $0x3,%rax
  317ef3:	4c 8d 2c 40          	lea    (%rax,%rax,2),%r13
  317ef7:	4c 89 ef             	mov    %r13,%rdi
  317efa:	e8 31 2b dd ff       	call   eaa30 <_Znwm$plt>
  317eff:	48 89 c3             	mov    %rax,%rbx
  317f02:	49 01 c5             	add    %rax,%r13
  317f05:	4e 89 34 38          	mov    %r14,(%rax,%r15,1)
  317f09:	48 8b 45 d0          	mov    -0x30(%rbp),%rax
  317f0d:	c5 f8 10 00          	vmovups (%rax),%xmm0
  317f11:	c4 a1 78 11 44 3b 08 	vmovups %xmm0,0x8(%rbx,%r15,1)
  317f18:	4a 8d 44 3b 18       	lea    0x18(%rbx,%r15,1),%rax
  317f1d:	48 89 45 d0          	mov    %rax,-0x30(%rbp)
  317f21:	48 89 df             	mov    %rbx,%rdi
  317f24:	4c 8b 75 b8          	mov    -0x48(%rbp),%r14
  317f28:	4c 89 f6             	mov    %r14,%rsi
  317f2b:	4c 89 fa             	mov    %r15,%rdx
  317f2e:	e8 6d 2f dd ff       	call   eaea0 <memcpy$plt>
  317f33:	49 89 1c 24          	mov    %rbx,(%r12)
  317f37:	48 8b 5d d0          	mov    -0x30(%rbp),%rbx
  317f3b:	49 89 5c 24 08       	mov    %rbx,0x8(%r12)
  317f40:	4d 89 6c 24 10       	mov    %r13,0x10(%r12)
  317f45:	4d 85 f6             	test   %r14,%r14
  317f48:	74 0c                	je     317f56 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const+0x106>
  317f4a:	4c 89 f7             	mov    %r14,%rdi
  317f4d:	48 8b 75 c8          	mov    -0x38(%rbp),%rsi
  317f51:	e8 6a 2a dd ff       	call   ea9c0 <_ZdlPvm$plt>
  317f56:	48 8b 45 c0          	mov    -0x40(%rbp),%rax
  317f5a:	48 8b 00             	mov    (%rax),%rax
  317f5d:	48 89 18             	mov    %rbx,(%rax)
  317f60:	48 83 c4 28          	add    $0x28,%rsp
  317f64:	5b                   	pop    %rbx
  317f65:	41 5c                	pop    %r12
  317f67:	41 5d                	pop    %r13
  317f69:	41 5e                	pop    %r14
  317f6b:	41 5f                	pop    %r15
  317f6d:	5d                   	pop    %rbp
  317f6e:	c3                   	ret
  317f6f:	e8 0c 00 00 00       	call   317f80 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::__throw_length_error[abi:nqe220108]()>
  317f74:	e8 c7 0c f9 ff       	call   2a8c40 <std::__throw_bad_array_new_length[abi:nqe220108]()>
  317f79:	cc                   	int3
  317f7a:	cc                   	int3
  317f7b:	cc                   	int3
  317f7c:	cc                   	int3
  317f7d:	cc                   	int3
  317f7e:	cc                   	int3
  317f7f:	cc                   	int3
