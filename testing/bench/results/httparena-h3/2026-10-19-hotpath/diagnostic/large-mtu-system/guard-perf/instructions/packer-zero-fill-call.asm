
/var/tmp/cnetmod-h3-large-mtu-perf-c6333e93d31b45c689c0146783514018/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000030c710 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1520>:
  30c710:	83 fd 0b             	cmp    $0xb,%ebp
  30c713:	00 00                	add    %al,(%rax)
  30c715:	45 88 0e             	mov    %r9b,(%r14)
  30c718:	49 ff c6             	inc    %r14
  30c71b:	48 8b 9d 48 f9 ff ff 	mov    -0x6b8(%rbp),%rbx
  30c722:	48 8b 85 88 f9 ff ff 	mov    -0x678(%rbp),%rax
  30c729:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c730:	4c 2b b5 d0 fd ff ff 	sub    -0x230(%rbp),%r14
  30c737:	48 8b 10             	mov    (%rax),%rdx
  30c73a:	48 8b 48 08          	mov    0x8(%rax),%rcx
  30c73e:	48 29 d1             	sub    %rdx,%rcx
  30c741:	48 8b 85 80 f9 ff ff 	mov    -0x680(%rbp),%rax
  30c748:	8b 00                	mov    (%rax),%eax
  30c74a:	89 04 24             	mov    %eax,(%rsp)
  30c74d:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30c754:	4c 8d 85 d0 fd ff ff 	lea    -0x230(%rbp),%r8
  30c75b:	48 89 de             	mov    %rbx,%rsi
  30c75e:	e8 7d 34 03 00       	call   33fbe0 <cnetmod::quic::append_sealed_payload@cnetmod.protocol.quic(cnetmod::quic::quic_level_keys@cnetmod.protocol.quic const&, std::__1::span<std::byte const, 18446744073709551615ul>, std::__1::vector<std::byte, std::__1::allocator<std::byte> >&, unsigned long, unsigned int)>
  30c763:	80 bd 50 fd ff ff 00 	cmpb   $0x0,-0x2b0(%rbp)
  30c76a:	0f 84 44 04 00 00    	je     30cbb4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x19c4>
  30c770:	49 83 c6 fc          	add    $0xfffffffffffffffc,%r14
  30c774:	48 8b 95 d0 fd ff ff 	mov    -0x230(%rbp),%rdx
  30c77b:	48 8b 8d d8 fd ff ff 	mov    -0x228(%rbp),%rcx
  30c782:	48 29 d1             	sub    %rdx,%rcx
  30c785:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30c78c:	48 89 de             	mov    %rbx,%rsi
  30c78f:	4d 89 f0             	mov    %r14,%r8
  30c792:	45 31 c9             	xor    %r9d,%r9d
  30c795:	e8 56 2b 03 00       	call   33f2f0 <cnetmod::quic::protect_header@cnetmod.protocol.quic(cnetmod::quic::quic_level_keys@cnetmod.protocol.quic const&, std::__1::span<std::byte, 18446744073709551615ul>, unsigned long, bool)>
  30c79a:	80                   	.byte 0x80
  30c79b:	bd 50 fd ff ff       	mov    $0xfffffd50,%ebp
