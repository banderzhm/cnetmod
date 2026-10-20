
/var/tmp/cnetmod-h3-mixed-prefix-candidate-4a739b77e7584ead85cbf02304fb77ff/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000030b240 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)>:
  30b240:	55                   	push   %rbp
  30b241:	48 89 e5             	mov    %rsp,%rbp
  30b244:	41 57                	push   %r15
  30b246:	41 56                	push   %r14
  30b248:	41 55                	push   %r13
  30b24a:	41 54                	push   %r12
  30b24c:	53                   	push   %rbx
  30b24d:	48 81 ec b8 06 00 00 	sub    $0x6b8,%rsp
  30b254:	48 89 8d 78 f9 ff ff 	mov    %rcx,-0x688(%rbp)
  30b25b:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30b262:	00 00 
  30b264:	48 89 45 d0          	mov    %rax,-0x30(%rbp)
  30b268:	48 8b 0e             	mov    (%rsi),%rcx
  30b26b:	48 8b 81 58 03 00 00 	mov    0x358(%rcx),%rax
  30b272:	48 85 c0             	test   %rax,%rax
  30b275:	0f 84 2f 01 00 00    	je     30b3aa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b27b:	49 89 f4             	mov    %rsi,%r12
  30b27e:	4c 8d 98 00 04 00 00 	lea    0x400(%rax),%r11
  30b285:	48 8b b0 08 04 00 00 	mov    0x408(%rax),%rsi
  30b28c:	45 31 f6             	xor    %r14d,%r14d
  30b28f:	48 3b b0 10 04 00 00 	cmp    0x410(%rax),%rsi
  30b296:	4d 0f 45 f3          	cmovne %r11,%r14
  30b29a:	0f 84 0a 01 00 00    	je     30b3aa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b2a0:	4c 8b 89 80 0e 00 00 	mov    0xe80(%rcx),%r9
  30b2a7:	4d 85 c9             	test   %r9,%r9
  30b2aa:	74 2d                	je     30b2d9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x99>
  30b2ac:	8b b1 a8 0e 00 00    	mov    0xea8(%rcx),%esi
  30b2b2:	eb 14                	jmp    30b2c8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x88>
  30b2b4:	66 66 66 2e 0f 1f 84 	data16 data16 cs nopw 0x0(%rax,%rax,1)
  30b2bb:	00 00 00 00 00 
  30b2c0:	4d 8b 09             	mov    (%r9),%r9
  30b2c3:	4d 85 c9             	test   %r9,%r9
  30b2c6:	74 11                	je     30b2d9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x99>
  30b2c8:	41 3b 71 20          	cmp    0x20(%r9),%esi
  30b2cc:	72 f2                	jb     30b2c0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x80>
  30b2ce:	74 28                	je     30b2f8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xb8>
  30b2d0:	4d 8b 49 08          	mov    0x8(%r9),%r9
  30b2d4:	4d 85 c9             	test   %r9,%r9
  30b2d7:	75 ef                	jne    30b2c8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x88>
  30b2d9:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30b2e0:	00 00 
  30b2e2:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30b2e6:	0f 85 a6 23 00 00    	jne    30d692 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2452>
  30b2ec:	48 8d 3d 9e 1d dd ff 	lea    -0x22e262(%rip),%rdi        # dd091 <cnetmod::quic::max_udp_receive_payload@cnetmod.protocol.quic+0x3181>
  30b2f3:	e8 48 ae fa ff       	call   2b6140 <std::__1::__throw_out_of_range[abi:nqe220108](char const*)>
  30b2f8:	4d 8b 51 28          	mov    0x28(%r9),%r10
  30b2fc:	4d 8d 4a 32          	lea    0x32(%r10),%r9
  30b300:	41 0f b6 72 47       	movzbl 0x47(%r10),%esi
  30b305:	4c 89 cb             	mov    %r9,%rbx
  30b308:	40 84 f6             	test   %sil,%sil
  30b30b:	74 79                	je     30b386 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x146>
  30b30d:	41 80 ba 83 00 00 00 	cmpb   $0x0,0x83(%r10)
  30b314:	00 
  30b315:	0f 85 8f 00 00 00    	jne    30b3aa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b31b:	41 80 ba 84 00 00 00 	cmpb   $0x1,0x84(%r10)
  30b322:	01 
  30b323:	0f 84 81 00 00 00    	je     30b3aa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b329:	4c 89 b5 50 f9 ff ff 	mov    %r14,-0x6b0(%rbp)
  30b330:	48 89 9d 68 f9 ff ff 	mov    %rbx,-0x698(%rbp)
  30b337:	89 95 74 f9 ff ff    	mov    %edx,-0x68c(%rbp)
  30b33d:	48 89 bd 90 f9 ff ff 	mov    %rdi,-0x670(%rbp)
  30b344:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b348:	c5 f9 7f 85 d0 f9 ff 	vmovdqa %xmm0,-0x630(%rbp)
  30b34f:	ff 
  30b350:	48 c7 85 e0 f9 ff ff 	movq   $0x0,-0x620(%rbp)
  30b357:	00 00 00 00 
  30b35b:	4d 85 c0             	test   %r8,%r8
  30b35e:	4c 8d b5 d0 f9 ff ff 	lea    -0x630(%rbp),%r14
  30b365:	4c 89 c7             	mov    %r8,%rdi
  30b368:	49 0f 44 fe          	cmove  %r14,%rdi
  30b36c:	48 8b 1f             	mov    (%rdi),%rbx
  30b36f:	48 89 5f 08          	mov    %rbx,0x8(%rdi)
  30b373:	40 84 f6             	test   %sil,%sil
  30b376:	74 6b                	je     30b3e3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a3>
  30b378:	41 0f b6 51 14       	movzbl 0x14(%r9),%edx
  30b37d:	48 83 c2 05          	add    $0x5,%rdx
  30b381:	e9 80 00 00 00       	jmp    30b406 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c6>
  30b386:	41 83 3a 00          	cmpl   $0x0,(%r10)
  30b38a:	75 1e                	jne    30b3aa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b38c:	80 b9 8f 01 00 00 01 	cmpb   $0x1,0x18f(%rcx)
  30b393:	75 15                	jne    30b3aa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b395:	48 8d 99 7a 01 00 00 	lea    0x17a(%rcx),%rbx
  30b39c:	41 80 ba 83 00 00 00 	cmpb   $0x0,0x83(%r10)
  30b3a3:	00 
  30b3a4:	0f 84 71 ff ff ff    	je     30b31b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xdb>
  30b3aa:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b3ae:	c5 fa 7f 07          	vmovdqu %xmm0,(%rdi)
  30b3b2:	48 c7 47 10 00 00 00 	movq   $0x0,0x10(%rdi)
  30b3b9:	00 
  30b3ba:	c6 47 18 01          	movb   $0x1,0x18(%rdi)
  30b3be:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30b3c5:	00 00 
  30b3c7:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30b3cb:	0f 85 c1 22 00 00    	jne    30d692 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2452>
  30b3d1:	48 81 c4 b8 06 00 00 	add    $0x6b8,%rsp
  30b3d8:	5b                   	pop    %rbx
  30b3d9:	41 5c                	pop    %r12
  30b3db:	41 5d                	pop    %r13
  30b3dd:	41 5e                	pop    %r14
  30b3df:	41 5f                	pop    %r15
  30b3e1:	5d                   	pop    %rbp
  30b3e2:	c3                   	ret
  30b3e3:	ba 19 00 00 00       	mov    $0x19,%edx
  30b3e8:	41 83 3a 00          	cmpl   $0x0,(%r10)
  30b3ec:	75 18                	jne    30b406 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c6>
  30b3ee:	80 b9 8f 01 00 00 01 	cmpb   $0x1,0x18f(%rcx)
  30b3f5:	75 0f                	jne    30b406 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c6>
  30b3f7:	48 81 c1 7a 01 00 00 	add    $0x17a,%rcx
  30b3fe:	49 89 c9             	mov    %rcx,%r9
  30b401:	e9 72 ff ff ff       	jmp    30b378 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x138>
  30b406:	48 89 bd 88 f9 ff ff 	mov    %rdi,-0x678(%rbp)
  30b40d:	4c 89 9d 48 f9 ff ff 	mov    %r11,-0x6b8(%rbp)
  30b414:	48 03 90 78 04 00 00 	add    0x478(%rax),%rdx
  30b41b:	4c 89 95 80 f9 ff ff 	mov    %r10,-0x680(%rbp)
  30b422:	49 8b 82 58 1c 00 00 	mov    0x1c58(%r10),%rax
  30b429:	45 31 ff             	xor    %r15d,%r15d
  30b42c:	48 29 d0             	sub    %rdx,%rax
  30b42f:	4c 0f 43 f8          	cmovae %rax,%r15
  30b433:	4d 85 c0             	test   %r8,%r8
  30b436:	4d 0f 45 f0          	cmovne %r8,%r14
  30b43a:	4d 8b 6e 10          	mov    0x10(%r14),%r13
  30b43e:	49 29 dd             	sub    %rbx,%r13
  30b441:	4d 39 ef             	cmp    %r13,%r15
  30b444:	4c 89 bd 98 f9 ff ff 	mov    %r15,-0x668(%rbp)
  30b44b:	76 44                	jbe    30b491 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x251>
  30b44d:	4d 85 ff             	test   %r15,%r15
  30b450:	0f 88 ad 1f 00 00    	js     30d403 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x21c3>
  30b456:	48 8b bd 98 f9 ff ff 	mov    -0x668(%rbp),%rdi
  30b45d:	e8 ce f5 dd ff       	call   eaa30 <_Znwm$plt>
  30b462:	48 89 c1             	mov    %rax,%rcx
  30b465:	48 8b 95 88 f9 ff ff 	mov    -0x678(%rbp),%rdx
  30b46c:	48 89 02             	mov    %rax,(%rdx)
  30b46f:	4c 8b bd 98 f9 ff ff 	mov    -0x668(%rbp),%r15
  30b476:	4c 01 f9             	add    %r15,%rcx
  30b479:	48 89 42 08          	mov    %rax,0x8(%rdx)
  30b47d:	49 89 4e 10          	mov    %rcx,0x10(%r14)
  30b481:	48 85 db             	test   %rbx,%rbx
  30b484:	74 0b                	je     30b491 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x251>
  30b486:	48 89 df             	mov    %rbx,%rdi
  30b489:	4c 89 ee             	mov    %r13,%rsi
  30b48c:	e8 2f f5 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b491:	48 8b 85 78 f9 ff ff 	mov    -0x688(%rbp),%rax
  30b498:	48 8b 18             	mov    (%rax),%rbx
  30b49b:	4c 8b 68 10          	mov    0x10(%rax),%r13
  30b49f:	48 89 58 08          	mov    %rbx,0x8(%rax)
  30b4a3:	48 8b 85 68 f9 ff ff 	mov    -0x698(%rbp),%rax
  30b4aa:	0f b6 40 14          	movzbl 0x14(%rax),%eax
  30b4ae:	48 8b 8d 50 f9 ff ff 	mov    -0x6b0(%rbp),%rcx
  30b4b5:	48 8b 49 78          	mov    0x78(%rcx),%rcx
  30b4b9:	4c 01 f8             	add    %r15,%rax
  30b4bc:	4c 8d 3c 01          	lea    (%rcx,%rax,1),%r15
  30b4c0:	49 83 c7 05          	add    $0x5,%r15
  30b4c4:	49 29 dd             	sub    %rbx,%r13
  30b4c7:	4d 39 ef             	cmp    %r13,%r15
  30b4ca:	76 36                	jbe    30b502 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2c2>
  30b4cc:	4d 85 ff             	test   %r15,%r15
  30b4cf:	0f 88 2e 1f 00 00    	js     30d403 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x21c3>
  30b4d5:	4c 89 ff             	mov    %r15,%rdi
  30b4d8:	e8 53 f5 dd ff       	call   eaa30 <_Znwm$plt>
  30b4dd:	48 8b 8d 78 f9 ff ff 	mov    -0x688(%rbp),%rcx
  30b4e4:	48 89 01             	mov    %rax,(%rcx)
  30b4e7:	49 01 c7             	add    %rax,%r15
  30b4ea:	48 89 41 08          	mov    %rax,0x8(%rcx)
  30b4ee:	4c 89 79 10          	mov    %r15,0x10(%rcx)
  30b4f2:	48 85 db             	test   %rbx,%rbx
  30b4f5:	74 0b                	je     30b502 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2c2>
  30b4f7:	48 89 df             	mov    %rbx,%rdi
  30b4fa:	4c 89 ee             	mov    %r13,%rsi
  30b4fd:	e8 be f4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b502:	49 8b 04 24          	mov    (%r12),%rax
  30b506:	80 b8 82 06 00 00 01 	cmpb   $0x1,0x682(%rax)
  30b50d:	4c 8b b5 80 f9 ff ff 	mov    -0x680(%rbp),%r14
  30b514:	4c 89 f3             	mov    %r14,%rbx
  30b517:	4c 8b bd 88 f9 ff ff 	mov    -0x678(%rbp),%r15
  30b51e:	75 71                	jne    30b591 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x351>
  30b520:	48 8b 88 78 0e 00 00 	mov    0xe78(%rax),%rcx
  30b527:	48 05 80 0e 00 00    	add    $0xe80,%rax
  30b52d:	4c 89 f3             	mov    %r14,%rbx
  30b530:	48 39 c1             	cmp    %rax,%rcx
  30b533:	75 10                	jne    30b545 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x305>
  30b535:	eb 5a                	jmp    30b591 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x351>
  30b537:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
  30b53e:	00 00 
  30b540:	48 39 c1             	cmp    %rax,%rcx
  30b543:	74 49                	je     30b58e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x34e>
  30b545:	48 8b 51 28          	mov    0x28(%rcx),%rdx
  30b549:	48 83 ba d0 00 00 00 	cmpq   $0x0,0xd0(%rdx)
  30b550:	00 
  30b551:	75 2b                	jne    30b57e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x33e>
  30b553:	48 8b 51 08          	mov    0x8(%rcx),%rdx
  30b557:	48 85 d2             	test   %rdx,%rdx
  30b55a:	74 14                	je     30b570 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x330>
  30b55c:	0f 1f 40 00          	nopl   0x0(%rax)
  30b560:	48 89 d1             	mov    %rdx,%rcx
  30b563:	48 8b 12             	mov    (%rdx),%rdx
  30b566:	48 85 d2             	test   %rdx,%rdx
  30b569:	75 f5                	jne    30b560 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x320>
  30b56b:	eb d3                	jmp    30b540 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x300>
  30b56d:	0f 1f 00             	nopl   (%rax)
  30b570:	48 89 ca             	mov    %rcx,%rdx
  30b573:	48 8b 49 10          	mov    0x10(%rcx),%rcx
  30b577:	48 3b 11             	cmp    (%rcx),%rdx
  30b57a:	75 f4                	jne    30b570 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x330>
  30b57c:	eb c2                	jmp    30b540 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x300>
  30b57e:	8b 71 20             	mov    0x20(%rcx),%esi
  30b581:	48 8b 38             	mov    (%rax),%rdi
  30b584:	e8 37 74 ff ff       	call   3029c0 <std::__1::map<unsigned int, std::__1::unique_ptr<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state, std::__1::default_delete<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state> >, std::__1::less<unsigned int>, std::__1::allocator<std::__1::pair<unsigned int const, std::__1::unique_ptr<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state, std::__1::default_delete<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state> > > > >::at(unsigned int const&)>
  30b589:	48 8b 18             	mov    (%rax),%rbx
  30b58c:	eb 03                	jmp    30b591 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x351>
  30b58e:	4c 89 f3             	mov    %r14,%rbx
  30b591:	48 8d b3 c0 00 00 00 	lea    0xc0(%rbx),%rsi
  30b598:	48 8d bd 30 fa ff ff 	lea    -0x5d0(%rbp),%rdi
  30b59f:	48 89 b5 40 f9 ff ff 	mov    %rsi,-0x6c0(%rbp)
  30b5a6:	e8 05 5a 00 00       	call   310fb0 <cnetmod::quic::(anonymous namespace)::take_ack_frame(std::__1::set<unsigned long, std::__1::less<unsigned long>, std::__1::allocator<unsigned long> >&)>
  30b5ab:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30b5b2:	0f 85 b3 00 00 00    	jne    30b66b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x42b>
  30b5b8:	49 8b 04 24          	mov    (%r12),%rax
  30b5bc:	80 b8 82 06 00 00 01 	cmpb   $0x1,0x682(%rax)
  30b5c3:	75 52                	jne    30b617 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x3d7>
  30b5c5:	8b 03                	mov    (%rbx),%eax
  30b5c7:	89 85 40 ff ff ff    	mov    %eax,-0xc0(%rbp)
  30b5cd:	48 8d bd 48 ff ff ff 	lea    -0xb8(%rbp),%rdi
  30b5d4:	48 8d b5 30 fa ff ff 	lea    -0x5d0(%rbp),%rsi
  30b5db:	e8 50 5f 00 00       	call   311530 <cnetmod::quic::ack_frame@cnetmod.protocol.quic::ack_frame(cnetmod::quic::ack_frame@cnetmod.protocol.quic const&)>
  30b5e0:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30b5e7:	48 8d b5 40 ff ff ff 	lea    -0xc0(%rbp),%rsi
  30b5ee:	e8 ad bc 03 00       	call   3472a0 <cnetmod::quic::encode_frame@cnetmod.protocol.quic(cnetmod::quic::path_ack_frame@cnetmod.protocol.quic const&)>
  30b5f3:	48 8b bd 68 ff ff ff 	mov    -0x98(%rbp),%rdi
  30b5fa:	48 85 ff             	test   %rdi,%rdi
  30b5fd:	74 2b                	je     30b62a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x3ea>
  30b5ff:	48 89 bd 70 ff ff ff 	mov    %rdi,-0x90(%rbp)
  30b606:	48 8b b5 78 ff ff ff 	mov    -0x88(%rbp),%rsi
  30b60d:	48 29 fe             	sub    %rdi,%rsi
  30b610:	e8 ab f3 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b615:	eb 13                	jmp    30b62a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x3ea>
  30b617:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30b61e:	48 8d b5 30 fa ff ff 	lea    -0x5d0(%rbp),%rsi
  30b625:	e8 26 b0 03 00       	call   346650 <cnetmod::quic::encode_frame@cnetmod.protocol.quic(cnetmod::quic::ack_frame@cnetmod.protocol.quic const&)>
  30b62a:	49 8b 77 08          	mov    0x8(%r15),%rsi
  30b62e:	4c 89 ff             	mov    %r15,%rdi
  30b631:	4c 8b ad 40 fd ff ff 	mov    -0x2c0(%rbp),%r13
  30b638:	48 8b 8d 48 fd ff ff 	mov    -0x2b8(%rbp),%rcx
  30b63f:	49 89 c8             	mov    %rcx,%r8
  30b642:	4d 29 e8             	sub    %r13,%r8
  30b645:	4c 89 ea             	mov    %r13,%rdx
  30b648:	e8 13 f5 fa ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30b64d:	4d 85 ed             	test   %r13,%r13
  30b650:	74 19                	je     30b66b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x42b>
  30b652:	4c 89 ad 48 fd ff ff 	mov    %r13,-0x2b8(%rbp)
  30b659:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30b660:	4c 29 ee             	sub    %r13,%rsi
  30b663:	4c 89 ef             	mov    %r13,%rdi
  30b666:	e8 55 f3 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b66b:	c6 45 a8 00          	movb   $0x0,-0x58(%rbp)
  30b66f:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b673:	c5 fe 7f 85 40 ff ff 	vmovdqu %ymm0,-0xc0(%rbp)
  30b67a:	ff 
  30b67b:	c5 fe 7f 85 60 ff ff 	vmovdqu %ymm0,-0xa0(%rbp)
  30b682:	ff 
  30b683:	c5 fe 7f 45 80       	vmovdqu %ymm0,-0x80(%rbp)
  30b688:	c6 45 a0 00          	movb   $0x0,-0x60(%rbp)
  30b68c:	66 c7 45 b0 03 00    	movw   $0x3,-0x50(%rbp)
  30b692:	c6 45 b2 00          	movb   $0x0,-0x4e(%rbp)
  30b696:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b69a:	c5 fa 7f 45 b8       	vmovdqu %xmm0,-0x48(%rbp)
  30b69f:	48 c7 45 c8 00 00 00 	movq   $0x0,-0x38(%rbp)
  30b6a6:	00 
  30b6a7:	c6 85 10 fa ff ff 00 	movb   $0x0,-0x5f0(%rbp)
  30b6ae:	c6 85 28 fa ff ff 00 	movb   $0x0,-0x5d8(%rbp)
  30b6b5:	c5 f9 7f 85 b0 f9 ff 	vmovdqa %xmm0,-0x650(%rbp)
  30b6bc:	ff 
  30b6bd:	48 c7 85 c0 f9 ff ff 	movq   $0x0,-0x640(%rbp)
  30b6c4:	00 00 00 00 
  30b6c8:	49 8b 86 60 1c 00 00 	mov    0x1c60(%r14),%rax
  30b6cf:	48 89 85 58 f9 ff ff 	mov    %rax,-0x6a8(%rbp)
  30b6d6:	41 0f b6 8e 68 1c 00 	movzbl 0x1c68(%r14),%ecx
  30b6dd:	00 
  30b6de:	41 8b 86 6c 1c 00 00 	mov    0x1c6c(%r14),%eax
  30b6e5:	89 85 0b fa ff ff    	mov    %eax,-0x5f5(%rbp)
  30b6eb:	41 8b 86 69 1c 00 00 	mov    0x1c69(%r14),%eax
  30b6f2:	89 85 08 fa ff ff    	mov    %eax,-0x5f8(%rbp)
  30b6f8:	88 8d af f9 ff ff    	mov    %cl,-0x651(%rbp)
  30b6fe:	80 f9 01             	cmp    $0x1,%cl
  30b701:	48 8b 9d 98 f9 ff ff 	mov    -0x668(%rbp),%rbx
  30b708:	75 3f                	jne    30b749 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x509>
  30b70a:	bf 01 00 00 00       	mov    $0x1,%edi
  30b70f:	c5 f8 77             	vzeroupper
  30b712:	e8 19 f3 dd ff       	call   eaa30 <_Znwm$plt>
  30b717:	49 89 c5             	mov    %rax,%r13
  30b71a:	48 89 c1             	mov    %rax,%rcx
  30b71d:	48 ff c1             	inc    %rcx
  30b720:	c6 00 01             	movb   $0x1,(%rax)
  30b723:	48 8b bd 88 f9 ff ff 	mov    -0x678(%rbp),%rdi
  30b72a:	48 8b 77 08          	mov    0x8(%rdi),%rsi
  30b72e:	41 b8 01 00 00 00    	mov    $0x1,%r8d
  30b734:	48 89 c2             	mov    %rax,%rdx
  30b737:	e8 24 f4 fa ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30b73c:	be 01 00 00 00       	mov    $0x1,%esi
  30b741:	4c 89 ef             	mov    %r13,%rdi
  30b744:	e8 77 f2 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b749:	49 8b 04 24          	mov    (%r12),%rax
  30b74d:	83 78 60 01          	cmpl   $0x1,0x60(%rax)
  30b751:	0f 85 6f 02 00 00    	jne    30b9c6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x786>
  30b757:	48 83 b8 50 2c 00 00 	cmpq   $0x0,0x2c50(%rax)
  30b75e:	00 
  30b75f:	0f 84 f4 00 00 00    	je     30b859 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x619>
  30b765:	48 8d b8 28 2c 00 00 	lea    0x2c28(%rax),%rdi
  30b76c:	48 8b 88 30 2c 00 00 	mov    0x2c30(%rax),%rcx
  30b773:	48 8b 90 48 2c 00 00 	mov    0x2c48(%rax),%rdx
  30b77a:	48 b8 c1 c0 c0 c0 c0 	movabs $0xc0c0c0c0c0c0c0c1,%rax
  30b781:	c0 c0 c0 
  30b784:	c4 e2 fb f6 c0       	mulx   %rax,%rax,%rax
  30b789:	48 c1 e8 07          	shr    $0x7,%rax
  30b78d:	48 8b 0c c1          	mov    (%rcx,%rax,8),%rcx
  30b791:	48 69 c0 aa 00 00 00 	imul   $0xaa,%rax,%rax
  30b798:	48 29 c2             	sub    %rax,%rdx
  30b79b:	48 8d 04 52          	lea    (%rdx,%rdx,2),%rax
  30b79f:	48 8b 1c c1          	mov    (%rcx,%rax,8),%rbx
  30b7a3:	4c 8b 7c c1 08       	mov    0x8(%rcx,%rax,8),%r15
  30b7a8:	4c 8b 6c c1 10       	mov    0x10(%rcx,%rax,8),%r13
  30b7ad:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b7b1:	c5 fa 7f 04 c1       	vmovdqu %xmm0,(%rcx,%rax,8)
  30b7b6:	48 c7 44 c1 10 00 00 	movq   $0x0,0x10(%rcx,%rax,8)
  30b7bd:	00 00 
  30b7bf:	c5 f8 77             	vzeroupper
  30b7c2:	e8 59 4b 00 00       	call   310320 <std::__1::deque<std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::allocator<std::__1::vector<std::byte, std::__1::allocator<std::byte> > > >::pop_front()>
  30b7c7:	48 8b bd 88 f9 ff ff 	mov    -0x678(%rbp),%rdi
  30b7ce:	48 8b 77 08          	mov    0x8(%rdi),%rsi
  30b7d2:	4d 89 f8             	mov    %r15,%r8
  30b7d5:	49 29 d8             	sub    %rbx,%r8
  30b7d8:	48 89 da             	mov    %rbx,%rdx
  30b7db:	4c 89 f9             	mov    %r15,%rcx
  30b7de:	e8 ad dc fa ff       	call   2b9490 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*>, long)>
  30b7e3:	48 89 9d c8 fe ff ff 	mov    %rbx,-0x138(%rbp)
  30b7ea:	4c 89 bd d0 fe ff ff 	mov    %r15,-0x130(%rbp)
  30b7f1:	4c 89 ad d8 fe ff ff 	mov    %r13,-0x128(%rbp)
  30b7f8:	c6 85 30 ff ff ff 00 	movb   $0x0,-0xd0(%rbp)
  30b7ff:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b803:	c5 fe 7f 85 e0 fe ff 	vmovdqu %ymm0,-0x120(%rbp)
  30b80a:	ff 
  30b80b:	c5 fe 7f 85 00 ff ff 	vmovdqu %ymm0,-0x100(%rbp)
  30b812:	ff 
  30b813:	c5 fe 7f 85 09 ff ff 	vmovdqu %ymm0,-0xf7(%rbp)
  30b81a:	ff 
  30b81b:	66 c7 85 38 ff ff ff 	movw   $0x3,-0xc8(%rbp)
  30b822:	03 00 
  30b824:	c6 85 3a ff ff ff 00 	movb   $0x0,-0xc6(%rbp)
  30b82b:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30b832:	48 8d b5 c8 fe ff ff 	lea    -0x138(%rbp),%rsi
  30b839:	c5 f8 77             	vzeroupper
  30b83c:	e8 9f d3 03 00       	call   348be0 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::push_back(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline)>
  30b841:	48 8d bd c8 fe ff ff 	lea    -0x138(%rbp),%rdi
  30b848:	e8 43 42 ff ff       	call   2ffa90 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30b84d:	48 8b 9d 98 f9 ff ff 	mov    -0x668(%rbp),%rbx
  30b854:	e9 6d 01 00 00       	jmp    30b9c6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x786>
  30b859:	48 8b 80 58 03 00 00 	mov    0x358(%rax),%rax
  30b860:	4c 8b a8 b8 06 00 00 	mov    0x6b8(%rax),%r13
  30b867:	48 8b 98 c0 06 00 00 	mov    0x6c0(%rax),%rbx
  30b86e:	4c 8b b8 c8 06 00 00 	mov    0x6c8(%rax),%r15
  30b875:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b879:	c5 fa 7f 80 b8 06 00 	vmovdqu %xmm0,0x6b8(%rax)
  30b880:	00 
  30b881:	48 c7 80 c8 06 00 00 	movq   $0x0,0x6c8(%rax)
  30b888:	00 00 00 00 
  30b88c:	49 39 dd             	cmp    %rbx,%r13
  30b88f:	0f 84 14 01 00 00    	je     30b9a9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x769>
  30b895:	49 8b 04 24          	mov    (%r12),%rax
  30b899:	48 8b 80 90 2b 00 00 	mov    0x2b90(%rax),%rax
  30b8a0:	48 89 85 d0 fd ff ff 	mov    %rax,-0x230(%rbp)
  30b8a7:	4c 89 ad d8 fd ff ff 	mov    %r13,-0x228(%rbp)
  30b8ae:	4c 29 eb             	sub    %r13,%rbx
  30b8b1:	48 89 9d e0 fd ff ff 	mov    %rbx,-0x220(%rbp)
  30b8b8:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30b8bf:	48 8d b5 d0 fd ff ff 	lea    -0x230(%rbp),%rsi
  30b8c6:	c5 f8 77             	vzeroupper
  30b8c9:	e8 42 b1 03 00       	call   346a10 <cnetmod::quic::encode_frame@cnetmod.protocol.quic(cnetmod::quic::crypto_frame@cnetmod.protocol.quic const&)>
  30b8ce:	49 8b 04 24          	mov    (%r12),%rax
  30b8d2:	48 01 98 90 2b 00 00 	add    %rbx,0x2b90(%rax)
  30b8d9:	48 8b bd 88 f9 ff ff 	mov    -0x678(%rbp),%rdi
  30b8e0:	48 8b 77 08          	mov    0x8(%rdi),%rsi
  30b8e4:	48 8b 95 40 fd ff ff 	mov    -0x2c0(%rbp),%rdx
  30b8eb:	48 8b 8d 48 fd ff ff 	mov    -0x2b8(%rbp),%rcx
  30b8f2:	49 89 c8             	mov    %rcx,%r8
  30b8f5:	49 29 d0             	sub    %rdx,%r8
  30b8f8:	e8 93 db fa ff       	call   2b9490 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*>, long)>
  30b8fd:	48 8b 85 50 fd ff ff 	mov    -0x2b0(%rbp),%rax
  30b904:	c5 f9 6f 85 40 fd ff 	vmovdqa -0x2c0(%rbp),%xmm0
  30b90b:	ff 
  30b90c:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30b910:	c5 f8 29 8d 40 fd ff 	vmovaps %xmm1,-0x2c0(%rbp)
  30b917:	ff 
  30b918:	48 c7 85 50 fd ff ff 	movq   $0x0,-0x2b0(%rbp)
  30b91f:	00 00 00 00 
  30b923:	c5 f9 7f 85 50 fe ff 	vmovdqa %xmm0,-0x1b0(%rbp)
  30b92a:	ff 
  30b92b:	48 89 85 60 fe ff ff 	mov    %rax,-0x1a0(%rbp)
  30b932:	c6 85 b8 fe ff ff 00 	movb   $0x0,-0x148(%rbp)
  30b939:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b93d:	c5 fe 7f 85 68 fe ff 	vmovdqu %ymm0,-0x198(%rbp)
  30b944:	ff 
  30b945:	c5 fe 7f 85 88 fe ff 	vmovdqu %ymm0,-0x178(%rbp)
  30b94c:	ff 
  30b94d:	c5 fe 7f 85 91 fe ff 	vmovdqu %ymm0,-0x16f(%rbp)
  30b954:	ff 
  30b955:	66 c7 85 c0 fe ff ff 	movw   $0x3,-0x140(%rbp)
  30b95c:	03 00 
  30b95e:	c6 85 c2 fe ff ff 00 	movb   $0x0,-0x13e(%rbp)
  30b965:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30b96c:	48 8d b5 50 fe ff ff 	lea    -0x1b0(%rbp),%rsi
  30b973:	c5 f8 77             	vzeroupper
  30b976:	e8 65 d2 03 00       	call   348be0 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::push_back(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline)>
  30b97b:	48 8d bd 50 fe ff ff 	lea    -0x1b0(%rbp),%rdi
  30b982:	e8 09 41 ff ff       	call   2ffa90 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30b987:	48 8b bd 40 fd ff ff 	mov    -0x2c0(%rbp),%rdi
  30b98e:	48 85 ff             	test   %rdi,%rdi
  30b991:	74 16                	je     30b9a9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x769>
  30b993:	48 89 bd 48 fd ff ff 	mov    %rdi,-0x2b8(%rbp)
  30b99a:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30b9a1:	48 29 fe             	sub    %rdi,%rsi
  30b9a4:	e8 17 f0 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b9a9:	4d 85 ed             	test   %r13,%r13
  30b9ac:	48 8b 9d 98 f9 ff ff 	mov    -0x668(%rbp),%rbx
  30b9b3:	74 11                	je     30b9c6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x786>
  30b9b5:	4d 29 ef             	sub    %r13,%r15
  30b9b8:	4c 89 ef             	mov    %r13,%rdi
  30b9bb:	4c 89 fe             	mov    %r15,%rsi
  30b9be:	c5 f8 77             	vzeroupper
  30b9c1:	e8 fa ef dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b9c6:	4c 89 a5 a0 f9 ff ff 	mov    %r12,-0x660(%rbp)
  30b9cd:	4c 8b ad 88 f9 ff ff 	mov    -0x678(%rbp),%r13
  30b9d4:	eb 1a                	jmp    30b9f0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x7b0>
  30b9d6:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
  30b9dd:	00 00 00 
  30b9e0:	45 84 f6             	test   %r14b,%r14b
  30b9e3:	48 8b 9d 98 f9 ff ff 	mov    -0x668(%rbp),%rbx
  30b9ea:	0f 84 ef 08 00 00    	je     30c2df <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x109f>
  30b9f0:	49 8b 34 24          	mov    (%r12),%rsi
  30b9f4:	4c 8b b6 f0 06 00 00 	mov    0x6f0(%rsi),%r14
  30b9fb:	4d 85 f6             	test   %r14,%r14
  30b9fe:	75 70                	jne    30ba70 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30ba00:	48 8b 86 f8 06 00 00 	mov    0x6f8(%rsi),%rax
  30ba07:	48 83 78 40 00       	cmpq   $0x0,0x40(%rax)
  30ba0c:	75 62                	jne    30ba70 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30ba0e:	48 83 b8 30 01 00 00 	cmpq   $0x0,0x130(%rax)
  30ba15:	00 
  30ba16:	75 58                	jne    30ba70 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30ba18:	48 83 b8 60 01 00 00 	cmpq   $0x0,0x160(%rax)
  30ba1f:	00 
  30ba20:	75 4e                	jne    30ba70 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30ba22:	48 83 b8 90 01 00 00 	cmpq   $0x0,0x190(%rax)
  30ba29:	00 
  30ba2a:	75 44                	jne    30ba70 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30ba2c:	48 83 b8 c0 01 00 00 	cmpq   $0x0,0x1c0(%rax)
  30ba33:	00 
  30ba34:	75 3a                	jne    30ba70 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30ba36:	48 83 b8 f0 01 00 00 	cmpq   $0x0,0x1f0(%rax)
  30ba3d:	00 
  30ba3e:	75 30                	jne    30ba70 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30ba40:	48 83 b8 20 02 00 00 	cmpq   $0x0,0x220(%rax)
  30ba47:	00 
  30ba48:	75 26                	jne    30ba70 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30ba4a:	48 83 b8 50 02 00 00 	cmpq   $0x0,0x250(%rax)
  30ba51:	00 
  30ba52:	75 1c                	jne    30ba70 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30ba54:	48 83 b8 80 02 00 00 	cmpq   $0x0,0x280(%rax)
  30ba5b:	00 
  30ba5c:	0f 84 81 08 00 00    	je     30c2e3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10a3>
  30ba62:	66 66 66 66 66 2e 0f 	data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
  30ba69:	1f 84 00 00 00 00 00 
  30ba70:	49 8b 45 08          	mov    0x8(%r13),%rax
  30ba74:	49 2b 45 00          	sub    0x0(%r13),%rax
  30ba78:	48 39 d8             	cmp    %rbx,%rax
  30ba7b:	0f 83 62 08 00 00    	jae    30c2e3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10a3>
  30ba81:	c6 85 38 fe ff ff 00 	movb   $0x0,-0x1c8(%rbp)
  30ba88:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30ba8c:	c5 fe 7f 85 d0 fd ff 	vmovdqu %ymm0,-0x230(%rbp)
  30ba93:	ff 
  30ba94:	c5 fe 7f 85 f0 fd ff 	vmovdqu %ymm0,-0x210(%rbp)
  30ba9b:	ff 
  30ba9c:	c5 fe 7f 85 10 fe ff 	vmovdqu %ymm0,-0x1f0(%rbp)
  30baa3:	ff 
  30baa4:	c6 85 30 fe ff ff 00 	movb   $0x0,-0x1d0(%rbp)
  30baab:	49 81 e7 00 ff ff ff 	and    $0xffffffffffffff00,%r15
  30bab2:	66 c7 85 40 fe ff ff 	movw   $0x3,-0x1c0(%rbp)
  30bab9:	03 00 
  30babb:	c6 85 42 fe ff ff 00 	movb   $0x0,-0x1be(%rbp)
  30bac2:	4d 85 f6             	test   %r14,%r14
  30bac5:	0f 84 f5 02 00 00    	je     30bdc0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xb80>
  30bacb:	48 8b 86 d0 06 00 00 	mov    0x6d0(%rsi),%rax
  30bad2:	48 8b 96 e8 06 00 00 	mov    0x6e8(%rsi),%rdx
  30bad9:	48 b9 f1 f0 f0 f0 f0 	movabs $0xf0f0f0f0f0f0f0f1,%rcx
  30bae0:	f0 f0 f0 
  30bae3:	c4 e2 f3 f6 c9       	mulx   %rcx,%rcx,%rcx
  30bae8:	48 89 ce             	mov    %rcx,%rsi
  30baeb:	48 c1 ee 05          	shr    $0x5,%rsi
  30baef:	4c 8b 24 f0          	mov    (%rax,%rsi,8),%r12
  30baf3:	48 b8 e0 ff ff ff ff 	movabs $0x1fffffffffffffe0,%rax
  30bafa:	ff ff 1f 
  30bafd:	48 21 c1             	and    %rax,%rcx
  30bb00:	48 8d 04 71          	lea    (%rcx,%rsi,2),%rax
  30bb04:	48 29 c2             	sub    %rax,%rdx
  30bb07:	4c 6b ea 78          	imul   $0x78,%rdx,%r13
  30bb0b:	c4 81 78 10 04 2c    	vmovups (%r12,%r13,1),%xmm0
  30bb11:	c5 f8 29 85 d0 fd ff 	vmovaps %xmm0,-0x230(%rbp)
  30bb18:	ff 
  30bb19:	4b 8b 44 2c 10       	mov    0x10(%r12,%r13,1),%rax
  30bb1e:	48 89 85 e0 fd ff ff 	mov    %rax,-0x220(%rbp)
  30bb25:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30bb29:	c4 81 78 11 0c 2c    	vmovups %xmm1,(%r12,%r13,1)
  30bb2f:	4b c7 44 2c 10 00 00 	movq   $0x0,0x10(%r12,%r13,1)
  30bb36:	00 00 
  30bb38:	c4 81 78 10 44 2c 18 	vmovups 0x18(%r12,%r13,1),%xmm0
  30bb3f:	c4 81 78 11 4c 2c 18 	vmovups %xmm1,0x18(%r12,%r13,1)
  30bb46:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bb4d:	c5 f8 11 85 e8 fd ff 	vmovups %xmm0,-0x218(%rbp)
  30bb54:	ff 
  30bb55:	48 85 db             	test   %rbx,%rbx
  30bb58:	74 26                	je     30bb80 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x940>
  30bb5a:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30bb61:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30bb67:	48 85 c0             	test   %rax,%rax
  30bb6a:	75 14                	jne    30bb80 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x940>
  30bb6c:	48 8b 03             	mov    (%rbx),%rax
  30bb6f:	48 89 df             	mov    %rbx,%rdi
  30bb72:	c5 f8 77             	vzeroupper
  30bb75:	ff 50 10             	call   *0x10(%rax)
  30bb78:	48 89 df             	mov    %rbx,%rdi
  30bb7b:	e8 a0 ec dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30bb80:	4d 01 ec             	add    %r13,%r12
  30bb83:	c4 c1 7e 6f 44 24 28 	vmovdqu 0x28(%r12),%ymm0
  30bb8a:	c4 c1 7c 10 4c 24 48 	vmovups 0x48(%r12),%ymm1
  30bb91:	c4 c1 7c 10 54 24 53 	vmovups 0x53(%r12),%ymm2
  30bb98:	48 8d 85 f8 fd ff ff 	lea    -0x208(%rbp),%rax
  30bb9f:	c5 fc 11 50 2b       	vmovups %ymm2,0x2b(%rax)
  30bba4:	c5 fc 11 48 20       	vmovups %ymm1,0x20(%rax)
  30bba9:	c5 fe 7f 00          	vmovdqu %ymm0,(%rax)
  30bbad:	4c 8b a5 a0 f9 ff ff 	mov    -0x660(%rbp),%r12
  30bbb4:	49 8b 3c 24          	mov    (%r12),%rdi
  30bbb8:	b8 c8 06 00 00       	mov    $0x6c8,%eax
  30bbbd:	48 01 c7             	add    %rax,%rdi
  30bbc0:	c5 f8 77             	vzeroupper
  30bbc3:	e8 28 5a 00 00       	call   3115f0 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::pop_front()>
  30bbc8:	4c 8b ad 88 f9 ff ff 	mov    -0x678(%rbp),%r13
  30bbcf:	48 8b 95 d0 fd ff ff 	mov    -0x230(%rbp),%rdx
  30bbd6:	48 8b 8d d8 fd ff ff 	mov    -0x228(%rbp),%rcx
  30bbdd:	49 89 c8             	mov    %rcx,%r8
  30bbe0:	49 29 d0             	sub    %rdx,%r8
  30bbe3:	48 8b 85 e8 fd ff ff 	mov    -0x218(%rbp),%rax
  30bbea:	48 8b bd 18 fe ff ff 	mov    -0x1e8(%rbp),%rdi
  30bbf1:	4c 01 c7             	add    %r8,%rdi
  30bbf4:	48 03 bd 28 fe ff ff 	add    -0x1d8(%rbp),%rdi
  30bbfb:	48 85 c0             	test   %rax,%rax
  30bbfe:	49 0f 44 f8          	cmove  %r8,%rdi
  30bc02:	49 8b 75 08          	mov    0x8(%r13),%rsi
  30bc06:	4c 8b 8d 98 f9 ff ff 	mov    -0x668(%rbp),%r9
  30bc0d:	49 29 f1             	sub    %rsi,%r9
  30bc10:	4d 03 4d 00          	add    0x0(%r13),%r9
  30bc14:	4c 39 cf             	cmp    %r9,%rdi
  30bc17:	0f 86 43 01 00 00    	jbe    30bd60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xb20>
  30bc1d:	49 8b 1c 24          	mov    (%r12),%rbx
  30bc21:	4d 85 f6             	test   %r14,%r14
  30bc24:	0f 84 01 03 00 00    	je     30bf2b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xceb>
  30bc2a:	48 8b 93 e8 06 00 00 	mov    0x6e8(%rbx),%rdx
  30bc31:	48 85 d2             	test   %rdx,%rdx
  30bc34:	75 16                	jne    30bc4c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xa0c>
  30bc36:	48 8d bb c8 06 00 00 	lea    0x6c8(%rbx),%rdi
  30bc3d:	c5 f8 77             	vzeroupper
  30bc40:	e8 8b dd 00 00       	call   3199d0 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::__add_front_capacity()>
  30bc45:	48 8b 93 e8 06 00 00 	mov    0x6e8(%rbx),%rdx
  30bc4c:	48 8b 8b d0 06 00 00 	mov    0x6d0(%rbx),%rcx
  30bc53:	48 b8 f1 f0 f0 f0 f0 	movabs $0xf0f0f0f0f0f0f0f1,%rax
  30bc5a:	f0 f0 f0 
  30bc5d:	c4 e2 fb f6 c0       	mulx   %rax,%rax,%rax
  30bc62:	48 89 c6             	mov    %rax,%rsi
  30bc65:	48 c1 ee 05          	shr    $0x5,%rsi
  30bc69:	48 bf e0 ff ff ff ff 	movabs $0x1fffffffffffffe0,%rdi
  30bc70:	ff ff 1f 
  30bc73:	48 21 f8             	and    %rdi,%rax
  30bc76:	48 8d 04 70          	lea    (%rax,%rsi,2),%rax
  30bc7a:	48 29 c2             	sub    %rax,%rdx
  30bc7d:	48 8b 3c f1          	mov    (%rcx,%rsi,8),%rdi
  30bc81:	48 6b c2 78          	imul   $0x78,%rdx,%rax
  30bc85:	48 01 f8             	add    %rdi,%rax
  30bc88:	48 3b 8b d8 06 00 00 	cmp    0x6d8(%rbx),%rcx
  30bc8f:	ba 00 00 00 00       	mov    $0x0,%edx
  30bc94:	48 0f 44 c2          	cmove  %rdx,%rax
  30bc98:	48 39 f8             	cmp    %rdi,%rax
  30bc9b:	75 0d                	jne    30bcaa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xa6a>
  30bc9d:	48 8b 44 f1 f8       	mov    -0x8(%rcx,%rsi,8),%rax
  30bca2:	b9 f0 0f 00 00       	mov    $0xff0,%ecx
  30bca7:	48 01 c8             	add    %rcx,%rax
  30bcaa:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30bcae:	c5 f8 11 48 88       	vmovups %xmm1,-0x78(%rax)
  30bcb3:	48 c7 40 98 00 00 00 	movq   $0x0,-0x68(%rax)
  30bcba:	00 
  30bcbb:	c5 f8 28 85 d0 fd ff 	vmovaps -0x230(%rbp),%xmm0
  30bcc2:	ff 
  30bcc3:	c5 f8 11 40 88       	vmovups %xmm0,-0x78(%rax)
  30bcc8:	48 8b 8d e0 fd ff ff 	mov    -0x220(%rbp),%rcx
  30bccf:	48 89 48 98          	mov    %rcx,-0x68(%rax)
  30bcd3:	c5 f8 29 8d d0 fd ff 	vmovaps %xmm1,-0x230(%rbp)
  30bcda:	ff 
  30bcdb:	48 c7 85 e0 fd ff ff 	movq   $0x0,-0x220(%rbp)
  30bce2:	00 00 00 00 
  30bce6:	c5 f8 10 85 e8 fd ff 	vmovups -0x218(%rbp),%xmm0
  30bced:	ff 
  30bcee:	c5 f8 11 40 a0       	vmovups %xmm0,-0x60(%rax)
  30bcf3:	48 8d 8d e8 fd ff ff 	lea    -0x218(%rbp),%rcx
  30bcfa:	c5 f8 11 09          	vmovups %xmm1,(%rcx)
  30bcfe:	48 8d 8d f8 fd ff ff 	lea    -0x208(%rbp),%rcx
  30bd05:	c5 fc 10 01          	vmovups (%rcx),%ymm0
  30bd09:	c5 fc 10 49 20       	vmovups 0x20(%rcx),%ymm1
  30bd0e:	c5 fc 10 51 2b       	vmovups 0x2b(%rcx),%ymm2
  30bd13:	c5 fc 11 50 db       	vmovups %ymm2,-0x25(%rax)
  30bd18:	c5 fc 11 48 d0       	vmovups %ymm1,-0x30(%rax)
  30bd1d:	c5 fc 11 40 b0       	vmovups %ymm0,-0x50(%rax)
  30bd22:	c5 f9 6f 05 a6 d2 dc 	vmovdqa -0x232d5a(%rip),%xmm0        # d8fd0 <typeinfo name for cnetmod::io_uring_context@cnetmod.io.platform.io_uring+0xbc2>
  30bd29:	ff 
  30bd2a:	c5 f9 d4 83 e8 06 00 	vpaddq 0x6e8(%rbx),%xmm0,%xmm0
  30bd31:	00 
  30bd32:	c5 fa 7f 83 e8 06 00 	vmovdqu %xmm0,0x6e8(%rbx)
  30bd39:	00 
  30bd3a:	45 31 f6             	xor    %r14d,%r14d
  30bd3d:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bd44:	48 85 db             	test   %rbx,%rbx
  30bd47:	0f 85 23 05 00 00    	jne    30c270 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1030>
  30bd4d:	e9 44 05 00 00       	jmp    30c296 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1056>
  30bd52:	66 66 66 66 66 2e 0f 	data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
  30bd59:	1f 84 00 00 00 00 00 
  30bd60:	48 39 ca             	cmp    %rcx,%rdx
  30bd63:	74 12                	je     30bd77 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xb37>
  30bd65:	4c 89 ef             	mov    %r13,%rdi
  30bd68:	c5 f8 77             	vzeroupper
  30bd6b:	e8 f0 ed fa ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30bd70:	48 8b 85 e8 fd ff ff 	mov    -0x218(%rbp),%rax
  30bd77:	48 85 c0             	test   %rax,%rax
  30bd7a:	0f 84 f9 02 00 00    	je     30c079 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe39>
  30bd80:	49 8b 75 08          	mov    0x8(%r13),%rsi
  30bd84:	4c 8b 85 18 fe ff ff 	mov    -0x1e8(%rbp),%r8
  30bd8b:	48 8d 95 f8 fd ff ff 	lea    -0x208(%rbp),%rdx
  30bd92:	4a 8d 0c 02          	lea    (%rdx,%r8,1),%rcx
  30bd96:	4c 89 ef             	mov    %r13,%rdi
  30bd99:	c5 f8 77             	vzeroupper
  30bd9c:	e8 af e7 fa ff       	call   2ba550 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::byte const*, std::byte const*>(std::__1::__wrap_iter<std::byte const*>, std::byte const*, std::byte const*, long)>
  30bda1:	48 8b 95 e8 fd ff ff 	mov    -0x218(%rbp),%rdx
  30bda8:	f6 02 01             	testb  $0x1,(%rdx)
  30bdab:	0f 84 a7 02 00 00    	je     30c058 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe18>
  30bdb1:	48 8b 52 10          	mov    0x10(%rdx),%rdx
  30bdb5:	e9 a1 02 00 00       	jmp    30c05b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe1b>
  30bdba:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
  30bdc0:	48 81 c6 f8 06 00 00 	add    $0x6f8,%rsi
  30bdc7:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30bdce:	c5 f8 77             	vzeroupper
  30bdd1:	e8 1a 25 04 00       	call   34e2f0 <cnetmod::quic::quic_stream_scheduler@cnetmod.protocol.quic.send_pipeline::take()>
  30bdd6:	80 bd c8 fd ff ff 00 	cmpb   $0x0,-0x238(%rbp)
  30bddd:	0f 84 5d 02 00 00    	je     30c040 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe00>
  30bde3:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30bdea:	48 85 ff             	test   %rdi,%rdi
  30bded:	74 16                	je     30be05 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xbc5>
  30bdef:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30bdf6:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30bdfd:	48 29 fe             	sub    %rdi,%rsi
  30be00:	e8 bb eb dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30be05:	c5 f8 10 85 50 fd ff 	vmovups -0x2b0(%rbp),%xmm0
  30be0c:	ff 
  30be0d:	c5 f8 29 85 d0 fd ff 	vmovaps %xmm0,-0x230(%rbp)
  30be14:	ff 
  30be15:	48 8b 85 60 fd ff ff 	mov    -0x2a0(%rbp),%rax
  30be1c:	48 89 85 e0 fd ff ff 	mov    %rax,-0x220(%rbp)
  30be23:	48 8d 85 50 fd ff ff 	lea    -0x2b0(%rbp),%rax
  30be2a:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30be2e:	c5 f8 11 08          	vmovups %xmm1,(%rax)
  30be32:	48 c7 40 10 00 00 00 	movq   $0x0,0x10(%rax)
  30be39:	00 
  30be3a:	c5 f8 10 85 68 fd ff 	vmovups -0x298(%rbp),%xmm0
  30be41:	ff 
  30be42:	c5 f8 11 48 18       	vmovups %xmm1,0x18(%rax)
  30be47:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30be4e:	c5 f8 11 85 e8 fd ff 	vmovups %xmm0,-0x218(%rbp)
  30be55:	ff 
  30be56:	48 85 db             	test   %rbx,%rbx
  30be59:	74 23                	je     30be7e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xc3e>
  30be5b:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30be62:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30be68:	48 85 c0             	test   %rax,%rax
  30be6b:	75 11                	jne    30be7e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xc3e>
  30be6d:	48 8b 03             	mov    (%rbx),%rax
  30be70:	48 89 df             	mov    %rbx,%rdi
  30be73:	ff 50 10             	call   *0x10(%rax)
  30be76:	48 89 df             	mov    %rbx,%rdi
  30be79:	e8 a2 e9 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30be7e:	48 8d 85 50 fd ff ff 	lea    -0x2b0(%rbp),%rax
  30be85:	c5 fe 6f 40 28       	vmovdqu 0x28(%rax),%ymm0
  30be8a:	c5 fc 10 48 48       	vmovups 0x48(%rax),%ymm1
  30be8f:	c5 fc 10 50 53       	vmovups 0x53(%rax),%ymm2
  30be94:	48 8d 85 f8 fd ff ff 	lea    -0x208(%rbp),%rax
  30be9b:	c5 fc 11 50 2b       	vmovups %ymm2,0x2b(%rax)
  30bea0:	c5 fc 11 48 20       	vmovups %ymm1,0x20(%rax)
  30bea5:	c5 fe 7f 00          	vmovdqu %ymm0,(%rax)
  30bea9:	4c 8b bd 40 fd ff ff 	mov    -0x2c0(%rbp),%r15
  30beb0:	0f b7 85 48 fd ff ff 	movzwl -0x2b8(%rbp),%eax
  30beb7:	48 89 85 60 f9 ff ff 	mov    %rax,-0x6a0(%rbp)
  30bebe:	80 bd c8 fd ff ff 01 	cmpb   $0x1,-0x238(%rbp)
  30bec5:	0f 85 04 fd ff ff    	jne    30bbcf <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x98f>
  30becb:	48 8b 9d 70 fd ff ff 	mov    -0x290(%rbp),%rbx
  30bed2:	48 85 db             	test   %rbx,%rbx
  30bed5:	74 26                	je     30befd <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xcbd>
  30bed7:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30bede:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30bee4:	48 85 c0             	test   %rax,%rax
  30bee7:	75 14                	jne    30befd <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xcbd>
  30bee9:	48 8b 03             	mov    (%rbx),%rax
  30beec:	48 89 df             	mov    %rbx,%rdi
  30beef:	c5 f8 77             	vzeroupper
  30bef2:	ff 50 10             	call   *0x10(%rax)
  30bef5:	48 89 df             	mov    %rbx,%rdi
  30bef8:	e8 23 e9 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30befd:	48 8b bd 50 fd ff ff 	mov    -0x2b0(%rbp),%rdi
  30bf04:	48 85 ff             	test   %rdi,%rdi
  30bf07:	0f 84 c2 fc ff ff    	je     30bbcf <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x98f>
  30bf0d:	48 89 bd 58 fd ff ff 	mov    %rdi,-0x2a8(%rbp)
  30bf14:	48 8b b5 60 fd ff ff 	mov    -0x2a0(%rbp),%rsi
  30bf1b:	48 29 fe             	sub    %rdi,%rsi
  30bf1e:	c5 f8 77             	vzeroupper
  30bf21:	e8 9a ea dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30bf26:	e9 a4 fc ff ff       	jmp    30bbcf <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x98f>
  30bf2b:	4c 89 bd b8 fc ff ff 	mov    %r15,-0x348(%rbp)
  30bf32:	48 8b b5 60 f9 ff ff 	mov    -0x6a0(%rbp),%rsi
  30bf39:	66 89 b5 c0 fc ff ff 	mov    %si,-0x340(%rbp)
  30bf40:	48 89 95 c8 fc ff ff 	mov    %rdx,-0x338(%rbp)
  30bf47:	48 89 8d d0 fc ff ff 	mov    %rcx,-0x330(%rbp)
  30bf4e:	48 8b 8d e0 fd ff ff 	mov    -0x220(%rbp),%rcx
  30bf55:	48 8b 95 f0 fd ff ff 	mov    -0x210(%rbp),%rdx
  30bf5c:	48 89 8d d8 fc ff ff 	mov    %rcx,-0x328(%rbp)
  30bf63:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30bf67:	c5 f9 7f 85 d0 fd ff 	vmovdqa %xmm0,-0x230(%rbp)
  30bf6e:	ff 
  30bf6f:	48 c7 85 e0 fd ff ff 	movq   $0x0,-0x220(%rbp)
  30bf76:	00 00 00 00 
  30bf7a:	48 89 85 e0 fc ff ff 	mov    %rax,-0x320(%rbp)
  30bf81:	48 89 95 e8 fc ff ff 	mov    %rdx,-0x318(%rbp)
  30bf88:	48 8d 85 e8 fd ff ff 	lea    -0x218(%rbp),%rax
  30bf8f:	c5 fa 7f 00          	vmovdqu %xmm0,(%rax)
  30bf93:	48 8d 85 f8 fd ff ff 	lea    -0x208(%rbp),%rax
  30bf9a:	c5 fe 6f 00          	vmovdqu (%rax),%ymm0
  30bf9e:	c5 fc 10 48 20       	vmovups 0x20(%rax),%ymm1
  30bfa3:	c5 fc 10 50 2b       	vmovups 0x2b(%rax),%ymm2
  30bfa8:	48 8d 85 f0 fc ff ff 	lea    -0x310(%rbp),%rax
  30bfaf:	c5 fc 11 50 2b       	vmovups %ymm2,0x2b(%rax)
  30bfb4:	c5 fc 11 48 20       	vmovups %ymm1,0x20(%rax)
  30bfb9:	c5 fe 7f 00          	vmovdqu %ymm0,(%rax)
  30bfbd:	48 89 df             	mov    %rbx,%rdi
  30bfc0:	48 8d b5 b8 fc ff ff 	lea    -0x348(%rbp),%rsi
  30bfc7:	c5 f8 77             	vzeroupper
  30bfca:	e8 31 3b ff ff       	call   2ffb00 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::requeue_prioritized_stream_frame(cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline)>
  30bfcf:	48 8b 9d e8 fc ff ff 	mov    -0x318(%rbp),%rbx
  30bfd6:	48 85 db             	test   %rbx,%rbx
  30bfd9:	74 23                	je     30bffe <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xdbe>
  30bfdb:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30bfe2:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30bfe8:	48 85 c0             	test   %rax,%rax
  30bfeb:	75 11                	jne    30bffe <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xdbe>
  30bfed:	48 8b 03             	mov    (%rbx),%rax
  30bff0:	48 89 df             	mov    %rbx,%rdi
  30bff3:	ff 50 10             	call   *0x10(%rax)
  30bff6:	48 89 df             	mov    %rbx,%rdi
  30bff9:	e8 22 e8 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30bffe:	48 8b bd c8 fc ff ff 	mov    -0x338(%rbp),%rdi
  30c005:	48 85 ff             	test   %rdi,%rdi
  30c008:	74 36                	je     30c040 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe00>
  30c00a:	48 89 bd d0 fc ff ff 	mov    %rdi,-0x330(%rbp)
  30c011:	48 8b b5 d8 fc ff ff 	mov    -0x328(%rbp),%rsi
  30c018:	48 29 fe             	sub    %rdi,%rsi
  30c01b:	e8 a0 e9 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c020:	45 31 f6             	xor    %r14d,%r14d
  30c023:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30c02a:	48 85 db             	test   %rbx,%rbx
  30c02d:	0f 85 3d 02 00 00    	jne    30c270 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1030>
  30c033:	e9 5e 02 00 00       	jmp    30c296 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1056>
  30c038:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
  30c03f:	00 
  30c040:	45 31 f6             	xor    %r14d,%r14d
  30c043:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30c04a:	48 85 db             	test   %rbx,%rbx
  30c04d:	0f 85 1d 02 00 00    	jne    30c270 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1030>
  30c053:	e9 3e 02 00 00       	jmp    30c296 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1056>
  30c058:	48 ff c2             	inc    %rdx
  30c05b:	4c 8b 85 28 fe ff ff 	mov    -0x1d8(%rbp),%r8
  30c062:	48 03 95 20 fe ff ff 	add    -0x1e0(%rbp),%rdx
  30c069:	49 8b 75 08          	mov    0x8(%r13),%rsi
  30c06d:	4a 8d 0c 02          	lea    (%rdx,%r8,1),%rcx
  30c071:	4c 89 ef             	mov    %r13,%rdi
  30c074:	e8 e7 ea fa ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30c079:	4d 85 f6             	test   %r14,%r14
  30c07c:	0f 85 fe 00 00 00    	jne    30c180 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xf40>
  30c082:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
  30c089:	48 3b 85 48 ff ff ff 	cmp    -0xb8(%rbp),%rax
  30c090:	75 0b                	jne    30c09d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe5d>
  30c092:	48 83 7d 88 00       	cmpq   $0x0,-0x78(%rbp)
  30c097:	75 04                	jne    30c09d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe5d>
  30c099:	31 c0                	xor    %eax,%eax
  30c09b:	eb 1d                	jmp    30c0ba <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe7a>
  30c09d:	48 8b 45 c0          	mov    -0x40(%rbp),%rax
  30c0a1:	48 2b 45 b8          	sub    -0x48(%rbp),%rax
  30c0a5:	48 c1 f8 03          	sar    $0x3,%rax
  30c0a9:	48 b9 ef ee ee ee ee 	movabs $0xeeeeeeeeeeeeeeef,%rcx
  30c0b0:	ee ee ee 
  30c0b3:	48 0f af c1          	imul   %rcx,%rax
  30c0b7:	48 ff c0             	inc    %rax
  30c0ba:	4c 89 bd 90 fa ff ff 	mov    %r15,-0x570(%rbp)
  30c0c1:	48 8b 8d 60 f9 ff ff 	mov    -0x6a0(%rbp),%rcx
  30c0c8:	48 89 8d 98 fa ff ff 	mov    %rcx,-0x568(%rbp)
  30c0cf:	48 89 85 e8 f9 ff ff 	mov    %rax,-0x618(%rbp)
  30c0d6:	80 bd 28 fa ff ff 00 	cmpb   $0x0,-0x5d8(%rbp)
  30c0dd:	74 70                	je     30c14f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xf0f>
  30c0df:	48 8b 8d b8 f9 ff ff 	mov    -0x648(%rbp),%rcx
  30c0e6:	48 89 8d f0 f9 ff ff 	mov    %rcx,-0x610(%rbp)
  30c0ed:	48 8d 95 f0 f9 ff ff 	lea    -0x610(%rbp),%rdx
  30c0f4:	48 89 95 40 fd ff ff 	mov    %rdx,-0x2c0(%rbp)
  30c0fb:	48 8d 95 e8 f9 ff ff 	lea    -0x618(%rbp),%rdx
  30c102:	48 89 95 48 fd ff ff 	mov    %rdx,-0x2b8(%rbp)
  30c109:	48 8d 95 90 fa ff ff 	lea    -0x570(%rbp),%rdx
  30c110:	48 89 95 50 fd ff ff 	mov    %rdx,-0x2b0(%rbp)
  30c117:	48 8d 95 b0 f9 ff ff 	lea    -0x650(%rbp),%rdx
  30c11e:	48 89 95 58 fd ff ff 	mov    %rdx,-0x2a8(%rbp)
  30c125:	48 3b 8d c0 f9 ff ff 	cmp    -0x640(%rbp),%rcx
  30c12c:	0f 83 92 01 00 00    	jae    30c2c4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1084>
  30c132:	48 89 01             	mov    %rax,(%rcx)
  30c135:	c5 fa 6f 85 90 fa ff 	vmovdqu -0x570(%rbp),%xmm0
  30c13c:	ff 
  30c13d:	c5 fa 7f 41 08       	vmovdqu %xmm0,0x8(%rcx)
  30c142:	48 83 c1 18          	add    $0x18,%rcx
  30c146:	48 89 8d b8 f9 ff ff 	mov    %rcx,-0x648(%rbp)
  30c14d:	eb 31                	jmp    30c180 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xf40>
  30c14f:	48 89 85 10 fa ff ff 	mov    %rax,-0x5f0(%rbp)
  30c156:	48 8b 85 90 fa ff ff 	mov    -0x570(%rbp),%rax
  30c15d:	48 8d 8d 18 fa ff ff 	lea    -0x5e8(%rbp),%rcx
  30c164:	48 89 01             	mov    %rax,(%rcx)
  30c167:	48 8b 85 98 fa ff ff 	mov    -0x568(%rbp),%rax
  30c16e:	48 89 41 08          	mov    %rax,0x8(%rcx)
  30c172:	c6 85 28 fa ff ff 01 	movb   $0x1,-0x5d8(%rbp)
  30c179:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
  30c180:	c5 f8 28 85 d0 fd ff 	vmovaps -0x230(%rbp),%xmm0
  30c187:	ff 
  30c188:	c5 f8 29 85 40 fc ff 	vmovaps %xmm0,-0x3c0(%rbp)
  30c18f:	ff 
  30c190:	48 8b 85 e0 fd ff ff 	mov    -0x220(%rbp),%rax
  30c197:	48 89 85 50 fc ff ff 	mov    %rax,-0x3b0(%rbp)
  30c19e:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30c1a2:	c5 f8 29 8d d0 fd ff 	vmovaps %xmm1,-0x230(%rbp)
  30c1a9:	ff 
  30c1aa:	48 c7 85 e0 fd ff ff 	movq   $0x0,-0x220(%rbp)
  30c1b1:	00 00 00 00 
  30c1b5:	c5 f8 10 85 e8 fd ff 	vmovups -0x218(%rbp),%xmm0
  30c1bc:	ff 
  30c1bd:	c5 f8 11 85 58 fc ff 	vmovups %xmm0,-0x3a8(%rbp)
  30c1c4:	ff 
  30c1c5:	48 8d 85 e8 fd ff ff 	lea    -0x218(%rbp),%rax
  30c1cc:	c5 f8 11 08          	vmovups %xmm1,(%rax)
  30c1d0:	48 8d 85 f8 fd ff ff 	lea    -0x208(%rbp),%rax
  30c1d7:	c5 fe 6f 00          	vmovdqu (%rax),%ymm0
  30c1db:	c5 fc 10 48 20       	vmovups 0x20(%rax),%ymm1
  30c1e0:	c5 fc 10 50 2b       	vmovups 0x2b(%rax),%ymm2
  30c1e5:	48 8d 85 68 fc ff ff 	lea    -0x398(%rbp),%rax
  30c1ec:	c5 fc 11 50 2b       	vmovups %ymm2,0x2b(%rax)
  30c1f1:	c5 fc 11 48 20       	vmovups %ymm1,0x20(%rax)
  30c1f6:	c5 fe 7f 00          	vmovdqu %ymm0,(%rax)
  30c1fa:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30c201:	48 8d b5 40 fc ff ff 	lea    -0x3c0(%rbp),%rsi
  30c208:	c5 f8 77             	vzeroupper
  30c20b:	e8 d0 c9 03 00       	call   348be0 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::push_back(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline)>
  30c210:	48 8b 9d 60 fc ff ff 	mov    -0x3a0(%rbp),%rbx
  30c217:	48 85 db             	test   %rbx,%rbx
  30c21a:	74 23                	je     30c23f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfff>
  30c21c:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30c223:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30c229:	48 85 c0             	test   %rax,%rax
  30c22c:	75 11                	jne    30c23f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfff>
  30c22e:	48 8b 03             	mov    (%rbx),%rax
  30c231:	48 89 df             	mov    %rbx,%rdi
  30c234:	ff 50 10             	call   *0x10(%rax)
  30c237:	48 89 df             	mov    %rbx,%rdi
  30c23a:	e8 e1 e5 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30c23f:	48 8b bd 40 fc ff ff 	mov    -0x3c0(%rbp),%rdi
  30c246:	41 b6 01             	mov    $0x1,%r14b
  30c249:	48 85 ff             	test   %rdi,%rdi
  30c24c:	74 16                	je     30c264 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1024>
  30c24e:	48 89 bd 48 fc ff ff 	mov    %rdi,-0x3b8(%rbp)
  30c255:	48 8b b5 50 fc ff ff 	mov    -0x3b0(%rbp),%rsi
  30c25c:	48 29 fe             	sub    %rdi,%rsi
  30c25f:	e8 5c e7 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c264:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30c26b:	48 85 db             	test   %rbx,%rbx
  30c26e:	74 26                	je     30c296 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1056>
  30c270:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30c277:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30c27d:	48 85 c0             	test   %rax,%rax
  30c280:	75 14                	jne    30c296 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1056>
  30c282:	48 8b 03             	mov    (%rbx),%rax
  30c285:	48 89 df             	mov    %rbx,%rdi
  30c288:	c5 f8 77             	vzeroupper
  30c28b:	ff 50 10             	call   *0x10(%rax)
  30c28e:	48 89 df             	mov    %rbx,%rdi
  30c291:	e8 8a e5 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30c296:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30c29d:	48 85 ff             	test   %rdi,%rdi
  30c2a0:	0f 84 3a f7 ff ff    	je     30b9e0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x7a0>
  30c2a6:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30c2ad:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30c2b4:	48 29 fe             	sub    %rdi,%rsi
  30c2b7:	c5 f8 77             	vzeroupper
  30c2ba:	e8 01 e7 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c2bf:	e9 1c f7 ff ff       	jmp    30b9e0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x7a0>
  30c2c4:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30c2cb:	c5 f8 77             	vzeroupper
  30c2ce:	e8 2d bc 00 00       	call   317f00 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const>
  30c2d3:	48 8b 8d f0 f9 ff ff 	mov    -0x610(%rbp),%rcx
  30c2da:	e9 67 fe ff ff       	jmp    30c146 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xf06>
  30c2df:	49 8b 34 24          	mov    (%r12),%rsi
  30c2e3:	48 83 be 68 08 00 00 	cmpq   $0x0,0x868(%rsi)
  30c2ea:	00 
  30c2eb:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30c2f2:	74 5c                	je     30c350 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1110>
  30c2f4:	48 8b 86 48 08 00 00 	mov    0x848(%rsi),%rax
  30c2fb:	48 8b 96 60 08 00 00 	mov    0x860(%rsi),%rdx
  30c302:	49 89 d9             	mov    %rbx,%r9
  30c305:	48 bb c1 c0 c0 c0 c0 	movabs $0xc0c0c0c0c0c0c0c1,%rbx
  30c30c:	c0 c0 c0 
  30c30f:	c4 e2 f3 f6 cb       	mulx   %rbx,%rcx,%rcx
  30c314:	48 c1 e9 07          	shr    $0x7,%rcx
  30c318:	48 8b 04 c8          	mov    (%rax,%rcx,8),%rax
  30c31c:	48 69 c9 aa 00 00 00 	imul   $0xaa,%rcx,%rcx
  30c323:	48 29 ca             	sub    %rcx,%rdx
  30c326:	49 8b 75 08          	mov    0x8(%r13),%rsi
  30c32a:	48 89 f7             	mov    %rsi,%rdi
  30c32d:	49 2b 7d 00          	sub    0x0(%r13),%rdi
  30c331:	48 8d 0c 52          	lea    (%rdx,%rdx,2),%rcx
  30c335:	48 8b 14 c8          	mov    (%rax,%rcx,8),%rdx
  30c339:	48 8b 4c c8 08       	mov    0x8(%rax,%rcx,8),%rcx
  30c33e:	49 89 c8             	mov    %rcx,%r8
  30c341:	49 29 d0             	sub    %rdx,%r8
  30c344:	4c 01 c7             	add    %r8,%rdi
  30c347:	4c 39 cf             	cmp    %r9,%rdi
  30c34a:	0f 86 52 01 00 00    	jbe    30c4a2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1262>
  30c350:	45 31 ff             	xor    %r15d,%r15d
  30c353:	80 bd af f9 ff ff 01 	cmpb   $0x1,-0x651(%rbp)
  30c35a:	75 40                	jne    30c39c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x115c>
  30c35c:	48 8b 85 68 f9 ff ff 	mov    -0x698(%rbp),%rax
  30c363:	0f b6 40 14          	movzbl 0x14(%rax),%eax
  30c367:	48 8d 48 15          	lea    0x15(%rax),%rcx
  30c36b:	48 39 8d 58 f9 ff ff 	cmp    %rcx,-0x6a8(%rbp)
  30c372:	76 28                	jbe    30c39c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x115c>
  30c374:	48 8b b5 58 f9 ff ff 	mov    -0x6a8(%rbp),%rsi
  30c37b:	48 29 c6             	sub    %rax,%rsi
  30c37e:	48 83 c6 eb          	add    $0xffffffffffffffeb,%rsi
  30c382:	49 8b 45 08          	mov    0x8(%r13),%rax
  30c386:	49 2b 45 00          	sub    0x0(%r13),%rax
  30c38a:	48 39 f0             	cmp    %rsi,%rax
  30c38d:	73 0d                	jae    30c39c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x115c>
  30c38f:	4c 89 ef             	mov    %r13,%rdi
  30c392:	31 d2                	xor    %edx,%edx
  30c394:	c5 f8 77             	vzeroupper
  30c397:	e8 74 50 00 00       	call   311410 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::resize(unsigned long, std::byte const&)>
  30c39c:	49 8b 55 00          	mov    0x0(%r13),%rdx
  30c3a0:	49 8b 4d 08          	mov    0x8(%r13),%rcx
  30c3a4:	48 39 ca             	cmp    %rcx,%rdx
  30c3a7:	0f 84 c3 00 00 00    	je     30c470 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1230>
  30c3ad:	4c 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%r8
  30c3b4:	48 8b bd 40 ff ff ff 	mov    -0xc0(%rbp),%rdi
  30c3bb:	4c 39 c7             	cmp    %r8,%rdi
  30c3be:	0f 94 c0             	sete   %al
  30c3c1:	48 83 7d 88 00       	cmpq   $0x0,-0x78(%rbp)
  30c3c6:	40 0f 94 c6          	sete   %sil
  30c3ca:	4c 39 c7             	cmp    %r8,%rdi
  30c3cd:	44 89 bd 98 f9 ff ff 	mov    %r15d,-0x668(%rbp)
  30c3d4:	74 2d                	je     30c403 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11c3>
  30c3d6:	48 83 bd 58 ff ff ff 	cmpq   $0x0,-0xa8(%rbp)
  30c3dd:	00 
  30c3de:	75 23                	jne    30c403 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11c3>
  30c3e0:	0f b6 3f             	movzbl (%rdi),%edi
  30c3e3:	40 80 e7 fe          	and    $0xfe,%dil
  30c3e7:	40 80 ff 1c          	cmp    $0x1c,%dil
  30c3eb:	75 16                	jne    30c403 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11c3>
  30c3ed:	45 31 ff             	xor    %r15d,%r15d
  30c3f0:	44 8b 95 74 f9 ff ff 	mov    -0x68c(%rbp),%r10d
  30c3f7:	4c 8b b5 68 f9 ff ff 	mov    -0x698(%rbp),%r14
  30c3fe:	e9 72 02 00 00       	jmp    30c675 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1435>
  30c403:	4c 8b 45 b8          	mov    -0x48(%rbp),%r8
  30c407:	48 8b 7d c0          	mov    -0x40(%rbp),%rdi
  30c40b:	49 39 f8             	cmp    %rdi,%r8
  30c40e:	44 8b 95 74 f9 ff ff 	mov    -0x68c(%rbp),%r10d
  30c415:	4c 8b b5 68 f9 ff ff 	mov    -0x698(%rbp),%r14
  30c41c:	0f 84 50 02 00 00    	je     30c672 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1432>
  30c422:	49 83 c0 78          	add    $0x78,%r8
  30c426:	eb 19                	jmp    30c441 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1201>
  30c428:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
  30c42f:	00 
  30c430:	45 31 ff             	xor    %r15d,%r15d
  30c433:	45 84 ff             	test   %r15b,%r15b
  30c436:	75 2f                	jne    30c467 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1227>
  30c438:	49 39 f8             	cmp    %rdi,%r8
  30c43b:	4d 8d 40 78          	lea    0x78(%r8),%r8
  30c43f:	74 26                	je     30c467 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1227>
  30c441:	49 83 78 a0 00       	cmpq   $0x0,-0x60(%r8)
  30c446:	75 e8                	jne    30c430 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11f0>
  30c448:	4d 8b 48 88          	mov    -0x78(%r8),%r9
  30c44c:	4d 3b 48 90          	cmp    -0x70(%r8),%r9
  30c450:	74 de                	je     30c430 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11f0>
  30c452:	45 0f b6 09          	movzbl (%r9),%r9d
  30c456:	41 80 e1 fe          	and    $0xfe,%r9b
  30c45a:	41 80 f9 1c          	cmp    $0x1c,%r9b
  30c45e:	41 0f 94 c7          	sete   %r15b
  30c462:	45 84 ff             	test   %r15b,%r15b
  30c465:	74 d1                	je     30c438 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11f8>
  30c467:	41 80 f7 01          	xor    $0x1,%r15b
  30c46b:	e9 05 02 00 00       	jmp    30c675 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1435>
  30c470:	48 8b 8d 78 f9 ff ff 	mov    -0x688(%rbp),%rcx
  30c477:	c5 fa 6f 01          	vmovdqu (%rcx),%xmm0
  30c47b:	c4 c1 7a 7f 06       	vmovdqu %xmm0,(%r14)
  30c480:	48 8b 41 10          	mov    0x10(%rcx),%rax
  30c484:	49 89 46 10          	mov    %rax,0x10(%r14)
  30c488:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30c48c:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30c490:	48 c7 41 10 00 00 00 	movq   $0x0,0x10(%rcx)
  30c497:	00 
  30c498:	41 c6 46 18 01       	movb   $0x1,0x18(%r14)
  30c49d:	e9 56 0c 00 00       	jmp    30d0f8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1eb8>
  30c4a2:	4c 89 ef             	mov    %r13,%rdi
  30c4a5:	c5 f8 77             	vzeroupper
  30c4a8:	e8 b3 e6 fa ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30c4ad:	4d 8b 34 24          	mov    (%r12),%r14
  30c4b1:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30c4b8:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30c4bf:	c4 e2 f3 f6 cb       	mulx   %rbx,%rcx,%rcx
  30c4c4:	48 c1 e9 07          	shr    $0x7,%rcx
  30c4c8:	48 8b 04 c8          	mov    (%rax,%rcx,8),%rax
  30c4cc:	48 69 c9 aa 00 00 00 	imul   $0xaa,%rcx,%rcx
  30c4d3:	48 89 d6             	mov    %rdx,%rsi
  30c4d6:	48 29 ce             	sub    %rcx,%rsi
  30c4d9:	48 8d 0c 76          	lea    (%rsi,%rsi,2),%rcx
  30c4dd:	48 8b 3c c8          	mov    (%rax,%rcx,8),%rdi
  30c4e1:	48 85 ff             	test   %rdi,%rdi
  30c4e4:	74 1b                	je     30c501 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x12c1>
  30c4e6:	48 8d 04 c8          	lea    (%rax,%rcx,8),%rax
  30c4ea:	48 89 78 08          	mov    %rdi,0x8(%rax)
  30c4ee:	48 8b 70 10          	mov    0x10(%rax),%rsi
  30c4f2:	48 29 fe             	sub    %rdi,%rsi
  30c4f5:	e8 c6 e4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c4fa:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30c501:	49 ff 8e 68 08 00 00 	decq   0x868(%r14)
  30c508:	48 ff c2             	inc    %rdx
  30c50b:	49 89 96 60 08 00 00 	mov    %rdx,0x860(%r14)
  30c512:	48 81 fa 54 01 00 00 	cmp    $0x154,%rdx
  30c519:	72 27                	jb     30c542 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1302>
  30c51b:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30c522:	48 8b 38             	mov    (%rax),%rdi
  30c525:	be f0 0f 00 00       	mov    $0xff0,%esi
  30c52a:	e8 91 e4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c52f:	49 83 86 48 08 00 00 	addq   $0x8,0x848(%r14)
  30c536:	08 
  30c537:	49 81 86 60 08 00 00 	addq   $0xffffffffffffff56,0x860(%r14)
  30c53e:	56 ff ff ff 
  30c542:	49 8b 04 24          	mov    (%r12),%rax
  30c546:	41 b7 01             	mov    $0x1,%r15b
  30c549:	48 83 b8 68 08 00 00 	cmpq   $0x0,0x868(%rax)
  30c550:	00 
  30c551:	0f 84 a8 0a 00 00    	je     30cfff <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1dbf>
  30c557:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30c55e:	eb 19                	jmp    30c579 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1339>
  30c560:	49 8b 04 24          	mov    (%r12),%rax
  30c564:	48 83 b8 68 08 00 00 	cmpq   $0x0,0x868(%rax)
  30c56b:	00 
  30c56c:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30c573:	0f 84 da fd ff ff    	je     30c353 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1113>
  30c579:	48 8b 88 48 08 00 00 	mov    0x848(%rax),%rcx
  30c580:	48 8b 90 60 08 00 00 	mov    0x860(%rax),%rdx
  30c587:	c4 e2 fb f6 c3       	mulx   %rbx,%rax,%rax
  30c58c:	48 c1 e8 07          	shr    $0x7,%rax
  30c590:	48 8b 0c c1          	mov    (%rcx,%rax,8),%rcx
  30c594:	48 69 c0 aa 00 00 00 	imul   $0xaa,%rax,%rax
  30c59b:	48 29 c2             	sub    %rax,%rdx
  30c59e:	49 8b 75 08          	mov    0x8(%r13),%rsi
  30c5a2:	48 89 f0             	mov    %rsi,%rax
  30c5a5:	49 2b 45 00          	sub    0x0(%r13),%rax
  30c5a9:	48 8d 3c 52          	lea    (%rdx,%rdx,2),%rdi
  30c5ad:	48 8b 14 f9          	mov    (%rcx,%rdi,8),%rdx
  30c5b1:	48 8b 4c f9 08       	mov    0x8(%rcx,%rdi,8),%rcx
  30c5b6:	49 89 c8             	mov    %rcx,%r8
  30c5b9:	49 29 d0             	sub    %rdx,%r8
  30c5bc:	4c 01 c0             	add    %r8,%rax
  30c5bf:	48 3b 85 98 f9 ff ff 	cmp    -0x668(%rbp),%rax
  30c5c6:	0f 87 87 fd ff ff    	ja     30c353 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1113>
  30c5cc:	4c 89 ef             	mov    %r13,%rdi
  30c5cf:	e8 8c e5 fa ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30c5d4:	4d 8b 34 24          	mov    (%r12),%r14
  30c5d8:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30c5df:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30c5e6:	c4 e2 f3 f6 cb       	mulx   %rbx,%rcx,%rcx
  30c5eb:	48 c1 e9 07          	shr    $0x7,%rcx
  30c5ef:	48 8b 04 c8          	mov    (%rax,%rcx,8),%rax
  30c5f3:	48 69 c9 aa 00 00 00 	imul   $0xaa,%rcx,%rcx
  30c5fa:	48 89 d6             	mov    %rdx,%rsi
  30c5fd:	48 29 ce             	sub    %rcx,%rsi
  30c600:	48 8d 0c 76          	lea    (%rsi,%rsi,2),%rcx
  30c604:	48 8b 3c c8          	mov    (%rax,%rcx,8),%rdi
  30c608:	48 85 ff             	test   %rdi,%rdi
  30c60b:	74 1b                	je     30c628 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x13e8>
  30c60d:	48 8d 04 c8          	lea    (%rax,%rcx,8),%rax
  30c611:	48 89 78 08          	mov    %rdi,0x8(%rax)
  30c615:	48 8b 70 10          	mov    0x10(%rax),%rsi
  30c619:	48 29 fe             	sub    %rdi,%rsi
  30c61c:	e8 9f e3 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c621:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30c628:	49 ff 8e 68 08 00 00 	decq   0x868(%r14)
  30c62f:	48 ff c2             	inc    %rdx
  30c632:	49 89 96 60 08 00 00 	mov    %rdx,0x860(%r14)
  30c639:	48 81 fa 54 01 00 00 	cmp    $0x154,%rdx
  30c640:	0f 82 1a ff ff ff    	jb     30c560 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1320>
  30c646:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30c64d:	48 8b 38             	mov    (%rax),%rdi
  30c650:	be f0 0f 00 00       	mov    $0xff0,%esi
  30c655:	e8 66 e3 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c65a:	49 83 86 48 08 00 00 	addq   $0x8,0x848(%r14)
  30c661:	08 
  30c662:	49 81 86 60 08 00 00 	addq   $0xffffffffffffff56,0x860(%r14)
  30c669:	56 ff ff ff 
  30c66d:	e9 ee fe ff ff       	jmp    30c560 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1320>
  30c672:	41 b7 01             	mov    $0x1,%r15b
  30c675:	40 20 f0             	and    %sil,%al
  30c678:	41 0f b6 76 14       	movzbl 0x14(%r14),%esi
  30c67d:	48 8b bd 50 f9 ff ff 	mov    -0x6b0(%rbp),%rdi
  30c684:	48 8b 7f 78          	mov    0x78(%rdi),%rdi
  30c688:	48 29 d1             	sub    %rdx,%rcx
  30c68b:	48 01 f1             	add    %rsi,%rcx
  30c68e:	4c 8d 04 0f          	lea    (%rdi,%rcx,1),%r8
  30c692:	49 83 c0 05          	add    $0x5,%r8
  30c696:	49 8b 0c 24          	mov    (%r12),%rcx
  30c69a:	83 79 60 01          	cmpl   $0x1,0x60(%rcx)
  30c69e:	75 49                	jne    30c6e9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x14a9>
  30c6a0:	48 8b 8d 80 f9 ff ff 	mov    -0x680(%rbp),%rcx
  30c6a7:	80 b9 82 00 00 00 00 	cmpb   $0x0,0x82(%rcx)
  30c6ae:	75 39                	jne    30c6e9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x14a9>
  30c6b0:	48 8b b5 80 f9 ff ff 	mov    -0x680(%rbp),%rsi
  30c6b7:	48 8b 8e 98 00 00 00 	mov    0x98(%rsi),%rcx
  30c6be:	48 ba 55 55 55 55 55 	movabs $0x5555555555555555,%rdx
  30c6c5:	55 55 55 
  30c6c8:	48 39 d1             	cmp    %rdx,%rcx
  30c6cb:	48 0f 42 d1          	cmovb  %rcx,%rdx
  30c6cf:	48 8d 0c 52          	lea    (%rdx,%rdx,2),%rcx
  30c6d3:	48 2b 8e a0 00 00 00 	sub    0xa0(%rsi),%rcx
  30c6da:	0f 82 ab 05 00 00    	jb     30cc8b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a4b>
  30c6e0:	49 39 c8             	cmp    %rcx,%r8
  30c6e3:	0f 87 a2 05 00 00    	ja     30cc8b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a4b>
  30c6e9:	45 84 d2             	test   %r10b,%r10b
  30c6ec:	75 3c                	jne    30c72a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x14ea>
  30c6ee:	f6 d0                	not    %al
  30c6f0:	0a 85 98 f9 ff ff    	or     -0x668(%rbp),%al
  30c6f6:	0a 85 af f9 ff ff    	or     -0x651(%rbp),%al
  30c6fc:	44 20 f8             	and    %r15b,%al
  30c6ff:	3c 01                	cmp    $0x1,%al
  30c701:	75 27                	jne    30c72a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x14ea>
  30c703:	48 8b 85 80 f9 ff ff 	mov    -0x680(%rbp),%rax
  30c70a:	48 8d b8 e0 00 00 00 	lea    0xe0(%rax),%rdi
  30c711:	4c 89 c6             	mov    %r8,%rsi
  30c714:	4c 89 c3             	mov    %r8,%rbx
  30c717:	c5 f8 77             	vzeroupper
  30c71a:	e8 31 02 04 00       	call   34c950 <cnetmod::quic::quic_recovery_controller@cnetmod.protocol.quic::can_send_datagram(unsigned long) const>
  30c71f:	49 89 d8             	mov    %rbx,%r8
  30c722:	84 c0                	test   %al,%al
  30c724:	0f 84 24 09 00 00    	je     30d04e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e0e>
  30c72a:	48 8b 8d 80 f9 ff ff 	mov    -0x680(%rbp),%rcx
  30c731:	48 8b 81 a8 00 00 00 	mov    0xa8(%rcx),%rax
  30c738:	48 89 85 a0 f9 ff ff 	mov    %rax,-0x660(%rbp)
  30c73f:	48 ff c0             	inc    %rax
  30c742:	48 89 81 a8 00 00 00 	mov    %rax,0xa8(%rcx)
  30c749:	48 8b 8d 78 f9 ff ff 	mov    -0x688(%rbp),%rcx
  30c750:	48 8b 11             	mov    (%rcx),%rdx
  30c753:	48 89 95 d0 fd ff ff 	mov    %rdx,-0x230(%rbp)
  30c75a:	48 8b 59 08          	mov    0x8(%rcx),%rbx
  30c75e:	48 89 9d d8 fd ff ff 	mov    %rbx,-0x228(%rbp)
  30c765:	48 8b 41 10          	mov    0x10(%rcx),%rax
  30c769:	48 89 85 e0 fd ff ff 	mov    %rax,-0x220(%rbp)
  30c770:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30c774:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30c778:	48 c7 41 10 00 00 00 	movq   $0x0,0x10(%rcx)
  30c77f:	00 
  30c780:	49 89 c5             	mov    %rax,%r13
  30c783:	48 89 95 60 f9 ff ff 	mov    %rdx,-0x6a0(%rbp)
  30c78a:	49 29 d5             	sub    %rdx,%r13
  30c78d:	4d 39 e8             	cmp    %r13,%r8
  30c790:	0f 86 90 00 00 00    	jbe    30c826 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x15e6>
  30c796:	4d 85 c0             	test   %r8,%r8
  30c799:	0f 88 7c 0c 00 00    	js     30d41b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x21db>
  30c79f:	4c 89 c7             	mov    %r8,%rdi
  30c7a2:	4c 89 85 78 f9 ff ff 	mov    %r8,-0x688(%rbp)
  30c7a9:	c5 f8 77             	vzeroupper
  30c7ac:	e8 7f e2 dd ff       	call   eaa30 <_Znwm$plt>
  30c7b1:	48 8b b5 60 f9 ff ff 	mov    -0x6a0(%rbp),%rsi
  30c7b8:	48 29 f3             	sub    %rsi,%rbx
  30c7bb:	48 8d 0c 18          	lea    (%rax,%rbx,1),%rcx
  30c7bf:	48 89 8d 50 f9 ff ff 	mov    %rcx,-0x6b0(%rbp)
  30c7c6:	4c 8b b5 78 f9 ff ff 	mov    -0x688(%rbp),%r14
  30c7cd:	49 01 c6             	add    %rax,%r14
  30c7d0:	48 89 c7             	mov    %rax,%rdi
  30c7d3:	48 89 da             	mov    %rbx,%rdx
  30c7d6:	48 89 c3             	mov    %rax,%rbx
  30c7d9:	e8 c2 e6 dd ff       	call   eaea0 <memcpy$plt>
  30c7de:	48 8b bd 60 f9 ff ff 	mov    -0x6a0(%rbp),%rdi
  30c7e5:	48 89 9d d0 fd ff ff 	mov    %rbx,-0x230(%rbp)
  30c7ec:	48 8b 9d 50 f9 ff ff 	mov    -0x6b0(%rbp),%rbx
  30c7f3:	48 89 9d d8 fd ff ff 	mov    %rbx,-0x228(%rbp)
  30c7fa:	4c 89 b5 e0 fd ff ff 	mov    %r14,-0x220(%rbp)
  30c801:	4c 89 f0             	mov    %r14,%rax
  30c804:	48 85 ff             	test   %rdi,%rdi
  30c807:	4c 8b b5 68 f9 ff ff 	mov    -0x698(%rbp),%r14
  30c80e:	74 16                	je     30c826 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x15e6>
  30c810:	4c 89 ee             	mov    %r13,%rsi
  30c813:	e8 a8 e1 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c818:	48 8b 9d d8 fd ff ff 	mov    -0x228(%rbp),%rbx
  30c81f:	48 8b 85 e0 fd ff ff 	mov    -0x220(%rbp),%rax
  30c826:	49 8b 0c 24          	mov    (%r12),%rcx
  30c82a:	48 8b 89 58 03 00 00 	mov    0x358(%rcx),%rcx
  30c831:	0f b6 89 59 06 00 00 	movzbl 0x659(%rcx),%ecx
  30c838:	c0 e1 02             	shl    $0x2,%cl
  30c83b:	80 c9 43             	or     $0x43,%cl
  30c83e:	88 8d f0 f9 ff ff    	mov    %cl,-0x610(%rbp)
  30c844:	48 89 9d 90 fa ff ff 	mov    %rbx,-0x570(%rbp)
  30c84b:	48 8d 95 90 fa ff ff 	lea    -0x570(%rbp),%rdx
  30c852:	48 89 95 40 fd ff ff 	mov    %rdx,-0x2c0(%rbp)
  30c859:	4c 8d a5 f0 f9 ff ff 	lea    -0x610(%rbp),%r12
  30c860:	4c 89 a5 48 fd ff ff 	mov    %r12,-0x2b8(%rbp)
  30c867:	48 8d 95 d0 fd ff ff 	lea    -0x230(%rbp),%rdx
  30c86e:	48 89 95 50 fd ff ff 	mov    %rdx,-0x2b0(%rbp)
  30c875:	48 39 c3             	cmp    %rax,%rbx
  30c878:	0f 83 d9 0a 00 00    	jae    30d357 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2117>
  30c87e:	88 0b                	mov    %cl,(%rbx)
  30c880:	48 ff c3             	inc    %rbx
  30c883:	48 89 9d d8 fd ff ff 	mov    %rbx,-0x228(%rbp)
  30c88a:	45 0f b6 46 14       	movzbl 0x14(%r14),%r8d
  30c88f:	4b 8d 0c 06          	lea    (%r14,%r8,1),%rcx
  30c893:	4c 8d ad d0 fd ff ff 	lea    -0x230(%rbp),%r13
  30c89a:	4c 89 ef             	mov    %r13,%rdi
  30c89d:	48 89 de             	mov    %rbx,%rsi
  30c8a0:	4c 89 f2             	mov    %r14,%rdx
  30c8a3:	c5 f8 77             	vzeroupper
  30c8a6:	e8 a5 dc fa ff       	call   2ba550 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::byte const*, std::byte const*>(std::__1::__wrap_iter<std::byte const*>, std::byte const*, std::byte const*, long)>
  30c8ab:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30c8b2:	44 89 c8             	mov    %r9d,%eax
  30c8b5:	c1 e8 18             	shr    $0x18,%eax
  30c8b8:	88 85 f0 f9 ff ff    	mov    %al,-0x610(%rbp)
  30c8be:	4c 8b b5 d8 fd ff ff 	mov    -0x228(%rbp),%r14
  30c8c5:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c8cc:	48 8d 8d 90 fa ff ff 	lea    -0x570(%rbp),%rcx
  30c8d3:	48 89 8d 40 fd ff ff 	mov    %rcx,-0x2c0(%rbp)
  30c8da:	4c 89 a5 48 fd ff ff 	mov    %r12,-0x2b8(%rbp)
  30c8e1:	4c 89 ad 50 fd ff ff 	mov    %r13,-0x2b0(%rbp)
  30c8e8:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c8ef:	0f 83 84 0a 00 00    	jae    30d379 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2139>
  30c8f5:	41 88 06             	mov    %al,(%r14)
  30c8f8:	49 ff c6             	inc    %r14
  30c8fb:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c902:	44 89 c8             	mov    %r9d,%eax
  30c905:	c1 e8 10             	shr    $0x10,%eax
  30c908:	88 85 f0 f9 ff ff    	mov    %al,-0x610(%rbp)
  30c90e:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c915:	48 8d 8d 90 fa ff ff 	lea    -0x570(%rbp),%rcx
  30c91c:	48 89 8d 40 fd ff ff 	mov    %rcx,-0x2c0(%rbp)
  30c923:	4c 89 a5 48 fd ff ff 	mov    %r12,-0x2b8(%rbp)
  30c92a:	4c 89 ad 50 fd ff ff 	mov    %r13,-0x2b0(%rbp)
  30c931:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c938:	0f 83 5a 0a 00 00    	jae    30d398 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2158>
  30c93e:	41 88 06             	mov    %al,(%r14)
  30c941:	49 ff c6             	inc    %r14
  30c944:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c94b:	44 89 c8             	mov    %r9d,%eax
  30c94e:	c1 e8 08             	shr    $0x8,%eax
  30c951:	88 85 f0 f9 ff ff    	mov    %al,-0x610(%rbp)
  30c957:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c95e:	48 8d 8d 90 fa ff ff 	lea    -0x570(%rbp),%rcx
  30c965:	48 89 8d 40 fd ff ff 	mov    %rcx,-0x2c0(%rbp)
  30c96c:	4c 89 a5 48 fd ff ff 	mov    %r12,-0x2b8(%rbp)
  30c973:	4c 89 ad 50 fd ff ff 	mov    %r13,-0x2b0(%rbp)
  30c97a:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c981:	0f 83 30 0a 00 00    	jae    30d3b7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2177>
  30c987:	41 88 06             	mov    %al,(%r14)
  30c98a:	49 ff c6             	inc    %r14
  30c98d:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c994:	44 88 8d f0 f9 ff ff 	mov    %r9b,-0x610(%rbp)
  30c99b:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c9a2:	48 8d 85 90 fa ff ff 	lea    -0x570(%rbp),%rax
  30c9a9:	48 89 85 40 fd ff ff 	mov    %rax,-0x2c0(%rbp)
  30c9b0:	4c 89 a5 48 fd ff ff 	mov    %r12,-0x2b8(%rbp)
  30c9b7:	4c 89 ad 50 fd ff ff 	mov    %r13,-0x2b0(%rbp)
  30c9be:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c9c5:	0f 83 0b 0a 00 00    	jae    30d3d6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2196>
  30c9cb:	45 88 0e             	mov    %r9b,(%r14)
  30c9ce:	49 ff c6             	inc    %r14
  30c9d1:	48 8b 9d 48 f9 ff ff 	mov    -0x6b8(%rbp),%rbx
  30c9d8:	48 8b 85 88 f9 ff ff 	mov    -0x678(%rbp),%rax
  30c9df:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c9e6:	4c 2b b5 d0 fd ff ff 	sub    -0x230(%rbp),%r14
  30c9ed:	48 8b 10             	mov    (%rax),%rdx
  30c9f0:	48 8b 48 08          	mov    0x8(%rax),%rcx
  30c9f4:	48 29 d1             	sub    %rdx,%rcx
  30c9f7:	48 8b 85 80 f9 ff ff 	mov    -0x680(%rbp),%rax
  30c9fe:	8b 00                	mov    (%rax),%eax
  30ca00:	89 04 24             	mov    %eax,(%rsp)
  30ca03:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30ca0a:	4c 8d 85 d0 fd ff ff 	lea    -0x230(%rbp),%r8
  30ca11:	48 89 de             	mov    %rbx,%rsi
  30ca14:	e8 f7 35 03 00       	call   340010 <cnetmod::quic::append_sealed_payload@cnetmod.protocol.quic(cnetmod::quic::quic_level_keys@cnetmod.protocol.quic const&, std::__1::span<std::byte const, 18446744073709551615ul>, std::__1::vector<std::byte, std::__1::allocator<std::byte> >&, unsigned long, unsigned int)>
  30ca19:	80 bd 50 fd ff ff 00 	cmpb   $0x0,-0x2b0(%rbp)
  30ca20:	0f 84 45 02 00 00    	je     30cc6b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a2b>
  30ca26:	49 83 c6 fc          	add    $0xfffffffffffffffc,%r14
  30ca2a:	48 8b 95 d0 fd ff ff 	mov    -0x230(%rbp),%rdx
  30ca31:	48 8b 8d d8 fd ff ff 	mov    -0x228(%rbp),%rcx
  30ca38:	48 29 d1             	sub    %rdx,%rcx
  30ca3b:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30ca42:	48 89 de             	mov    %rbx,%rsi
  30ca45:	4d 89 f0             	mov    %r14,%r8
  30ca48:	45 31 c9             	xor    %r9d,%r9d
  30ca4b:	e8 d0 2c 03 00       	call   33f720 <cnetmod::quic::protect_header@cnetmod.protocol.quic(cnetmod::quic::quic_level_keys@cnetmod.protocol.quic const&, std::__1::span<std::byte, 18446744073709551615ul>, unsigned long, bool)>
  30ca50:	80 bd 50 fd ff ff 00 	cmpb   $0x0,-0x2b0(%rbp)
  30ca57:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30ca5e:	0f 84 a7 05 00 00    	je     30d00b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1dcb>
  30ca64:	48 8b 85 d8 fd ff ff 	mov    -0x228(%rbp),%rax
  30ca6b:	48 2b 85 d0 fd ff ff 	sub    -0x230(%rbp),%rax
  30ca72:	48 8b 95 80 f9 ff ff 	mov    -0x680(%rbp),%rdx
  30ca79:	48 03 82 a0 00 00 00 	add    0xa0(%rdx),%rax
  30ca80:	48 c7 c1 ff ff ff ff 	mov    $0xffffffffffffffff,%rcx
  30ca87:	48 0f 43 c8          	cmovae %rax,%rcx
  30ca8b:	48 89 8a a0 00 00 00 	mov    %rcx,0xa0(%rdx)
  30ca92:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30ca99:	75 0c                	jne    30caa7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1867>
  30ca9b:	48 8b bd 40 f9 ff ff 	mov    -0x6c0(%rbp),%rdi
  30caa2:	e8 89 42 ff ff       	call   300d30 <std::__1::set<unsigned long, std::__1::less<unsigned long>, std::__1::allocator<unsigned long> >::clear[abi:nqe220108]()>
  30caa7:	80 bd af f9 ff ff 01 	cmpb   $0x1,-0x651(%rbp)
  30caae:	4c 8b a5 80 f9 ff ff 	mov    -0x680(%rbp),%r12
  30cab5:	75 2c                	jne    30cae3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x18a3>
  30cab7:	48 8b 85 58 f9 ff ff 	mov    -0x6a8(%rbp),%rax
  30cabe:	49 89 84 24 70 1c 00 	mov    %rax,0x1c70(%r12)
  30cac5:	00 
  30cac6:	41 c6 84 24 78 1c 00 	movb   $0x1,0x1c78(%r12)
  30cacd:	00 01 
  30cacf:	41 80 bc 24 68 1c 00 	cmpb   $0x1,0x1c68(%r12)
  30cad6:	00 01 
  30cad8:	75 09                	jne    30cae3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x18a3>
  30cada:	41 c6 84 24 68 1c 00 	movb   $0x0,0x1c68(%r12)
  30cae1:	00 00 
  30cae3:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
  30caea:	48 33 85 48 ff ff ff 	xor    -0xb8(%rbp),%rax
  30caf1:	48 0b 45 88          	or     -0x78(%rbp),%rax
  30caf5:	0f 95 c0             	setne  %al
  30caf8:	44 0f b6 ad af f9 ff 	movzbl -0x651(%rbp),%r13d
  30caff:	ff 
  30cb00:	8b 8d 98 f9 ff ff    	mov    -0x668(%rbp),%ecx
  30cb06:	44 08 e9             	or     %r13b,%cl
  30cb09:	08 c1                	or     %al,%cl
  30cb0b:	41 20 cf             	and    %cl,%r15b
  30cb0e:	49 81 c4 e0 00 00 00 	add    $0xe0,%r12
  30cb15:	48 8b 9d d8 fd ff ff 	mov    -0x228(%rbp),%rbx
  30cb1c:	48 2b 9d d0 fd ff ff 	sub    -0x230(%rbp),%rbx
  30cb23:	e8 18 d8 dd ff       	call   ea340 <_ZNSt3__16chrono12steady_clock3nowEv$plt>
  30cb28:	c5 f8 28 85 40 ff ff 	vmovaps -0xc0(%rbp),%xmm0
  30cb2f:	ff 
  30cb30:	c5 f8 29 85 a0 fa ff 	vmovaps %xmm0,-0x560(%rbp)
  30cb37:	ff 
  30cb38:	48 8b 8d 50 ff ff ff 	mov    -0xb0(%rbp),%rcx
  30cb3f:	48 89 8d b0 fa ff ff 	mov    %rcx,-0x550(%rbp)
  30cb46:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
  30cb4a:	c5 f8 29 85 40 ff ff 	vmovaps %xmm0,-0xc0(%rbp)
  30cb51:	ff 
  30cb52:	48 c7 85 50 ff ff ff 	movq   $0x0,-0xb0(%rbp)
  30cb59:	00 00 00 00 
  30cb5d:	c5 f8 10 8d 58 ff ff 	vmovups -0xa8(%rbp),%xmm1
  30cb64:	ff 
  30cb65:	c5 f8 11 8d b8 fa ff 	vmovups %xmm1,-0x548(%rbp)
  30cb6c:	ff 
  30cb6d:	48 8d 8d 58 ff ff ff 	lea    -0xa8(%rbp),%rcx
  30cb74:	c5 f8 11 01          	vmovups %xmm0,(%rcx)
  30cb78:	c5 fc 10 4d 88       	vmovups -0x78(%rbp),%ymm1
  30cb7d:	c5 fc 10 55 93       	vmovups -0x6d(%rbp),%ymm2
  30cb82:	c5 fc 11 95 f3 fa ff 	vmovups %ymm2,-0x50d(%rbp)
  30cb89:	ff 
  30cb8a:	c5 fc 11 8d e8 fa ff 	vmovups %ymm1,-0x518(%rbp)
  30cb91:	ff 
  30cb92:	c5 fc 10 8d 68 ff ff 	vmovups -0x98(%rbp),%ymm1
  30cb99:	ff 
  30cb9a:	c5 fc 11 8d c8 fa ff 	vmovups %ymm1,-0x538(%rbp)
  30cba1:	ff 
  30cba2:	c5 f8 10 4d b8       	vmovups -0x48(%rbp),%xmm1
  30cba7:	c5 f8 11 8d 18 fb ff 	vmovups %xmm1,-0x4e8(%rbp)
  30cbae:	ff 
  30cbaf:	48 8b 4d c8          	mov    -0x38(%rbp),%rcx
  30cbb3:	48 89 8d 28 fb ff ff 	mov    %rcx,-0x4d8(%rbp)
  30cbba:	48 8d 4d b8          	lea    -0x48(%rbp),%rcx
  30cbbe:	c5 f8 11 01          	vmovups %xmm0,(%rcx)
  30cbc2:	48 c7 41 10 00 00 00 	movq   $0x0,0x10(%rcx)
  30cbc9:	00 
  30cbca:	48 8b 8d 58 f9 ff ff 	mov    -0x6a8(%rbp),%rcx
  30cbd1:	48 89 8d f8 f9 ff ff 	mov    %rcx,-0x608(%rbp)
  30cbd8:	44 88 ad 00 fa ff ff 	mov    %r13b,-0x600(%rbp)
  30cbdf:	8b 8d 08 fa ff ff    	mov    -0x5f8(%rbp),%ecx
  30cbe5:	8b 95 0b fa ff ff    	mov    -0x5f5(%rbp),%edx
  30cbeb:	89 8d 01 fa ff ff    	mov    %ecx,-0x5ff(%rbp)
  30cbf1:	89 95 04 fa ff ff    	mov    %edx,-0x5fc(%rbp)
  30cbf7:	c5 fa 6f 85 f8 f9 ff 	vmovdqu -0x608(%rbp),%xmm0
  30cbfe:	ff 
  30cbff:	c5 fa 7f 44 24 08    	vmovdqu %xmm0,0x8(%rsp)
  30cc05:	0f b6 8d 74 f9 ff ff 	movzbl -0x68c(%rbp),%ecx
  30cc0c:	89 4c 24 18          	mov    %ecx,0x18(%rsp)
  30cc10:	48 8d 8d a0 fa ff ff 	lea    -0x560(%rbp),%rcx
  30cc17:	48 89 0c 24          	mov    %rcx,(%rsp)
  30cc1b:	45 0f b6 c7          	movzbl %r15b,%r8d
  30cc1f:	4c 89 e7             	mov    %r12,%rdi
  30cc22:	48 8b b5 a0 f9 ff ff 	mov    -0x660(%rbp),%rsi
  30cc29:	48 89 da             	mov    %rbx,%rdx
  30cc2c:	48 89 c1             	mov    %rax,%rcx
  30cc2f:	41 b9 02 00 00 00    	mov    $0x2,%r9d
  30cc35:	c5 f8 77             	vzeroupper
  30cc38:	e8 43 d0 03 00       	call   349c80 <cnetmod::quic::quic_recovery_controller@cnetmod.protocol.quic::on_packet_sent(unsigned long, unsigned long, std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >, bool, cnetmod::quic::pn_space@cnetmod.protocol.quic, cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic, std::__1::optional<unsigned long>, bool)>
  30cc3d:	48 8d bd a0 fa ff ff 	lea    -0x560(%rbp),%rdi
  30cc44:	e8 a7 38 00 00       	call   3104f0 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30cc49:	c5 f9 6f 85 d0 fd ff 	vmovdqa -0x230(%rbp),%xmm0
  30cc50:	ff 
  30cc51:	c4 c1 7a 7f 06       	vmovdqu %xmm0,(%r14)
  30cc56:	48 8b 85 e0 fd ff ff 	mov    -0x220(%rbp),%rax
  30cc5d:	49 89 46 10          	mov    %rax,0x10(%r14)
  30cc61:	41 c6 46 18 01       	movb   $0x1,0x18(%r14)
  30cc66:	e9 8d 04 00 00       	jmp    30d0f8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1eb8>
  30cc6b:	8b 9d 40 fd ff ff    	mov    -0x2c0(%rbp),%ebx
  30cc71:	85 db                	test   %ebx,%ebx
  30cc73:	0f 84 b2 03 00 00    	je     30d02b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1deb>
  30cc79:	8b 8d 44 fd ff ff    	mov    -0x2bc(%rbp),%ecx
  30cc7f:	48 8b 85 48 fd ff ff 	mov    -0x2b8(%rbp),%rax
  30cc86:	e9 aa 03 00 00       	jmp    30d035 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1df5>
  30cc8b:	84 c0                	test   %al,%al
  30cc8d:	0f 85 f5 03 00 00    	jne    30d088 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e48>
  30cc93:	4c 8b 75 c0          	mov    -0x40(%rbp),%r14
  30cc97:	4c 2b 75 b8          	sub    -0x48(%rbp),%r14
  30cc9b:	49 c1 fe 03          	sar    $0x3,%r14
  30cc9f:	48 b8 ef ee ee ee ee 	movabs $0xeeeeeeeeeeeeeeef,%rax
  30cca6:	ee ee ee 
  30cca9:	4c 0f af f0          	imul   %rax,%r14
  30ccad:	49 ff c6             	inc    %r14
  30ccb0:	0f 84 d2 03 00 00    	je     30d088 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e48>
  30ccb6:	4c 8d bd f0 fb ff ff 	lea    -0x410(%rbp),%r15
  30ccbd:	4c 8b a5 10 fa ff ff 	mov    -0x5f0(%rbp),%r12
  30ccc4:	e9 de 00 00 00       	jmp    30cda7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b67>
  30ccc9:	48 8d 4b 90          	lea    -0x70(%rbx),%rcx
  30cccd:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30ccd1:	c5 fa 7f 40 88       	vmovdqu %xmm0,-0x78(%rax)
  30ccd6:	48 8d 53 98          	lea    -0x68(%rbx),%rdx
  30ccda:	48 c7 40 98 00 00 00 	movq   $0x0,-0x68(%rax)
  30cce1:	00 
  30cce2:	49 8b 75 00          	mov    0x0(%r13),%rsi
  30cce6:	48 89 70 88          	mov    %rsi,-0x78(%rax)
  30ccea:	48 8d 73 a0          	lea    -0x60(%rbx),%rsi
  30ccee:	48 8d 7b a8          	lea    -0x58(%rbx),%rdi
  30ccf2:	48 83 c3 b0          	add    $0xffffffffffffffb0,%rbx
  30ccf6:	4d 85 f6             	test   %r14,%r14
  30ccf9:	4c 8d 85 48 ff ff ff 	lea    -0xb8(%rbp),%r8
  30cd00:	49 0f 44 c8          	cmove  %r8,%rcx
  30cd04:	48 8b 09             	mov    (%rcx),%rcx
  30cd07:	48 89 48 90          	mov    %rcx,-0x70(%rax)
  30cd0b:	48 8d 8d 50 ff ff ff 	lea    -0xb0(%rbp),%rcx
  30cd12:	48 0f 44 d1          	cmove  %rcx,%rdx
  30cd16:	48 8b 0a             	mov    (%rdx),%rcx
  30cd19:	48 89 48 98          	mov    %rcx,-0x68(%rax)
  30cd1d:	c4 c1 7a 7f 45 00    	vmovdqu %xmm0,0x0(%r13)
  30cd23:	49 c7 45 10 00 00 00 	movq   $0x0,0x10(%r13)
  30cd2a:	00 
  30cd2b:	48 8d 8d 58 ff ff ff 	lea    -0xa8(%rbp),%rcx
  30cd32:	48 0f 44 f1          	cmove  %rcx,%rsi
  30cd36:	48 8b 0e             	mov    (%rsi),%rcx
  30cd39:	48 89 48 a0          	mov    %rcx,-0x60(%rax)
  30cd3d:	48 8d 8d 60 ff ff ff 	lea    -0xa0(%rbp),%rcx
  30cd44:	48 0f 44 f9          	cmove  %rcx,%rdi
  30cd48:	48 8b 0f             	mov    (%rdi),%rcx
  30cd4b:	48 89 48 a8          	mov    %rcx,-0x58(%rax)
  30cd4f:	c5 fa 7f 06          	vmovdqu %xmm0,(%rsi)
  30cd53:	48 8d 8d 68 ff ff ff 	lea    -0x98(%rbp),%rcx
  30cd5a:	48 0f 44 d9          	cmove  %rcx,%rbx
  30cd5e:	c5 fc 10 03          	vmovups (%rbx),%ymm0
  30cd62:	c5 fc 10 4b 20       	vmovups 0x20(%rbx),%ymm1
  30cd67:	c5 fc 10 53 2b       	vmovups 0x2b(%rbx),%ymm2
  30cd6c:	c5 fc 11 50 db       	vmovups %ymm2,-0x25(%rax)
  30cd71:	c5 fc 11 48 d0       	vmovups %ymm1,-0x30(%rax)
  30cd76:	c5 fc 11 40 b0       	vmovups %ymm0,-0x50(%rax)
  30cd7b:	c5 f9 6f 05 4d c2 dc 	vmovdqa -0x233db3(%rip),%xmm0        # d8fd0 <typeinfo name for cnetmod::io_uring_context@cnetmod.io.platform.io_uring+0xbc2>
  30cd82:	ff 
  30cd83:	c4 c1 79 d4 84 24 e8 	vpaddq 0x6e8(%r12),%xmm0,%xmm0
  30cd8a:	06 00 00 
  30cd8d:	c4 c1 7a 7f 84 24 e8 	vmovdqu %xmm0,0x6e8(%r12)
  30cd94:	06 00 00 
  30cd97:	4c 8b a5 10 fa ff ff 	mov    -0x5f0(%rbp),%r12
  30cd9e:	4d 85 f6             	test   %r14,%r14
  30cda1:	0f 84 e1 02 00 00    	je     30d088 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e48>
  30cda7:	49 ff ce             	dec    %r14
  30cdaa:	48 8b 45 b8          	mov    -0x48(%rbp),%rax
  30cdae:	49 6b ce 78          	imul   $0x78,%r14,%rcx
  30cdb2:	48 8d 1c 08          	lea    (%rax,%rcx,1),%rbx
  30cdb6:	4d 85 f6             	test   %r14,%r14
  30cdb9:	4c 8d 6c 08 88       	lea    -0x78(%rax,%rcx,1),%r13
  30cdbe:	48 8d 85 40 ff ff ff 	lea    -0xc0(%rbp),%rax
  30cdc5:	4c 0f 44 e8          	cmove  %rax,%r13
  30cdc9:	80 bd 28 fa ff ff 01 	cmpb   $0x1,-0x5d8(%rbp)
  30cdd0:	75 0c                	jne    30cdde <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b9e>
  30cdd2:	48 8d 85 18 fa ff ff 	lea    -0x5e8(%rbp),%rax
  30cdd9:	4d 39 f4             	cmp    %r14,%r12
  30cddc:	74 42                	je     30ce20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1be0>
  30cdde:	48 8b 85 b0 f9 ff ff 	mov    -0x650(%rbp),%rax
  30cde5:	48 8b 8d b8 f9 ff ff 	mov    -0x648(%rbp),%rcx
  30cdec:	48 39 c8             	cmp    %rcx,%rax
  30cdef:	74 22                	je     30ce13 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1bd3>
  30cdf1:	66 66 66 66 66 66 2e 	data16 data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
  30cdf8:	0f 1f 84 00 00 00 00 
  30cdff:	00 
  30ce00:	4c 39 30             	cmp    %r14,(%rax)
  30ce03:	74 0e                	je     30ce13 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1bd3>
  30ce05:	48 83 c0 18          	add    $0x18,%rax
  30ce09:	48 39 c8             	cmp    %rcx,%rax
  30ce0c:	75 f2                	jne    30ce00 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1bc0>
  30ce0e:	e9 54 01 00 00       	jmp    30cf67 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1d27>
  30ce13:	48 39 c8             	cmp    %rcx,%rax
  30ce16:	0f 84 4b 01 00 00    	je     30cf67 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1d27>
  30ce1c:	48 83 c0 08          	add    $0x8,%rax
  30ce20:	48 8d 4b 90          	lea    -0x70(%rbx),%rcx
  30ce24:	48 8b 95 a0 f9 ff ff 	mov    -0x660(%rbp),%rdx
  30ce2b:	48 8b 3a             	mov    (%rdx),%rdi
  30ce2e:	48 8b 10             	mov    (%rax),%rdx
  30ce31:	48 89 95 b8 fb ff ff 	mov    %rdx,-0x448(%rbp)
  30ce38:	0f b6 50 08          	movzbl 0x8(%rax),%edx
  30ce3c:	88 95 c0 fb ff ff    	mov    %dl,-0x440(%rbp)
  30ce42:	0f b6 40 09          	movzbl 0x9(%rax),%eax
  30ce46:	48 8d 53 98          	lea    -0x68(%rbx),%rdx
  30ce4a:	88 85 c1 fb ff ff    	mov    %al,-0x43f(%rbp)
  30ce50:	49 8b 45 00          	mov    0x0(%r13),%rax
  30ce54:	48 89 85 c8 fb ff ff 	mov    %rax,-0x438(%rbp)
  30ce5b:	48 8d 43 a0          	lea    -0x60(%rbx),%rax
  30ce5f:	48 8d 73 a8          	lea    -0x58(%rbx),%rsi
  30ce63:	48 83 c3 b0          	add    $0xffffffffffffffb0,%rbx
  30ce67:	4d 85 f6             	test   %r14,%r14
  30ce6a:	4c 8d 85 48 ff ff ff 	lea    -0xb8(%rbp),%r8
  30ce71:	49 0f 44 c8          	cmove  %r8,%rcx
  30ce75:	48 8b 09             	mov    (%rcx),%rcx
  30ce78:	48 89 8d d0 fb ff ff 	mov    %rcx,-0x430(%rbp)
  30ce7f:	48 8d 8d 50 ff ff ff 	lea    -0xb0(%rbp),%rcx
  30ce86:	48 0f 44 d1          	cmove  %rcx,%rdx
  30ce8a:	48 8b 0a             	mov    (%rdx),%rcx
  30ce8d:	48 89 8d d8 fb ff ff 	mov    %rcx,-0x428(%rbp)
  30ce94:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30ce98:	c4 c1 7a 7f 45 00    	vmovdqu %xmm0,0x0(%r13)
  30ce9e:	49 c7 45 10 00 00 00 	movq   $0x0,0x10(%r13)
  30cea5:	00 
  30cea6:	48 8d 8d 58 ff ff ff 	lea    -0xa8(%rbp),%rcx
  30cead:	48 0f 44 c1          	cmove  %rcx,%rax
  30ceb1:	48 8b 08             	mov    (%rax),%rcx
  30ceb4:	48 89 8d e0 fb ff ff 	mov    %rcx,-0x420(%rbp)
  30cebb:	48 8d 8d 60 ff ff ff 	lea    -0xa0(%rbp),%rcx
  30cec2:	48 0f 44 f1          	cmove  %rcx,%rsi
  30cec6:	48 8b 0e             	mov    (%rsi),%rcx
  30cec9:	48 89 8d e8 fb ff ff 	mov    %rcx,-0x418(%rbp)
  30ced0:	c5 fa 7f 00          	vmovdqu %xmm0,(%rax)
  30ced4:	48 8d 85 68 ff ff ff 	lea    -0x98(%rbp),%rax
  30cedb:	48 0f 44 d8          	cmove  %rax,%rbx
  30cedf:	c5 fe 6f 03          	vmovdqu (%rbx),%ymm0
  30cee3:	c5 fc 10 4b 20       	vmovups 0x20(%rbx),%ymm1
  30cee8:	c5 fc 10 53 2b       	vmovups 0x2b(%rbx),%ymm2
  30ceed:	c4 c1 7c 11 57 2b    	vmovups %ymm2,0x2b(%r15)
  30cef3:	c4 c1 7c 11 4f 20    	vmovups %ymm1,0x20(%r15)
  30cef9:	c4 c1 7e 7f 07       	vmovdqu %ymm0,(%r15)
  30cefe:	48 8d b5 b8 fb ff ff 	lea    -0x448(%rbp),%rsi
  30cf05:	c5 f8 77             	vzeroupper
  30cf08:	e8 f3 2b ff ff       	call   2ffb00 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::requeue_prioritized_stream_frame(cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline)>
  30cf0d:	48 8b 9d e8 fb ff ff 	mov    -0x418(%rbp),%rbx
  30cf14:	48 85 db             	test   %rbx,%rbx
  30cf17:	74 23                	je     30cf3c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1cfc>
  30cf19:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30cf20:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30cf26:	48 85 c0             	test   %rax,%rax
  30cf29:	75 11                	jne    30cf3c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1cfc>
  30cf2b:	48 8b 03             	mov    (%rbx),%rax
  30cf2e:	48 89 df             	mov    %rbx,%rdi
  30cf31:	ff 50 10             	call   *0x10(%rax)
  30cf34:	48 89 df             	mov    %rbx,%rdi
  30cf37:	e8 e4 d8 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30cf3c:	48 8b bd c8 fb ff ff 	mov    -0x438(%rbp),%rdi
  30cf43:	48 85 ff             	test   %rdi,%rdi
  30cf46:	0f 84 52 fe ff ff    	je     30cd9e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b5e>
  30cf4c:	48 89 bd d0 fb ff ff 	mov    %rdi,-0x430(%rbp)
  30cf53:	48 8b b5 d8 fb ff ff 	mov    -0x428(%rbp),%rsi
  30cf5a:	48 29 fe             	sub    %rdi,%rsi
  30cf5d:	e8 5e da dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30cf62:	e9 37 fe ff ff       	jmp    30cd9e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b5e>
  30cf67:	48 8b 85 a0 f9 ff ff 	mov    -0x660(%rbp),%rax
  30cf6e:	4c 8b 20             	mov    (%rax),%r12
  30cf71:	49 8b 94 24 e8 06 00 	mov    0x6e8(%r12),%rdx
  30cf78:	00 
  30cf79:	48 85 d2             	test   %rdx,%rdx
  30cf7c:	75 18                	jne    30cf96 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1d56>
  30cf7e:	49 8d bc 24 c8 06 00 	lea    0x6c8(%r12),%rdi
  30cf85:	00 
  30cf86:	c5 f8 77             	vzeroupper
  30cf89:	e8 42 ca 00 00       	call   3199d0 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::__add_front_capacity()>
  30cf8e:	49 8b 94 24 e8 06 00 	mov    0x6e8(%r12),%rdx
  30cf95:	00 
  30cf96:	49 8b 8c 24 d0 06 00 	mov    0x6d0(%r12),%rcx
  30cf9d:	00 
  30cf9e:	48 b8 f1 f0 f0 f0 f0 	movabs $0xf0f0f0f0f0f0f0f1,%rax
  30cfa5:	f0 f0 f0 
  30cfa8:	c4 e2 fb f6 c0       	mulx   %rax,%rax,%rax
  30cfad:	48 89 c6             	mov    %rax,%rsi
  30cfb0:	48 c1 ee 05          	shr    $0x5,%rsi
  30cfb4:	48 bf e0 ff ff ff ff 	movabs $0x1fffffffffffffe0,%rdi
  30cfbb:	ff ff 1f 
  30cfbe:	48 21 f8             	and    %rdi,%rax
  30cfc1:	48 8d 04 70          	lea    (%rax,%rsi,2),%rax
  30cfc5:	48 29 c2             	sub    %rax,%rdx
  30cfc8:	48 8b 3c f1          	mov    (%rcx,%rsi,8),%rdi
  30cfcc:	48 6b c2 78          	imul   $0x78,%rdx,%rax
  30cfd0:	48 01 f8             	add    %rdi,%rax
  30cfd3:	49 3b 8c 24 d8 06 00 	cmp    0x6d8(%r12),%rcx
  30cfda:	00 
  30cfdb:	ba 00 00 00 00       	mov    $0x0,%edx
  30cfe0:	48 0f 44 c2          	cmove  %rdx,%rax
  30cfe4:	48 39 f8             	cmp    %rdi,%rax
  30cfe7:	0f 85 dc fc ff ff    	jne    30ccc9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a89>
  30cfed:	48 8b 44 f1 f8       	mov    -0x8(%rcx,%rsi,8),%rax
  30cff2:	b9 f0 0f 00 00       	mov    $0xff0,%ecx
  30cff7:	48 01 c8             	add    %rcx,%rax
  30cffa:	e9 ca fc ff ff       	jmp    30ccc9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a89>
  30cfff:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30d006:	e9 48 f3 ff ff       	jmp    30c353 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1113>
  30d00b:	8b 9d 40 fd ff ff    	mov    -0x2c0(%rbp),%ebx
  30d011:	85 db                	test   %ebx,%ebx
  30d013:	0f 84 a3 00 00 00    	je     30d0bc <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e7c>
  30d019:	8b 8d 44 fd ff ff    	mov    -0x2bc(%rbp),%ecx
  30d01f:	48 8b 85 48 fd ff ff 	mov    -0x2b8(%rbp),%rax
  30d026:	e9 9b 00 00 00       	jmp    30d0c6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e86>
  30d02b:	bb 05 00 00 00       	mov    $0x5,%ebx
  30d030:	e8 ab d7 dd ff       	call   ea7e0 <_ZNSt3__116generic_categoryEv$plt>
  30d035:	48 8b 95 90 f9 ff ff 	mov    -0x670(%rbp),%rdx
  30d03c:	89 1a                	mov    %ebx,(%rdx)
  30d03e:	89 4a 04             	mov    %ecx,0x4(%rdx)
  30d041:	48 89 42 08          	mov    %rax,0x8(%rdx)
  30d045:	c6 42 18 00          	movb   $0x0,0x18(%rdx)
  30d049:	e9 88 00 00 00       	jmp    30d0d6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e96>
  30d04e:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
  30d055:	48 3b 85 48 ff ff ff 	cmp    -0xb8(%rbp),%rax
  30d05c:	75 07                	jne    30d065 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e25>
  30d05e:	48 83 7d 88 00       	cmpq   $0x0,-0x78(%rbp)
  30d063:	74 23                	je     30d088 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e48>
  30d065:	4c 8b 75 c0          	mov    -0x40(%rbp),%r14
  30d069:	4c 2b 75 b8          	sub    -0x48(%rbp),%r14
  30d06d:	49 c1 fe 03          	sar    $0x3,%r14
  30d071:	48 b8 ef ee ee ee ee 	movabs $0xeeeeeeeeeeeeeeef,%rax
  30d078:	ee ee ee 
  30d07b:	4c 0f af f0          	imul   %rax,%r14
  30d07f:	49 ff c6             	inc    %r14
  30d082:	0f 85 f3 00 00 00    	jne    30d17b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f3b>
  30d088:	48 8b 95 78 f9 ff ff 	mov    -0x688(%rbp),%rdx
  30d08f:	c5 fa 6f 02          	vmovdqu (%rdx),%xmm0
  30d093:	48 8b 8d 90 f9 ff ff 	mov    -0x670(%rbp),%rcx
  30d09a:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30d09e:	48 8b 42 10          	mov    0x10(%rdx),%rax
  30d0a2:	48 89 41 10          	mov    %rax,0x10(%rcx)
  30d0a6:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30d0aa:	c5 fa 7f 02          	vmovdqu %xmm0,(%rdx)
  30d0ae:	48 c7 42 10 00 00 00 	movq   $0x0,0x10(%rdx)
  30d0b5:	00 
  30d0b6:	c6 41 18 01          	movb   $0x1,0x18(%rcx)
  30d0ba:	eb 3c                	jmp    30d0f8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1eb8>
  30d0bc:	bb 05 00 00 00       	mov    $0x5,%ebx
  30d0c1:	e8 1a d7 dd ff       	call   ea7e0 <_ZNSt3__116generic_categoryEv$plt>
  30d0c6:	41 89 1e             	mov    %ebx,(%r14)
  30d0c9:	41 89 4e 04          	mov    %ecx,0x4(%r14)
  30d0cd:	49 89 46 08          	mov    %rax,0x8(%r14)
  30d0d1:	41 c6 46 18 00       	movb   $0x0,0x18(%r14)
  30d0d6:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30d0dd:	48 85 ff             	test   %rdi,%rdi
  30d0e0:	74 16                	je     30d0f8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1eb8>
  30d0e2:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30d0e9:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30d0f0:	48 29 fe             	sub    %rdi,%rsi
  30d0f3:	e8 c8 d8 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d0f8:	48 8b bd b0 f9 ff ff 	mov    -0x650(%rbp),%rdi
  30d0ff:	48 85 ff             	test   %rdi,%rdi
  30d102:	74 19                	je     30d11d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1edd>
  30d104:	48 89 bd b8 f9 ff ff 	mov    %rdi,-0x648(%rbp)
  30d10b:	48 8b b5 c0 f9 ff ff 	mov    -0x640(%rbp),%rsi
  30d112:	48 29 fe             	sub    %rdi,%rsi
  30d115:	c5 f8 77             	vzeroupper
  30d118:	e8 a3 d8 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d11d:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30d124:	c5 f8 77             	vzeroupper
  30d127:	e8 c4 33 00 00       	call   3104f0 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30d12c:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30d133:	75 1b                	jne    30d150 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f10>
  30d135:	48 8b bd 50 fa ff ff 	mov    -0x5b0(%rbp),%rdi
  30d13c:	48 85 ff             	test   %rdi,%rdi
  30d13f:	74 0f                	je     30d150 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f10>
  30d141:	48 8b b5 60 fa ff ff 	mov    -0x5a0(%rbp),%rsi
  30d148:	48 29 fe             	sub    %rdi,%rsi
  30d14b:	e8 70 d8 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d150:	48 8b bd d0 f9 ff ff 	mov    -0x630(%rbp),%rdi
  30d157:	48 85 ff             	test   %rdi,%rdi
  30d15a:	0f 84 5e e2 ff ff    	je     30b3be <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x17e>
  30d160:	48 89 bd d8 f9 ff ff 	mov    %rdi,-0x628(%rbp)
  30d167:	48 8b b5 e0 f9 ff ff 	mov    -0x620(%rbp),%rsi
  30d16e:	48 29 fe             	sub    %rdi,%rsi
  30d171:	e8 4a d8 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d176:	e9 43 e2 ff ff       	jmp    30b3be <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x17e>
  30d17b:	44 0f b6 a5 28 fa ff 	movzbl -0x5d8(%rbp),%r12d
  30d182:	ff 
  30d183:	4c 8b ad 10 fa ff ff 	mov    -0x5f0(%rbp),%r13
  30d18a:	4c 8d bd 68 fb ff ff 	lea    -0x498(%rbp),%r15
  30d191:	eb 20                	jmp    30d1b3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f73>
  30d193:	48 8b 85 a0 f9 ff ff 	mov    -0x660(%rbp),%rax
  30d19a:	48 8b 38             	mov    (%rax),%rdi
  30d19d:	b8 c8 06 00 00       	mov    $0x6c8,%eax
  30d1a2:	48 01 c7             	add    %rax,%rdi
  30d1a5:	e8 f6 2a ff ff       	call   2ffca0 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::push_front(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline&&)>
  30d1aa:	4d 85 f6             	test   %r14,%r14
  30d1ad:	0f 84 d5 fe ff ff    	je     30d088 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e48>
  30d1b3:	49 ff ce             	dec    %r14
  30d1b6:	48 8b 45 b8          	mov    -0x48(%rbp),%rax
  30d1ba:	49 6b d6 78          	imul   $0x78,%r14,%rdx
  30d1be:	4d 85 f6             	test   %r14,%r14
  30d1c1:	48 8d 74 10 88       	lea    -0x78(%rax,%rdx,1),%rsi
  30d1c6:	48 8d 8d 40 ff ff ff 	lea    -0xc0(%rbp),%rcx
  30d1cd:	48 0f 44 f1          	cmove  %rcx,%rsi
  30d1d1:	45 84 e4             	test   %r12b,%r12b
  30d1d4:	74 0c                	je     30d1e2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1fa2>
  30d1d6:	48 8d 8d 18 fa ff ff 	lea    -0x5e8(%rbp),%rcx
  30d1dd:	4d 39 f5             	cmp    %r14,%r13
  30d1e0:	74 2c                	je     30d20e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1fce>
  30d1e2:	48 8b 8d b0 f9 ff ff 	mov    -0x650(%rbp),%rcx
  30d1e9:	48 8b bd b8 f9 ff ff 	mov    -0x648(%rbp),%rdi
  30d1f0:	48 39 f9             	cmp    %rdi,%rcx
  30d1f3:	74 10                	je     30d205 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1fc5>
  30d1f5:	4c 39 31             	cmp    %r14,(%rcx)
  30d1f8:	74 0b                	je     30d205 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1fc5>
  30d1fa:	48 83 c1 18          	add    $0x18,%rcx
  30d1fe:	48 39 f9             	cmp    %rdi,%rcx
  30d201:	75 f2                	jne    30d1f5 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1fb5>
  30d203:	eb 8e                	jmp    30d193 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f53>
  30d205:	48 39 f9             	cmp    %rdi,%rcx
  30d208:	74 89                	je     30d193 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f53>
  30d20a:	48 83 c1 08          	add    $0x8,%rcx
  30d20e:	48 01 d0             	add    %rdx,%rax
  30d211:	48 8d 50 90          	lea    -0x70(%rax),%rdx
  30d215:	48 8b bd a0 f9 ff ff 	mov    -0x660(%rbp),%rdi
  30d21c:	48 8b 3f             	mov    (%rdi),%rdi
  30d21f:	4c 8b 01             	mov    (%rcx),%r8
  30d222:	4c 89 85 30 fb ff ff 	mov    %r8,-0x4d0(%rbp)
  30d229:	44 0f b6 41 08       	movzbl 0x8(%rcx),%r8d
  30d22e:	44 88 85 38 fb ff ff 	mov    %r8b,-0x4c8(%rbp)
  30d235:	0f b6 49 09          	movzbl 0x9(%rcx),%ecx
  30d239:	4c 8d 40 98          	lea    -0x68(%rax),%r8
  30d23d:	88 8d 39 fb ff ff    	mov    %cl,-0x4c7(%rbp)
  30d243:	48 8b 0e             	mov    (%rsi),%rcx
  30d246:	48 89 8d 40 fb ff ff 	mov    %rcx,-0x4c0(%rbp)
  30d24d:	48 8d 48 a0          	lea    -0x60(%rax),%rcx
  30d251:	4c 8d 48 a8          	lea    -0x58(%rax),%r9
  30d255:	48 83 c0 b0          	add    $0xffffffffffffffb0,%rax
  30d259:	4d 85 f6             	test   %r14,%r14
  30d25c:	4c 8d 95 48 ff ff ff 	lea    -0xb8(%rbp),%r10
  30d263:	49 0f 44 d2          	cmove  %r10,%rdx
  30d267:	48 8b 12             	mov    (%rdx),%rdx
  30d26a:	48 89 95 48 fb ff ff 	mov    %rdx,-0x4b8(%rbp)
  30d271:	48 8d 95 50 ff ff ff 	lea    -0xb0(%rbp),%rdx
  30d278:	4c 0f 44 c2          	cmove  %rdx,%r8
  30d27c:	49 8b 10             	mov    (%r8),%rdx
  30d27f:	48 89 95 50 fb ff ff 	mov    %rdx,-0x4b0(%rbp)
  30d286:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30d28a:	c5 fa 7f 06          	vmovdqu %xmm0,(%rsi)
  30d28e:	48 c7 46 10 00 00 00 	movq   $0x0,0x10(%rsi)
  30d295:	00 
  30d296:	48 8d 95 58 ff ff ff 	lea    -0xa8(%rbp),%rdx
  30d29d:	48 0f 44 ca          	cmove  %rdx,%rcx
  30d2a1:	48 8b 11             	mov    (%rcx),%rdx
  30d2a4:	48 89 95 58 fb ff ff 	mov    %rdx,-0x4a8(%rbp)
  30d2ab:	48 8d 95 60 ff ff ff 	lea    -0xa0(%rbp),%rdx
  30d2b2:	4c 0f 44 ca          	cmove  %rdx,%r9
  30d2b6:	49 8b 11             	mov    (%r9),%rdx
  30d2b9:	48 89 95 60 fb ff ff 	mov    %rdx,-0x4a0(%rbp)
  30d2c0:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30d2c4:	48 8d 8d 68 ff ff ff 	lea    -0x98(%rbp),%rcx
  30d2cb:	48 0f 44 c1          	cmove  %rcx,%rax
  30d2cf:	c5 fe 6f 00          	vmovdqu (%rax),%ymm0
  30d2d3:	c5 fc 10 48 20       	vmovups 0x20(%rax),%ymm1
  30d2d8:	c5 fc 10 50 2b       	vmovups 0x2b(%rax),%ymm2
  30d2dd:	c4 c1 7c 11 57 2b    	vmovups %ymm2,0x2b(%r15)
  30d2e3:	c4 c1 7c 11 4f 20    	vmovups %ymm1,0x20(%r15)
  30d2e9:	c4 c1 7e 7f 07       	vmovdqu %ymm0,(%r15)
  30d2ee:	48 8d b5 30 fb ff ff 	lea    -0x4d0(%rbp),%rsi
  30d2f5:	c5 f8 77             	vzeroupper
  30d2f8:	e8 03 28 ff ff       	call   2ffb00 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::requeue_prioritized_stream_frame(cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline)>
  30d2fd:	48 8b 9d 60 fb ff ff 	mov    -0x4a0(%rbp),%rbx
  30d304:	48 85 db             	test   %rbx,%rbx
  30d307:	74 23                	je     30d32c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x20ec>
  30d309:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30d310:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30d316:	48 85 c0             	test   %rax,%rax
  30d319:	75 11                	jne    30d32c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x20ec>
  30d31b:	48 8b 03             	mov    (%rbx),%rax
  30d31e:	48 89 df             	mov    %rbx,%rdi
  30d321:	ff 50 10             	call   *0x10(%rax)
  30d324:	48 89 df             	mov    %rbx,%rdi
  30d327:	e8 f4 d4 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30d32c:	48 8b bd 40 fb ff ff 	mov    -0x4c0(%rbp),%rdi
  30d333:	48 85 ff             	test   %rdi,%rdi
  30d336:	0f 84 6e fe ff ff    	je     30d1aa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f6a>
  30d33c:	48 89 bd 48 fb ff ff 	mov    %rdi,-0x4b8(%rbp)
  30d343:	48 8b b5 50 fb ff ff 	mov    -0x4b0(%rbp),%rsi
  30d34a:	48 29 fe             	sub    %rdi,%rsi
  30d34d:	e8 6e d6 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d352:	e9 53 fe ff ff       	jmp    30d1aa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f6a>
  30d357:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d35e:	c5 f8 77             	vzeroupper
  30d361:	e8 9a 9a fa ff       	call   2b6e00 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d366:	48 8b 9d 90 fa ff ff 	mov    -0x570(%rbp),%rbx
  30d36d:	4c 8b b5 68 f9 ff ff 	mov    -0x698(%rbp),%r14
  30d374:	e9 0a f5 ff ff       	jmp    30c883 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1643>
  30d379:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d380:	e8 7b 9a fa ff       	call   2b6e00 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d385:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d38c:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d393:	e9 63 f5 ff ff       	jmp    30c8fb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16bb>
  30d398:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d39f:	e8 5c 9a fa ff       	call   2b6e00 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d3a4:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d3ab:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d3b2:	e9 8d f5 ff ff       	jmp    30c944 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1704>
  30d3b7:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d3be:	e8 3d 9a fa ff       	call   2b6e00 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d3c3:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d3ca:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d3d1:	e9 b7 f5 ff ff       	jmp    30c98d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x174d>
  30d3d6:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d3dd:	e8 1e 9a fa ff       	call   2b6e00 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d3e2:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d3e9:	48 8b 9d 48 f9 ff ff 	mov    -0x6b8(%rbp),%rbx
  30d3f0:	48 8b 85 88 f9 ff ff 	mov    -0x678(%rbp),%rax
  30d3f7:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d3fe:	e9 dc f5 ff ff       	jmp    30c9df <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x179f>
  30d403:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30d40a:	00 00 
  30d40c:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30d410:	0f 85 7c 02 00 00    	jne    30d692 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2452>
  30d416:	e8 d5 99 fa ff       	call   2b6df0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__throw_length_error[abi:nqe220108]()>
  30d41b:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30d422:	00 00 
  30d424:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30d428:	0f 85 64 02 00 00    	jne    30d692 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2452>
  30d42e:	c5 f8 77             	vzeroupper
  30d431:	e8 ba 99 fa ff       	call   2b6df0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__throw_length_error[abi:nqe220108]()>
  30d436:	e9 0e 01 00 00       	jmp    30d549 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2309>
  30d43b:	e9 09 01 00 00       	jmp    30d549 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2309>
  30d440:	49 89 c6             	mov    %rax,%r14
  30d443:	48 8d bd a0 fa ff ff 	lea    -0x560(%rbp),%rdi
  30d44a:	e8 a1 30 00 00       	call   3104f0 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30d44f:	e9 f8 00 00 00       	jmp    30d54c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x230c>
  30d454:	49 89 c6             	mov    %rax,%r14
  30d457:	48 8d bd 50 fe ff ff 	lea    -0x1b0(%rbp),%rdi
  30d45e:	e8 2d 26 ff ff       	call   2ffa90 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30d463:	eb 03                	jmp    30d468 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2228>
  30d465:	49 89 c6             	mov    %rax,%r14
  30d468:	48 8b bd 40 fd ff ff 	mov    -0x2c0(%rbp),%rdi
  30d46f:	48 85 ff             	test   %rdi,%rdi
  30d472:	74 1b                	je     30d48f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x224f>
  30d474:	48 89 bd 48 fd ff ff 	mov    %rdi,-0x2b8(%rbp)
  30d47b:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30d482:	48 29 fe             	sub    %rdi,%rsi
  30d485:	e8 36 d5 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d48a:	eb 03                	jmp    30d48f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x224f>
  30d48c:	49 89 c6             	mov    %rax,%r14
  30d48f:	4d 85 ed             	test   %r13,%r13
  30d492:	0f 84 6f 01 00 00    	je     30d607 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d498:	4d 29 ef             	sub    %r13,%r15
  30d49b:	4c 89 ef             	mov    %r13,%rdi
  30d49e:	4c 89 fe             	mov    %r15,%rsi
  30d4a1:	e8 1a d5 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d4a6:	e9 5c 01 00 00       	jmp    30d607 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d4ab:	e9 30 01 00 00       	jmp    30d5e0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23a0>
  30d4b0:	e9 2b 01 00 00       	jmp    30d5e0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23a0>
  30d4b5:	e9 26 01 00 00       	jmp    30d5e0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23a0>
  30d4ba:	49 89 c6             	mov    %rax,%r14
  30d4bd:	e9 97 01 00 00       	jmp    30d659 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2419>
  30d4c2:	e9 82 00 00 00       	jmp    30d549 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2309>
  30d4c7:	49 89 c6             	mov    %rax,%r14
  30d4ca:	48 8b bd 68 ff ff ff 	mov    -0x98(%rbp),%rdi
  30d4d1:	48 85 ff             	test   %rdi,%rdi
  30d4d4:	0f 84 64 01 00 00    	je     30d63e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23fe>
  30d4da:	48 8b b5 78 ff ff ff 	mov    -0x88(%rbp),%rsi
  30d4e1:	48 29 fe             	sub    %rdi,%rsi
  30d4e4:	e8 d7 d4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d4e9:	e9 50 01 00 00       	jmp    30d63e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23fe>
  30d4ee:	49 89 c6             	mov    %rax,%r14
  30d4f1:	e9 48 01 00 00       	jmp    30d63e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23fe>
  30d4f6:	49 89 c6             	mov    %rax,%r14
  30d4f9:	e9 40 01 00 00       	jmp    30d63e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23fe>
  30d4fe:	e9 dd 00 00 00       	jmp    30d5e0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23a0>
  30d503:	49 89 c6             	mov    %rax,%r14
  30d506:	48 8d bd c8 fe ff ff 	lea    -0x138(%rbp),%rdi
  30d50d:	e9 f0 00 00 00       	jmp    30d602 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c2>
  30d512:	49 89 c6             	mov    %rax,%r14
  30d515:	48 85 db             	test   %rbx,%rbx
  30d518:	0f 84 e9 00 00 00    	je     30d607 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d51e:	49 29 dd             	sub    %rbx,%r13
  30d521:	48 89 df             	mov    %rbx,%rdi
  30d524:	4c 89 ee             	mov    %r13,%rsi
  30d527:	e8 94 d4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d52c:	e9 d6 00 00 00       	jmp    30d607 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d531:	49 89 c6             	mov    %rax,%r14
  30d534:	48 8d bd 30 fb ff ff 	lea    -0x4d0(%rbp),%rdi
  30d53b:	e8 f0 26 ff ff       	call   2ffc30 <cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline::~scheduled_quic_frame()>
  30d540:	e9 c2 00 00 00       	jmp    30d607 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d545:	eb 02                	jmp    30d549 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2309>
  30d547:	eb 00                	jmp    30d549 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2309>
  30d549:	49 89 c6             	mov    %rax,%r14
  30d54c:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30d553:	48 85 ff             	test   %rdi,%rdi
  30d556:	0f 84 ab 00 00 00    	je     30d607 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d55c:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30d563:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30d56a:	48 29 fe             	sub    %rdi,%rsi
  30d56d:	e8 4e d4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d572:	e9 90 00 00 00       	jmp    30d607 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d577:	49 89 c6             	mov    %rax,%r14
  30d57a:	be 01 00 00 00       	mov    $0x1,%esi
  30d57f:	4c 89 ef             	mov    %r13,%rdi
  30d582:	e8 39 d4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d587:	eb 7e                	jmp    30d607 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d589:	49 89 c6             	mov    %rax,%r14
  30d58c:	4d 85 ed             	test   %r13,%r13
  30d58f:	0f 84 a9 00 00 00    	je     30d63e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23fe>
  30d595:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30d59c:	4c 29 ee             	sub    %r13,%rsi
  30d59f:	4c 89 ef             	mov    %r13,%rdi
  30d5a2:	e8 19 d4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d5a7:	e9 92 00 00 00       	jmp    30d63e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23fe>
  30d5ac:	eb 32                	jmp    30d5e0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23a0>
  30d5ae:	49 89 c6             	mov    %rax,%r14
  30d5b1:	48 8d bd b8 fb ff ff 	lea    -0x448(%rbp),%rdi
  30d5b8:	e8 73 26 ff ff       	call   2ffc30 <cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline::~scheduled_quic_frame()>
  30d5bd:	eb 48                	jmp    30d607 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d5bf:	49 89 c6             	mov    %rax,%r14
  30d5c2:	e9 92 00 00 00       	jmp    30d659 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2419>
  30d5c7:	49 89 c6             	mov    %rax,%r14
  30d5ca:	e9 8a 00 00 00       	jmp    30d659 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2419>
  30d5cf:	49 89 c6             	mov    %rax,%r14
  30d5d2:	48 8d bd b8 fc ff ff 	lea    -0x348(%rbp),%rdi
  30d5d9:	e8 52 26 ff ff       	call   2ffc30 <cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline::~scheduled_quic_frame()>
  30d5de:	eb 1b                	jmp    30d5fb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23bb>
  30d5e0:	49 89 c6             	mov    %rax,%r14
  30d5e3:	eb 22                	jmp    30d607 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d5e5:	eb 11                	jmp    30d5f8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23b8>
  30d5e7:	49 89 c6             	mov    %rax,%r14
  30d5ea:	48 8d bd 40 fc ff ff 	lea    -0x3c0(%rbp),%rdi
  30d5f1:	e8 9a 24 ff ff       	call   2ffa90 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30d5f6:	eb 03                	jmp    30d5fb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23bb>
  30d5f8:	49 89 c6             	mov    %rax,%r14
  30d5fb:	48 8d bd d0 fd ff ff 	lea    -0x230(%rbp),%rdi
  30d602:	e8 89 24 ff ff       	call   2ffa90 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30d607:	48 8b bd b0 f9 ff ff 	mov    -0x650(%rbp),%rdi
  30d60e:	48 85 ff             	test   %rdi,%rdi
  30d611:	74 16                	je     30d629 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23e9>
  30d613:	48 89 bd b8 f9 ff ff 	mov    %rdi,-0x648(%rbp)
  30d61a:	48 8b b5 c0 f9 ff ff 	mov    -0x640(%rbp),%rsi
  30d621:	48 29 fe             	sub    %rdi,%rsi
  30d624:	e8 97 d3 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d629:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30d630:	e8 bb 2e 00 00       	call   3104f0 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30d635:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30d63c:	75 1b                	jne    30d659 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2419>
  30d63e:	48 8b bd 50 fa ff ff 	mov    -0x5b0(%rbp),%rdi
  30d645:	48 85 ff             	test   %rdi,%rdi
  30d648:	74 0f                	je     30d659 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2419>
  30d64a:	48 8b b5 60 fa ff ff 	mov    -0x5a0(%rbp),%rsi
  30d651:	48 29 fe             	sub    %rdi,%rsi
  30d654:	e8 67 d3 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d659:	48 8b bd d0 f9 ff ff 	mov    -0x630(%rbp),%rdi
  30d660:	48 85 ff             	test   %rdi,%rdi
  30d663:	74 16                	je     30d67b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x243b>
  30d665:	48 89 bd d8 f9 ff ff 	mov    %rdi,-0x628(%rbp)
  30d66c:	48 8b b5 e0 f9 ff ff 	mov    -0x620(%rbp),%rsi
  30d673:	48 29 fe             	sub    %rdi,%rsi
  30d676:	e8 45 d3 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d67b:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30d682:	00 00 
  30d684:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30d688:	75 08                	jne    30d692 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2452>
  30d68a:	4c 89 f7             	mov    %r14,%rdi
  30d68d:	e8 fe d3 dd ff       	call   eaa90 <_Unwind_Resume$plt>
  30d692:	c5 f8 77             	vzeroupper
  30d695:	e8 96 d4 dd ff       	call   eab30 <__stack_chk_fail$plt>
  30d69a:	cc                   	int3
  30d69b:	cc                   	int3
  30d69c:	cc                   	int3
  30d69d:	cc                   	int3
  30d69e:	cc                   	int3
  30d69f:	cc                   	int3
