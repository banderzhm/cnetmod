
/var/tmp/cnetmod-h3-large-mtu-perf-c6333e93d31b45c689c0146783514018/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000030bfaf <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xdbf>:
  30bfaf:	63 02                	movsxd (%rdx),%eax
  30bfb1:	00 00                	add    %al,(%rax)
  30bfb3:	66 66 66 66 2e 0f 1f 	data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
  30bfba:	84 00 00 00 00 00 
  30bfc0:	45 31 f6             	xor    %r14d,%r14d
  30bfc3:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bfca:	48 85 db             	test   %rbx,%rbx
  30bfcd:	0f 85 1d 02 00 00    	jne    30c1f0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1000>
  30bfd3:	e9 3e 02 00 00       	jmp    30c216 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1026>
  30bfd8:	48 ff c2             	inc    %rdx
  30bfdb:	4c 8b 85 28 fe ff ff 	mov    -0x1d8(%rbp),%r8
  30bfe2:	48 03 95 20 fe ff ff 	add    -0x1e0(%rbp),%rdx
  30bfe9:	49 8b 75 08          	mov    0x8(%r13),%rsi
  30bfed:	4a 8d 0c 02          	lea    (%rdx,%r8,1),%rcx
  30bff1:	4c 89 ef             	mov    %r13,%rdi
  30bff4:	c5 f8 77             	vzeroupper
  30bff7:	e8 24 eb fa ff       	call   2bab20 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30bffc:	4d 85 f6             	test   %r14,%r14
  30bfff:	0f 85 fb 00 00 00    	jne    30c100 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xf10>
  30c005:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
  30c00c:	48 3b 85 48 ff ff ff 	cmp    -0xb8(%rbp),%rax
  30c013:	75 0b                	jne    30c020 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe30>
  30c015:	48 83 7d 88 00       	cmpq   $0x0,-0x78(%rbp)
  30c01a:	75 04                	jne    30c020 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe30>
  30c01c:	31 c0                	xor    %eax,%eax
  30c01e:	eb 1d                	jmp    30c03d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe4d>
