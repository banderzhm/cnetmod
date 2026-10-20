
/var/tmp/cnetmod-h3-large-mtu-perf-c6333e93d31b45c689c0146783514018/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

0000000000312460 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::apply_stream_write(unsigned long, std::__1::vector<std::byte, std::__1::allocator<std::byte> > const&, std::__1::shared_ptr<std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const> const&, bool)+0xe60>:
  312460:	31 c0                	xor    %eax,%eax
  312462:	48 8b 8d 38 fd ff ff 	mov    -0x2c8(%rbp),%rcx
  312469:	4a 8d 14 21          	lea    (%rcx,%r12,1),%rdx
  31246d:	4c 8b 85 20 fd ff ff 	mov    -0x2e0(%rbp),%r8
  312474:	49 29 d0             	sub    %rdx,%r8
  312477:	48 8d 9d 30 ff ff ff 	lea    -0xd0(%rbp),%rbx
  31247e:	48 89 df             	mov    %rbx,%rdi
  312481:	48 89 c6             	mov    %rax,%rsi
  312484:	48 8b 8d d8 fc ff ff 	mov    -0x328(%rbp),%rcx
  31248b:	c5 f8 77             	vzeroupper
  31248e:	e8 8d 86 fa ff       	call   2bab20 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  312493:	48 8b b5 38 ff ff ff 	mov    -0xc8(%rbp),%rsi
  31249a:	4d 01 f4             	add    %r14,%r12
  31249d:	48 8b 85 e8 fc ff ff 	mov    -0x318(%rbp),%rax
  3124a4:	4a 8d 0c 20          	lea    (%rax,%r12,1),%rcx
  3124a8:	4c 01 f9             	add    %r15,%rcx
  3124ab:	4c 03 a5 e0 fc ff ff 	add    -0x320(%rbp),%r12
  3124b2:	4d 01 fc             	add    %r15,%r12
  3124b5:	48 89 df             	mov    %rbx,%rdi
  3124b8:	48 8b 95 10 fd ff ff 	mov    -0x2f0(%rbp),%rdx
  3124bf:	4d 89 e0             	mov    %r12,%r8
  3124c2:	e8 59 86 fa ff       	call   2bab20 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  3124c7:	4c 8b a5 30 ff ff ff 	mov    -0xd0(%rbp),%r12
  3124ce:	48 8b bd 38 ff ff ff 	mov    -0xc8(%rbp),%rdi
  3124d5:	4c 29 e7             	sub    %r12,%rdi
  3124d8:	48 8b b5 a8 fd ff ff 	mov    -0x258(%rbp),%rsi
  3124df:	48 8d 9d 50 ff ff ff 	lea    -0xb0(%rbp),%rbx
  3124e6:	48 8b 85 60 fd ff ff 	mov    -0x2a0(%rbp),%rax
  3124ed:	0f b6 95 50 fd ff ff 	movzbl -0x2b0(%rbp),%edx
  3124f4:	48 89 b5 50 ff ff ff 	mov    %rsi,-0xb0(%rbp)
  3124fb:	48 89 85 58 ff ff ff 	mov    %rax,-0xa8(%rbp)
  312502:	4c 89 a5 60 ff ff ff 	mov    %r12,-0xa0(%rbp)
  312509:	48 89 bd 68 ff ff ff 	mov    %rdi,-0x98(%rbp)
  312510:	88 95 70 ff ff ff    	mov    %dl,-0x90(%rbp)
  312516:	48 8d bd 70 fd ff ff 	lea    -0x290(%rbp),%rdi
  31251d:	48 89 de             	mov    %rbx,%rsi
  312520:	c5 f8 77             	vzeroupper
  312523:	e8 78 41 03 00       	call   3466a0 <cnetmod::quic::encode_frame@cnetmod.protocol.quic(cnetmod::quic::stream_frame@cnetmod.protocol.quic const&)>
  312528:	48 8b 85 58 fd ff ff 	mov    -0x2a8(%rbp),%rax
  31252f:	4c 8b 30             	mov    (%rax),%r14
  312532:	80 bd 6c fd ff ff 00 	cmpb   $0x0,-0x294(%rbp)
  312539:	0f 84 c5 00 00 00    	je     312604 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::apply_stream_write(unsigned long, std::__1::vector<std::byte, std::__1::allocator<std::byte> > const&, std::__1::shared_ptr<std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const> const&, bool)+0x1004>
  31253f:	49                   	rex.WB
