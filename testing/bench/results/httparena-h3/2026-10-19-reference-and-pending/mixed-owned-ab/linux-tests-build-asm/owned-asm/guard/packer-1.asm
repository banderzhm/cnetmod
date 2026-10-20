
/var/tmp/cnetmod-pmtu-deadline-5afc506d/baseline-guard/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

0000000000317ba0 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const>:
  317ba0:	55                   	push   %rbp
  317ba1:	48 89 e5             	mov    %rsp,%rbp
  317ba4:	41 57                	push   %r15
  317ba6:	41 56                	push   %r14
  317ba8:	41 55                	push   %r13
  317baa:	41 54                	push   %r12
  317bac:	53                   	push   %rbx
  317bad:	48 83 ec 28          	sub    $0x28,%rsp
  317bb1:	48 b9 aa aa aa aa aa 	movabs $0xaaaaaaaaaaaaaaa,%rcx
  317bb8:	aa aa 0a 
  317bbb:	4c 8b 67 18          	mov    0x18(%rdi),%r12
  317bbf:	49 8b 34 24          	mov    (%r12),%rsi
  317bc3:	4d 8b 7c 24 08       	mov    0x8(%r12),%r15
  317bc8:	49 29 f7             	sub    %rsi,%r15
  317bcb:	4c 89 f8             	mov    %r15,%rax
  317bce:	48 c1 f8 03          	sar    $0x3,%rax
  317bd2:	48 ba ab aa aa aa aa 	movabs $0xaaaaaaaaaaaaaaab,%rdx
  317bd9:	aa aa aa 
  317bdc:	48 0f af c2          	imul   %rdx,%rax
  317be0:	48 ff c0             	inc    %rax
  317be3:	48 39 c8             	cmp    %rcx,%rax
  317be6:	0f 87 d3 00 00 00    	ja     317cbf <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const+0x11f>
  317bec:	4d 8b 44 24 10       	mov    0x10(%r12),%r8
  317bf1:	48 89 75 b8          	mov    %rsi,-0x48(%rbp)
  317bf5:	49 29 f0             	sub    %rsi,%r8
  317bf8:	4c 89 45 c8          	mov    %r8,-0x38(%rbp)
  317bfc:	4c 89 c6             	mov    %r8,%rsi
  317bff:	48 c1 fe 03          	sar    $0x3,%rsi
  317c03:	48 0f af f2          	imul   %rdx,%rsi
  317c07:	48 8d 14 36          	lea    (%rsi,%rsi,1),%rdx
  317c0b:	48 39 c2             	cmp    %rax,%rdx
  317c0e:	48 0f 47 c2          	cmova  %rdx,%rax
  317c12:	48 ba 55 55 55 55 55 	movabs $0x555555555555555,%rdx
  317c19:	55 55 05 
  317c1c:	48 39 d6             	cmp    %rdx,%rsi
  317c1f:	48 0f 43 c1          	cmovae %rcx,%rax
  317c23:	48 39 c8             	cmp    %rcx,%rax
  317c26:	0f 87 98 00 00 00    	ja     317cc4 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const+0x124>
  317c2c:	48 8b 4f 08          	mov    0x8(%rdi),%rcx
  317c30:	48 89 7d c0          	mov    %rdi,-0x40(%rbp)
  317c34:	48 8b 57 10          	mov    0x10(%rdi),%rdx
  317c38:	48 89 55 d0          	mov    %rdx,-0x30(%rbp)
  317c3c:	4c 8b 31             	mov    (%rcx),%r14
  317c3f:	48 c1 e0 03          	shl    $0x3,%rax
  317c43:	4c 8d 2c 40          	lea    (%rax,%rax,2),%r13
  317c47:	4c 89 ef             	mov    %r13,%rdi
  317c4a:	e8 c1 2d dd ff       	call   eaa10 <_Znwm$plt>
  317c4f:	48 89 c3             	mov    %rax,%rbx
  317c52:	49 01 c5             	add    %rax,%r13
  317c55:	4e 89 34 38          	mov    %r14,(%rax,%r15,1)
  317c59:	48 8b 45 d0          	mov    -0x30(%rbp),%rax
  317c5d:	c5 f8 10 00          	vmovups (%rax),%xmm0
  317c61:	c4 a1 78 11 44 3b 08 	vmovups %xmm0,0x8(%rbx,%r15,1)
  317c68:	4a 8d 44 3b 18       	lea    0x18(%rbx,%r15,1),%rax
  317c6d:	48 89 45 d0          	mov    %rax,-0x30(%rbp)
  317c71:	48 89 df             	mov    %rbx,%rdi
  317c74:	4c 8b 75 b8          	mov    -0x48(%rbp),%r14
  317c78:	4c 89 f6             	mov    %r14,%rsi
  317c7b:	4c 89 fa             	mov    %r15,%rdx
  317c7e:	e8 fd 31 dd ff       	call   eae80 <memcpy$plt>
  317c83:	49 89 1c 24          	mov    %rbx,(%r12)
  317c87:	48 8b 5d d0          	mov    -0x30(%rbp),%rbx
  317c8b:	49 89 5c 24 08       	mov    %rbx,0x8(%r12)
  317c90:	4d 89 6c 24 10       	mov    %r13,0x10(%r12)
  317c95:	4d 85 f6             	test   %r14,%r14
  317c98:	74 0c                	je     317ca6 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const+0x106>
  317c9a:	4c 89 f7             	mov    %r14,%rdi
  317c9d:	48 8b 75 c8          	mov    -0x38(%rbp),%rsi
  317ca1:	e8 fa 2c dd ff       	call   ea9a0 <_ZdlPvm$plt>
  317ca6:	48 8b 45 c0          	mov    -0x40(%rbp),%rax
  317caa:	48 8b 00             	mov    (%rax),%rax
  317cad:	48 89 18             	mov    %rbx,(%rax)
  317cb0:	48 83 c4 28          	add    $0x28,%rsp
  317cb4:	5b                   	pop    %rbx
  317cb5:	41 5c                	pop    %r12
  317cb7:	41 5d                	pop    %r13
  317cb9:	41 5e                	pop    %r14
  317cbb:	41 5f                	pop    %r15
  317cbd:	5d                   	pop    %rbp
  317cbe:	c3                   	ret
  317cbf:	e8 0c 00 00 00       	call   317cd0 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::__throw_length_error[abi:nqe220108]()>
  317cc4:	e8 37 0f f9 ff       	call   2a8c00 <std::__throw_bad_array_new_length[abi:nqe220108]()>
  317cc9:	cc                   	int3
  317cca:	cc                   	int3
  317ccb:	cc                   	int3
  317ccc:	cc                   	int3
  317ccd:	cc                   	int3
  317cce:	cc                   	int3
  317ccf:	cc                   	int3
