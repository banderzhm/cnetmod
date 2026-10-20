
/var/tmp/cnetmod-h3-owned-fast-candidate-fbaeb04a21614732b24a7fdae850fcd0/proof-build-root/testing/bench/arena_h3_server:     file format elf64-x86-64


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
  30b25b:	89 95 6c f9 ff ff    	mov    %edx,-0x694(%rbp)
  30b261:	49 89 fc             	mov    %rdi,%r12
  30b264:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30b26b:	00 00 
  30b26d:	48 89 45 d0          	mov    %rax,-0x30(%rbp)
  30b271:	48 8b 0e             	mov    (%rsi),%rcx
  30b274:	48 8b 81 58 03 00 00 	mov    0x358(%rcx),%rax
  30b27b:	48 85 c0             	test   %rax,%rax
  30b27e:	0f 84 16 01 00 00    	je     30b39a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x15a>
  30b284:	4c 8d 90 00 04 00 00 	lea    0x400(%rax),%r10
  30b28b:	48 8b 90 08 04 00 00 	mov    0x408(%rax),%rdx
  30b292:	31 db                	xor    %ebx,%ebx
  30b294:	48 3b 90 10 04 00 00 	cmp    0x410(%rax),%rdx
  30b29b:	49 0f 45 da          	cmovne %r10,%rbx
  30b29f:	0f 84 f5 00 00 00    	je     30b39a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x15a>
  30b2a5:	48 8b 91 80 0e 00 00 	mov    0xe80(%rcx),%rdx
  30b2ac:	48 85 d2             	test   %rdx,%rdx
  30b2af:	74 27                	je     30b2d8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x98>
  30b2b1:	8b b9 a8 0e 00 00    	mov    0xea8(%rcx),%edi
  30b2b7:	eb 0f                	jmp    30b2c8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x88>
  30b2b9:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
  30b2c0:	48 8b 12             	mov    (%rdx),%rdx
  30b2c3:	48 85 d2             	test   %rdx,%rdx
  30b2c6:	74 10                	je     30b2d8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x98>
  30b2c8:	3b 7a 20             	cmp    0x20(%rdx),%edi
  30b2cb:	72 f3                	jb     30b2c0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x80>
  30b2cd:	74 28                	je     30b2f7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xb7>
  30b2cf:	48 8b 52 08          	mov    0x8(%rdx),%rdx
  30b2d3:	48 85 d2             	test   %rdx,%rdx
  30b2d6:	75 f0                	jne    30b2c8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x88>
  30b2d8:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30b2df:	00 00 
  30b2e1:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30b2e5:	0f 85 f3 22 00 00    	jne    30d5de <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x239e>
  30b2eb:	48 8d 3d 9f 1d dd ff 	lea    -0x22e261(%rip),%rdi        # dd091 <cnetmod::quic::max_udp_receive_payload@cnetmod.protocol.quic+0x3181>
  30b2f2:	e8 49 ae fa ff       	call   2b6140 <std::__1::__throw_out_of_range[abi:nqe220108](char const*)>
  30b2f7:	4c 8b 4a 28          	mov    0x28(%rdx),%r9
  30b2fb:	49 8d 51 32          	lea    0x32(%r9),%rdx
  30b2ff:	41 0f b6 79 47       	movzbl 0x47(%r9),%edi
  30b304:	49 89 d3             	mov    %rdx,%r11
  30b307:	40 84 ff             	test   %dil,%dil
  30b30a:	74 6e                	je     30b37a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x13a>
  30b30c:	41 80 b9 83 00 00 00 	cmpb   $0x0,0x83(%r9)
  30b313:	00 
  30b314:	0f 85 80 00 00 00    	jne    30b39a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x15a>
  30b31a:	41 80 b9 84 00 00 00 	cmpb   $0x1,0x84(%r9)
  30b321:	01 
  30b322:	74 76                	je     30b39a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x15a>
  30b324:	48 89 9d 50 f9 ff ff 	mov    %rbx,-0x6b0(%rbp)
  30b32b:	4c 89 9d 70 f9 ff ff 	mov    %r11,-0x690(%rbp)
  30b332:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b336:	c5 f9 7f 85 d0 f9 ff 	vmovdqa %xmm0,-0x630(%rbp)
  30b33d:	ff 
  30b33e:	48 c7 85 e0 f9 ff ff 	movq   $0x0,-0x620(%rbp)
  30b345:	00 00 00 00 
  30b349:	4d 85 c0             	test   %r8,%r8
  30b34c:	4c 8d b5 d0 f9 ff ff 	lea    -0x630(%rbp),%r14
  30b353:	4d 89 c3             	mov    %r8,%r11
  30b356:	4d 0f 44 de          	cmove  %r14,%r11
  30b35a:	4d 8b 2b             	mov    (%r11),%r13
  30b35d:	4c 89 9d 90 f9 ff ff 	mov    %r11,-0x670(%rbp)
  30b364:	4d 89 6b 08          	mov    %r13,0x8(%r11)
  30b368:	40 84 ff             	test   %dil,%dil
  30b36b:	74 6b                	je     30b3d8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x198>
  30b36d:	0f b6 52 14          	movzbl 0x14(%rdx),%edx
  30b371:	48 83 c2 05          	add    $0x5,%rdx
  30b375:	e9 81 00 00 00       	jmp    30b3fb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1bb>
  30b37a:	41 83 39 00          	cmpl   $0x0,(%r9)
  30b37e:	75 1a                	jne    30b39a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x15a>
  30b380:	80 b9 8f 01 00 00 01 	cmpb   $0x1,0x18f(%rcx)
  30b387:	75 11                	jne    30b39a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x15a>
  30b389:	4c 8d 99 7a 01 00 00 	lea    0x17a(%rcx),%r11
  30b390:	41 80 b9 83 00 00 00 	cmpb   $0x0,0x83(%r9)
  30b397:	00 
  30b398:	74 80                	je     30b31a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xda>
  30b39a:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b39e:	c4 c1 7a 7f 04 24    	vmovdqu %xmm0,(%r12)
  30b3a4:	49 c7 44 24 10 00 00 	movq   $0x0,0x10(%r12)
  30b3ab:	00 00 
  30b3ad:	41 c6 44 24 18 01    	movb   $0x1,0x18(%r12)
  30b3b3:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30b3ba:	00 00 
  30b3bc:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30b3c0:	0f 85 18 22 00 00    	jne    30d5de <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x239e>
  30b3c6:	48 81 c4 b8 06 00 00 	add    $0x6b8,%rsp
  30b3cd:	5b                   	pop    %rbx
  30b3ce:	41 5c                	pop    %r12
  30b3d0:	41 5d                	pop    %r13
  30b3d2:	41 5e                	pop    %r14
  30b3d4:	41 5f                	pop    %r15
  30b3d6:	5d                   	pop    %rbp
  30b3d7:	c3                   	ret
  30b3d8:	ba 19 00 00 00       	mov    $0x19,%edx
  30b3dd:	41 83 39 00          	cmpl   $0x0,(%r9)
  30b3e1:	75 18                	jne    30b3fb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1bb>
  30b3e3:	80 b9 8f 01 00 00 01 	cmpb   $0x1,0x18f(%rcx)
  30b3ea:	75 0f                	jne    30b3fb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1bb>
  30b3ec:	48 81 c1 7a 01 00 00 	add    $0x17a,%rcx
  30b3f3:	48 89 ca             	mov    %rcx,%rdx
  30b3f6:	e9 72 ff ff ff       	jmp    30b36d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x12d>
  30b3fb:	4c 89 95 48 f9 ff ff 	mov    %r10,-0x6b8(%rbp)
  30b402:	48 89 b5 98 f9 ff ff 	mov    %rsi,-0x668(%rbp)
  30b409:	48 03 90 78 04 00 00 	add    0x478(%rax),%rdx
  30b410:	4c 89 8d 80 f9 ff ff 	mov    %r9,-0x680(%rbp)
  30b417:	49 8b 81 58 1c 00 00 	mov    0x1c58(%r9),%rax
  30b41e:	45 31 ff             	xor    %r15d,%r15d
  30b421:	48 29 d0             	sub    %rdx,%rax
  30b424:	4c 0f 43 f8          	cmovae %rax,%r15
  30b428:	4d 85 c0             	test   %r8,%r8
  30b42b:	4d 0f 45 f0          	cmovne %r8,%r14
  30b42f:	49 8b 5e 10          	mov    0x10(%r14),%rbx
  30b433:	4c 29 eb             	sub    %r13,%rbx
  30b436:	49 39 df             	cmp    %rbx,%r15
  30b439:	4c 89 bd a0 f9 ff ff 	mov    %r15,-0x660(%rbp)
  30b440:	76 44                	jbe    30b486 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x246>
  30b442:	4d 85 ff             	test   %r15,%r15
  30b445:	0f 88 04 1f 00 00    	js     30d34f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x210f>
  30b44b:	48 8b bd a0 f9 ff ff 	mov    -0x660(%rbp),%rdi
  30b452:	e8 d9 f5 dd ff       	call   eaa30 <_Znwm$plt>
  30b457:	48 89 c1             	mov    %rax,%rcx
  30b45a:	48 8b 95 90 f9 ff ff 	mov    -0x670(%rbp),%rdx
  30b461:	48 89 02             	mov    %rax,(%rdx)
  30b464:	4c 8b bd a0 f9 ff ff 	mov    -0x660(%rbp),%r15
  30b46b:	4c 01 f9             	add    %r15,%rcx
  30b46e:	48 89 42 08          	mov    %rax,0x8(%rdx)
  30b472:	49 89 4e 10          	mov    %rcx,0x10(%r14)
  30b476:	4d 85 ed             	test   %r13,%r13
  30b479:	74 0b                	je     30b486 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x246>
  30b47b:	4c 89 ef             	mov    %r13,%rdi
  30b47e:	48 89 de             	mov    %rbx,%rsi
  30b481:	e8 3a f5 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b486:	48 8b 85 78 f9 ff ff 	mov    -0x688(%rbp),%rax
  30b48d:	48 8b 18             	mov    (%rax),%rbx
  30b490:	4c 8b 68 10          	mov    0x10(%rax),%r13
  30b494:	48 89 58 08          	mov    %rbx,0x8(%rax)
  30b498:	48 8b 85 70 f9 ff ff 	mov    -0x690(%rbp),%rax
  30b49f:	0f b6 40 14          	movzbl 0x14(%rax),%eax
  30b4a3:	48 8b 8d 50 f9 ff ff 	mov    -0x6b0(%rbp),%rcx
  30b4aa:	48 8b 49 78          	mov    0x78(%rcx),%rcx
  30b4ae:	4c 01 f8             	add    %r15,%rax
  30b4b1:	4c 8d 3c 01          	lea    (%rcx,%rax,1),%r15
  30b4b5:	49 83 c7 05          	add    $0x5,%r15
  30b4b9:	49 29 dd             	sub    %rbx,%r13
  30b4bc:	4d 39 ef             	cmp    %r13,%r15
  30b4bf:	76 36                	jbe    30b4f7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2b7>
  30b4c1:	4d 85 ff             	test   %r15,%r15
  30b4c4:	0f 88 85 1e 00 00    	js     30d34f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x210f>
  30b4ca:	4c 89 ff             	mov    %r15,%rdi
  30b4cd:	e8 5e f5 dd ff       	call   eaa30 <_Znwm$plt>
  30b4d2:	48 8b 8d 78 f9 ff ff 	mov    -0x688(%rbp),%rcx
  30b4d9:	48 89 01             	mov    %rax,(%rcx)
  30b4dc:	49 01 c7             	add    %rax,%r15
  30b4df:	48 89 41 08          	mov    %rax,0x8(%rcx)
  30b4e3:	4c 89 79 10          	mov    %r15,0x10(%rcx)
  30b4e7:	48 85 db             	test   %rbx,%rbx
  30b4ea:	74 0b                	je     30b4f7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2b7>
  30b4ec:	48 89 df             	mov    %rbx,%rdi
  30b4ef:	4c 89 ee             	mov    %r13,%rsi
  30b4f2:	e8 c9 f4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b4f7:	4c 8b b5 98 f9 ff ff 	mov    -0x668(%rbp),%r14
  30b4fe:	49 8b 06             	mov    (%r14),%rax
  30b501:	80 b8 82 06 00 00 01 	cmpb   $0x1,0x682(%rax)
  30b508:	4c 8b bd 80 f9 ff ff 	mov    -0x680(%rbp),%r15
  30b50f:	4c 89 fb             	mov    %r15,%rbx
  30b512:	75 6d                	jne    30b581 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x341>
  30b514:	48 8b 88 78 0e 00 00 	mov    0xe78(%rax),%rcx
  30b51b:	48 05 80 0e 00 00    	add    $0xe80,%rax
  30b521:	4c 89 fb             	mov    %r15,%rbx
  30b524:	48 39 c1             	cmp    %rax,%rcx
  30b527:	75 0c                	jne    30b535 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2f5>
  30b529:	eb 56                	jmp    30b581 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x341>
  30b52b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
  30b530:	48 39 c1             	cmp    %rax,%rcx
  30b533:	74 49                	je     30b57e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x33e>
  30b535:	48 8b 51 28          	mov    0x28(%rcx),%rdx
  30b539:	48 83 ba d0 00 00 00 	cmpq   $0x0,0xd0(%rdx)
  30b540:	00 
  30b541:	75 2b                	jne    30b56e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x32e>
  30b543:	48 8b 51 08          	mov    0x8(%rcx),%rdx
  30b547:	48 85 d2             	test   %rdx,%rdx
  30b54a:	74 14                	je     30b560 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x320>
  30b54c:	0f 1f 40 00          	nopl   0x0(%rax)
  30b550:	48 89 d1             	mov    %rdx,%rcx
  30b553:	48 8b 12             	mov    (%rdx),%rdx
  30b556:	48 85 d2             	test   %rdx,%rdx
  30b559:	75 f5                	jne    30b550 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x310>
  30b55b:	eb d3                	jmp    30b530 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2f0>
  30b55d:	0f 1f 00             	nopl   (%rax)
  30b560:	48 89 ca             	mov    %rcx,%rdx
  30b563:	48 8b 49 10          	mov    0x10(%rcx),%rcx
  30b567:	48 3b 11             	cmp    (%rcx),%rdx
  30b56a:	75 f4                	jne    30b560 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x320>
  30b56c:	eb c2                	jmp    30b530 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2f0>
  30b56e:	8b 71 20             	mov    0x20(%rcx),%esi
  30b571:	48 8b 38             	mov    (%rax),%rdi
  30b574:	e8 47 74 ff ff       	call   3029c0 <std::__1::map<unsigned int, std::__1::unique_ptr<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state, std::__1::default_delete<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state> >, std::__1::less<unsigned int>, std::__1::allocator<std::__1::pair<unsigned int const, std::__1::unique_ptr<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state, std::__1::default_delete<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state> > > > >::at(unsigned int const&)>
  30b579:	48 8b 18             	mov    (%rax),%rbx
  30b57c:	eb 03                	jmp    30b581 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x341>
  30b57e:	4c 89 fb             	mov    %r15,%rbx
  30b581:	48 8d b3 c0 00 00 00 	lea    0xc0(%rbx),%rsi
  30b588:	48 8d bd 30 fa ff ff 	lea    -0x5d0(%rbp),%rdi
  30b58f:	48 89 b5 40 f9 ff ff 	mov    %rsi,-0x6c0(%rbp)
  30b596:	e8 65 59 00 00       	call   310f00 <cnetmod::quic::(anonymous namespace)::take_ack_frame(std::__1::set<unsigned long, std::__1::less<unsigned long>, std::__1::allocator<unsigned long> >&)>
  30b59b:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30b5a2:	0f 85 b6 00 00 00    	jne    30b65e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x41e>
  30b5a8:	49 8b 06             	mov    (%r14),%rax
  30b5ab:	80 b8 82 06 00 00 01 	cmpb   $0x1,0x682(%rax)
  30b5b2:	75 52                	jne    30b606 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x3c6>
  30b5b4:	8b 03                	mov    (%rbx),%eax
  30b5b6:	89 85 40 ff ff ff    	mov    %eax,-0xc0(%rbp)
  30b5bc:	48 8d bd 48 ff ff ff 	lea    -0xb8(%rbp),%rdi
  30b5c3:	48 8d b5 30 fa ff ff 	lea    -0x5d0(%rbp),%rsi
  30b5ca:	e8 b1 5e 00 00       	call   311480 <cnetmod::quic::ack_frame@cnetmod.protocol.quic::ack_frame(cnetmod::quic::ack_frame@cnetmod.protocol.quic const&)>
  30b5cf:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30b5d6:	48 8d b5 40 ff ff ff 	lea    -0xc0(%rbp),%rsi
  30b5dd:	e8 0e bc 03 00       	call   3471f0 <cnetmod::quic::encode_frame@cnetmod.protocol.quic(cnetmod::quic::path_ack_frame@cnetmod.protocol.quic const&)>
  30b5e2:	48 8b bd 68 ff ff ff 	mov    -0x98(%rbp),%rdi
  30b5e9:	48 85 ff             	test   %rdi,%rdi
  30b5ec:	74 2b                	je     30b619 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x3d9>
  30b5ee:	48 89 bd 70 ff ff ff 	mov    %rdi,-0x90(%rbp)
  30b5f5:	48 8b b5 78 ff ff ff 	mov    -0x88(%rbp),%rsi
  30b5fc:	48 29 fe             	sub    %rdi,%rsi
  30b5ff:	e8 bc f3 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b604:	eb 13                	jmp    30b619 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x3d9>
  30b606:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30b60d:	48 8d b5 30 fa ff ff 	lea    -0x5d0(%rbp),%rsi
  30b614:	e8 87 af 03 00       	call   3465a0 <cnetmod::quic::encode_frame@cnetmod.protocol.quic(cnetmod::quic::ack_frame@cnetmod.protocol.quic const&)>
  30b619:	48 8b bd 90 f9 ff ff 	mov    -0x670(%rbp),%rdi
  30b620:	48 8b 77 08          	mov    0x8(%rdi),%rsi
  30b624:	4c 8b ad 40 fd ff ff 	mov    -0x2c0(%rbp),%r13
  30b62b:	48 8b 8d 48 fd ff ff 	mov    -0x2b8(%rbp),%rcx
  30b632:	49 89 c8             	mov    %rcx,%r8
  30b635:	4d 29 e8             	sub    %r13,%r8
  30b638:	4c 89 ea             	mov    %r13,%rdx
  30b63b:	e8 20 f5 fa ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30b640:	4d 85 ed             	test   %r13,%r13
  30b643:	74 19                	je     30b65e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x41e>
  30b645:	4c 89 ad 48 fd ff ff 	mov    %r13,-0x2b8(%rbp)
  30b64c:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30b653:	4c 29 ee             	sub    %r13,%rsi
  30b656:	4c 89 ef             	mov    %r13,%rdi
  30b659:	e8 62 f3 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b65e:	c6 45 a8 00          	movb   $0x0,-0x58(%rbp)
  30b662:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b666:	c5 fe 7f 85 40 ff ff 	vmovdqu %ymm0,-0xc0(%rbp)
  30b66d:	ff 
  30b66e:	c5 fe 7f 85 60 ff ff 	vmovdqu %ymm0,-0xa0(%rbp)
  30b675:	ff 
  30b676:	c5 fe 7f 45 80       	vmovdqu %ymm0,-0x80(%rbp)
  30b67b:	c6 45 a0 00          	movb   $0x0,-0x60(%rbp)
  30b67f:	66 c7 45 b0 03 00    	movw   $0x3,-0x50(%rbp)
  30b685:	c6 45 b2 00          	movb   $0x0,-0x4e(%rbp)
  30b689:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b68d:	c5 fa 7f 45 b8       	vmovdqu %xmm0,-0x48(%rbp)
  30b692:	48 c7 45 c8 00 00 00 	movq   $0x0,-0x38(%rbp)
  30b699:	00 
  30b69a:	c6 85 10 fa ff ff 00 	movb   $0x0,-0x5f0(%rbp)
  30b6a1:	c6 85 28 fa ff ff 00 	movb   $0x0,-0x5d8(%rbp)
  30b6a8:	c5 f9 7f 85 b0 f9 ff 	vmovdqa %xmm0,-0x650(%rbp)
  30b6af:	ff 
  30b6b0:	48 c7 85 c0 f9 ff ff 	movq   $0x0,-0x640(%rbp)
  30b6b7:	00 00 00 00 
  30b6bb:	49 8b 87 60 1c 00 00 	mov    0x1c60(%r15),%rax
  30b6c2:	48 89 85 60 f9 ff ff 	mov    %rax,-0x6a0(%rbp)
  30b6c9:	41 0f b6 8f 68 1c 00 	movzbl 0x1c68(%r15),%ecx
  30b6d0:	00 
  30b6d1:	41 8b 87 6c 1c 00 00 	mov    0x1c6c(%r15),%eax
  30b6d8:	89 85 0b fa ff ff    	mov    %eax,-0x5f5(%rbp)
  30b6de:	41 8b 87 69 1c 00 00 	mov    0x1c69(%r15),%eax
  30b6e5:	89 85 08 fa ff ff    	mov    %eax,-0x5f8(%rbp)
  30b6eb:	88 8d af f9 ff ff    	mov    %cl,-0x651(%rbp)
  30b6f1:	80 f9 01             	cmp    $0x1,%cl
  30b6f4:	48 8b 9d a0 f9 ff ff 	mov    -0x660(%rbp),%rbx
  30b6fb:	75 3f                	jne    30b73c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x4fc>
  30b6fd:	bf 01 00 00 00       	mov    $0x1,%edi
  30b702:	c5 f8 77             	vzeroupper
  30b705:	e8 26 f3 dd ff       	call   eaa30 <_Znwm$plt>
  30b70a:	49 89 c5             	mov    %rax,%r13
  30b70d:	48 89 c1             	mov    %rax,%rcx
  30b710:	48 ff c1             	inc    %rcx
  30b713:	c6 00 01             	movb   $0x1,(%rax)
  30b716:	48 8b bd 90 f9 ff ff 	mov    -0x670(%rbp),%rdi
  30b71d:	48 8b 77 08          	mov    0x8(%rdi),%rsi
  30b721:	41 b8 01 00 00 00    	mov    $0x1,%r8d
  30b727:	48 89 c2             	mov    %rax,%rdx
  30b72a:	e8 31 f4 fa ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30b72f:	be 01 00 00 00       	mov    $0x1,%esi
  30b734:	4c 89 ef             	mov    %r13,%rdi
  30b737:	e8 84 f2 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b73c:	4c 89 a5 88 f9 ff ff 	mov    %r12,-0x678(%rbp)
  30b743:	49 8b 06             	mov    (%r14),%rax
  30b746:	83 78 60 01          	cmpl   $0x1,0x60(%rax)
  30b74a:	0f 85 6d 02 00 00    	jne    30b9bd <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x77d>
  30b750:	48 83 b8 50 2c 00 00 	cmpq   $0x0,0x2c50(%rax)
  30b757:	00 
  30b758:	0f 84 f4 00 00 00    	je     30b852 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x612>
  30b75e:	48 8d b8 28 2c 00 00 	lea    0x2c28(%rax),%rdi
  30b765:	48 8b 88 30 2c 00 00 	mov    0x2c30(%rax),%rcx
  30b76c:	48 8b 90 48 2c 00 00 	mov    0x2c48(%rax),%rdx
  30b773:	48 b8 c1 c0 c0 c0 c0 	movabs $0xc0c0c0c0c0c0c0c1,%rax
  30b77a:	c0 c0 c0 
  30b77d:	c4 e2 fb f6 c0       	mulx   %rax,%rax,%rax
  30b782:	48 c1 e8 07          	shr    $0x7,%rax
  30b786:	48 8b 0c c1          	mov    (%rcx,%rax,8),%rcx
  30b78a:	48 69 c0 aa 00 00 00 	imul   $0xaa,%rax,%rax
  30b791:	48 29 c2             	sub    %rax,%rdx
  30b794:	48 8d 04 52          	lea    (%rdx,%rdx,2),%rax
  30b798:	48 8b 1c c1          	mov    (%rcx,%rax,8),%rbx
  30b79c:	4c 8b 7c c1 08       	mov    0x8(%rcx,%rax,8),%r15
  30b7a1:	4c 8b 6c c1 10       	mov    0x10(%rcx,%rax,8),%r13
  30b7a6:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b7aa:	c5 fa 7f 04 c1       	vmovdqu %xmm0,(%rcx,%rax,8)
  30b7af:	48 c7 44 c1 10 00 00 	movq   $0x0,0x10(%rcx,%rax,8)
  30b7b6:	00 00 
  30b7b8:	c5 f8 77             	vzeroupper
  30b7bb:	e8 b0 4a 00 00       	call   310270 <std::__1::deque<std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::allocator<std::__1::vector<std::byte, std::__1::allocator<std::byte> > > >::pop_front()>
  30b7c0:	48 8b bd 90 f9 ff ff 	mov    -0x670(%rbp),%rdi
  30b7c7:	48 8b 77 08          	mov    0x8(%rdi),%rsi
  30b7cb:	4d 89 f8             	mov    %r15,%r8
  30b7ce:	49 29 d8             	sub    %rbx,%r8
  30b7d1:	48 89 da             	mov    %rbx,%rdx
  30b7d4:	4c 89 f9             	mov    %r15,%rcx
  30b7d7:	e8 b4 dc fa ff       	call   2b9490 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*>, long)>
  30b7dc:	48 89 9d c8 fe ff ff 	mov    %rbx,-0x138(%rbp)
  30b7e3:	4c 89 bd d0 fe ff ff 	mov    %r15,-0x130(%rbp)
  30b7ea:	4c 89 ad d8 fe ff ff 	mov    %r13,-0x128(%rbp)
  30b7f1:	c6 85 30 ff ff ff 00 	movb   $0x0,-0xd0(%rbp)
  30b7f8:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b7fc:	c5 fe 7f 85 e0 fe ff 	vmovdqu %ymm0,-0x120(%rbp)
  30b803:	ff 
  30b804:	c5 fe 7f 85 00 ff ff 	vmovdqu %ymm0,-0x100(%rbp)
  30b80b:	ff 
  30b80c:	c5 fe 7f 85 09 ff ff 	vmovdqu %ymm0,-0xf7(%rbp)
  30b813:	ff 
  30b814:	66 c7 85 38 ff ff ff 	movw   $0x3,-0xc8(%rbp)
  30b81b:	03 00 
  30b81d:	c6 85 3a ff ff ff 00 	movb   $0x0,-0xc6(%rbp)
  30b824:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30b82b:	48 8d b5 c8 fe ff ff 	lea    -0x138(%rbp),%rsi
  30b832:	c5 f8 77             	vzeroupper
  30b835:	e8 f6 d2 03 00       	call   348b30 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::push_back(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline)>
  30b83a:	48 8d bd c8 fe ff ff 	lea    -0x138(%rbp),%rdi
  30b841:	e8 4a 42 ff ff       	call   2ffa90 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30b846:	48 8b 9d a0 f9 ff ff 	mov    -0x660(%rbp),%rbx
  30b84d:	e9 6b 01 00 00       	jmp    30b9bd <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x77d>
  30b852:	48 8b 80 58 03 00 00 	mov    0x358(%rax),%rax
  30b859:	4c 8b b8 b8 06 00 00 	mov    0x6b8(%rax),%r15
  30b860:	48 8b 98 c0 06 00 00 	mov    0x6c0(%rax),%rbx
  30b867:	4c 8b a0 c8 06 00 00 	mov    0x6c8(%rax),%r12
  30b86e:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b872:	c5 fa 7f 80 b8 06 00 	vmovdqu %xmm0,0x6b8(%rax)
  30b879:	00 
  30b87a:	48 c7 80 c8 06 00 00 	movq   $0x0,0x6c8(%rax)
  30b881:	00 00 00 00 
  30b885:	49 39 df             	cmp    %rbx,%r15
  30b888:	0f 84 12 01 00 00    	je     30b9a0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x760>
  30b88e:	49 8b 06             	mov    (%r14),%rax
  30b891:	48 8b 80 90 2b 00 00 	mov    0x2b90(%rax),%rax
  30b898:	48 89 85 d0 fd ff ff 	mov    %rax,-0x230(%rbp)
  30b89f:	4c 89 bd d8 fd ff ff 	mov    %r15,-0x228(%rbp)
  30b8a6:	4c 29 fb             	sub    %r15,%rbx
  30b8a9:	48 89 9d e0 fd ff ff 	mov    %rbx,-0x220(%rbp)
  30b8b0:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30b8b7:	48 8d b5 d0 fd ff ff 	lea    -0x230(%rbp),%rsi
  30b8be:	c5 f8 77             	vzeroupper
  30b8c1:	e8 9a b0 03 00       	call   346960 <cnetmod::quic::encode_frame@cnetmod.protocol.quic(cnetmod::quic::crypto_frame@cnetmod.protocol.quic const&)>
  30b8c6:	49 8b 06             	mov    (%r14),%rax
  30b8c9:	48 01 98 90 2b 00 00 	add    %rbx,0x2b90(%rax)
  30b8d0:	48 8b bd 90 f9 ff ff 	mov    -0x670(%rbp),%rdi
  30b8d7:	48 8b 77 08          	mov    0x8(%rdi),%rsi
  30b8db:	48 8b 95 40 fd ff ff 	mov    -0x2c0(%rbp),%rdx
  30b8e2:	48 8b 8d 48 fd ff ff 	mov    -0x2b8(%rbp),%rcx
  30b8e9:	49 89 c8             	mov    %rcx,%r8
  30b8ec:	49 29 d0             	sub    %rdx,%r8
  30b8ef:	e8 9c db fa ff       	call   2b9490 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*>, long)>
  30b8f4:	48 8b 85 50 fd ff ff 	mov    -0x2b0(%rbp),%rax
  30b8fb:	c5 f9 6f 85 40 fd ff 	vmovdqa -0x2c0(%rbp),%xmm0
  30b902:	ff 
  30b903:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30b907:	c5 f8 29 8d 40 fd ff 	vmovaps %xmm1,-0x2c0(%rbp)
  30b90e:	ff 
  30b90f:	48 c7 85 50 fd ff ff 	movq   $0x0,-0x2b0(%rbp)
  30b916:	00 00 00 00 
  30b91a:	c5 f9 7f 85 50 fe ff 	vmovdqa %xmm0,-0x1b0(%rbp)
  30b921:	ff 
  30b922:	48 89 85 60 fe ff ff 	mov    %rax,-0x1a0(%rbp)
  30b929:	c6 85 b8 fe ff ff 00 	movb   $0x0,-0x148(%rbp)
  30b930:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b934:	c5 fe 7f 85 68 fe ff 	vmovdqu %ymm0,-0x198(%rbp)
  30b93b:	ff 
  30b93c:	c5 fe 7f 85 88 fe ff 	vmovdqu %ymm0,-0x178(%rbp)
  30b943:	ff 
  30b944:	c5 fe 7f 85 91 fe ff 	vmovdqu %ymm0,-0x16f(%rbp)
  30b94b:	ff 
  30b94c:	66 c7 85 c0 fe ff ff 	movw   $0x3,-0x140(%rbp)
  30b953:	03 00 
  30b955:	c6 85 c2 fe ff ff 00 	movb   $0x0,-0x13e(%rbp)
  30b95c:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30b963:	48 8d b5 50 fe ff ff 	lea    -0x1b0(%rbp),%rsi
  30b96a:	c5 f8 77             	vzeroupper
  30b96d:	e8 be d1 03 00       	call   348b30 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::push_back(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline)>
  30b972:	48 8d bd 50 fe ff ff 	lea    -0x1b0(%rbp),%rdi
  30b979:	e8 12 41 ff ff       	call   2ffa90 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30b97e:	48 8b bd 40 fd ff ff 	mov    -0x2c0(%rbp),%rdi
  30b985:	48 85 ff             	test   %rdi,%rdi
  30b988:	74 16                	je     30b9a0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x760>
  30b98a:	48 89 bd 48 fd ff ff 	mov    %rdi,-0x2b8(%rbp)
  30b991:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30b998:	48 29 fe             	sub    %rdi,%rsi
  30b99b:	e8 20 f0 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b9a0:	4d 85 ff             	test   %r15,%r15
  30b9a3:	48 8b 9d a0 f9 ff ff 	mov    -0x660(%rbp),%rbx
  30b9aa:	74 11                	je     30b9bd <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x77d>
  30b9ac:	4d 29 fc             	sub    %r15,%r12
  30b9af:	4c 89 ff             	mov    %r15,%rdi
  30b9b2:	4c 89 e6             	mov    %r12,%rsi
  30b9b5:	c5 f8 77             	vzeroupper
  30b9b8:	e8 03 f0 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30b9bd:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30b9c4:	eb 1a                	jmp    30b9e0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x7a0>
  30b9c6:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
  30b9cd:	00 00 00 
  30b9d0:	45 84 e4             	test   %r12b,%r12b
  30b9d3:	48 8b 9d a0 f9 ff ff 	mov    -0x660(%rbp),%rbx
  30b9da:	0f 84 a0 08 00 00    	je     30c280 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1040>
  30b9e0:	48 8b 85 98 f9 ff ff 	mov    -0x668(%rbp),%rax
  30b9e7:	48 8b 30             	mov    (%rax),%rsi
  30b9ea:	4c 8b a6 f0 06 00 00 	mov    0x6f0(%rsi),%r12
  30b9f1:	4d 85 e4             	test   %r12,%r12
  30b9f4:	75 6a                	jne    30ba60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x820>
  30b9f6:	48 8b 86 f8 06 00 00 	mov    0x6f8(%rsi),%rax
  30b9fd:	48 83 78 40 00       	cmpq   $0x0,0x40(%rax)
  30ba02:	75 5c                	jne    30ba60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x820>
  30ba04:	48 83 b8 30 01 00 00 	cmpq   $0x0,0x130(%rax)
  30ba0b:	00 
  30ba0c:	75 52                	jne    30ba60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x820>
  30ba0e:	48 83 b8 60 01 00 00 	cmpq   $0x0,0x160(%rax)
  30ba15:	00 
  30ba16:	75 48                	jne    30ba60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x820>
  30ba18:	48 83 b8 90 01 00 00 	cmpq   $0x0,0x190(%rax)
  30ba1f:	00 
  30ba20:	75 3e                	jne    30ba60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x820>
  30ba22:	48 83 b8 c0 01 00 00 	cmpq   $0x0,0x1c0(%rax)
  30ba29:	00 
  30ba2a:	75 34                	jne    30ba60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x820>
  30ba2c:	48 83 b8 f0 01 00 00 	cmpq   $0x0,0x1f0(%rax)
  30ba33:	00 
  30ba34:	75 2a                	jne    30ba60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x820>
  30ba36:	48 83 b8 20 02 00 00 	cmpq   $0x0,0x220(%rax)
  30ba3d:	00 
  30ba3e:	75 20                	jne    30ba60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x820>
  30ba40:	48 83 b8 50 02 00 00 	cmpq   $0x0,0x250(%rax)
  30ba47:	00 
  30ba48:	75 16                	jne    30ba60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x820>
  30ba4a:	48 83 b8 80 02 00 00 	cmpq   $0x0,0x280(%rax)
  30ba51:	00 
  30ba52:	0f 84 0e 08 00 00    	je     30c266 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1026>
  30ba58:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
  30ba5f:	00 
  30ba60:	49 8b 46 08          	mov    0x8(%r14),%rax
  30ba64:	49 2b 06             	sub    (%r14),%rax
  30ba67:	48 39 d8             	cmp    %rbx,%rax
  30ba6a:	0f 83 f6 07 00 00    	jae    30c266 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1026>
  30ba70:	c6 85 38 fe ff ff 00 	movb   $0x0,-0x1c8(%rbp)
  30ba77:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30ba7b:	c5 fe 7f 85 d0 fd ff 	vmovdqu %ymm0,-0x230(%rbp)
  30ba82:	ff 
  30ba83:	c5 fe 7f 85 f0 fd ff 	vmovdqu %ymm0,-0x210(%rbp)
  30ba8a:	ff 
  30ba8b:	c5 fe 7f 85 10 fe ff 	vmovdqu %ymm0,-0x1f0(%rbp)
  30ba92:	ff 
  30ba93:	c6 85 30 fe ff ff 00 	movb   $0x0,-0x1d0(%rbp)
  30ba9a:	49 81 e7 00 ff ff ff 	and    $0xffffffffffffff00,%r15
  30baa1:	66 c7 85 40 fe ff ff 	movw   $0x3,-0x1c0(%rbp)
  30baa8:	03 00 
  30baaa:	c6 85 42 fe ff ff 00 	movb   $0x0,-0x1be(%rbp)
  30bab1:	4d 85 e4             	test   %r12,%r12
  30bab4:	0f 84 16 01 00 00    	je     30bbd0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x990>
  30baba:	48 8b 86 d0 06 00 00 	mov    0x6d0(%rsi),%rax
  30bac1:	48 8b 96 e8 06 00 00 	mov    0x6e8(%rsi),%rdx
  30bac8:	48 b9 f1 f0 f0 f0 f0 	movabs $0xf0f0f0f0f0f0f0f1,%rcx
  30bacf:	f0 f0 f0 
  30bad2:	c4 e2 f3 f6 c9       	mulx   %rcx,%rcx,%rcx
  30bad7:	48 89 ce             	mov    %rcx,%rsi
  30bada:	48 c1 ee 05          	shr    $0x5,%rsi
  30bade:	4c 8b 2c f0          	mov    (%rax,%rsi,8),%r13
  30bae2:	48 b8 e0 ff ff ff ff 	movabs $0x1fffffffffffffe0,%rax
  30bae9:	ff ff 1f 
  30baec:	48 21 c1             	and    %rax,%rcx
  30baef:	48 8d 04 71          	lea    (%rcx,%rsi,2),%rax
  30baf3:	48 29 c2             	sub    %rax,%rdx
  30baf6:	4c 6b f2 78          	imul   $0x78,%rdx,%r14
  30bafa:	c4 81 78 10 44 35 00 	vmovups 0x0(%r13,%r14,1),%xmm0
  30bb01:	c5 f8 29 85 d0 fd ff 	vmovaps %xmm0,-0x230(%rbp)
  30bb08:	ff 
  30bb09:	4b 8b 44 35 10       	mov    0x10(%r13,%r14,1),%rax
  30bb0e:	48 89 85 e0 fd ff ff 	mov    %rax,-0x220(%rbp)
  30bb15:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30bb19:	c4 81 78 11 4c 35 00 	vmovups %xmm1,0x0(%r13,%r14,1)
  30bb20:	4b c7 44 35 10 00 00 	movq   $0x0,0x10(%r13,%r14,1)
  30bb27:	00 00 
  30bb29:	c4 81 78 10 44 35 18 	vmovups 0x18(%r13,%r14,1),%xmm0
  30bb30:	c4 81 78 11 4c 35 18 	vmovups %xmm1,0x18(%r13,%r14,1)
  30bb37:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bb3e:	c5 f8 11 85 e8 fd ff 	vmovups %xmm0,-0x218(%rbp)
  30bb45:	ff 
  30bb46:	48 85 db             	test   %rbx,%rbx
  30bb49:	74 26                	je     30bb71 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x931>
  30bb4b:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30bb52:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30bb58:	48 85 c0             	test   %rax,%rax
  30bb5b:	75 14                	jne    30bb71 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x931>
  30bb5d:	48 8b 03             	mov    (%rbx),%rax
  30bb60:	48 89 df             	mov    %rbx,%rdi
  30bb63:	c5 f8 77             	vzeroupper
  30bb66:	ff 50 10             	call   *0x10(%rax)
  30bb69:	48 89 df             	mov    %rbx,%rdi
  30bb6c:	e8 af ec dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30bb71:	4d 01 f5             	add    %r14,%r13
  30bb74:	c4 c1 7e 6f 45 28    	vmovdqu 0x28(%r13),%ymm0
  30bb7a:	c4 c1 7c 10 4d 48    	vmovups 0x48(%r13),%ymm1
  30bb80:	c4 c1 7c 10 55 53    	vmovups 0x53(%r13),%ymm2
  30bb86:	48 8d 85 e8 fd ff ff 	lea    -0x218(%rbp),%rax
  30bb8d:	c5 fc 11 50 3b       	vmovups %ymm2,0x3b(%rax)
  30bb92:	c5 fc 11 48 30       	vmovups %ymm1,0x30(%rax)
  30bb97:	c5 fe 7f 40 10       	vmovdqu %ymm0,0x10(%rax)
  30bb9c:	48 8b 85 98 f9 ff ff 	mov    -0x668(%rbp),%rax
  30bba3:	48 8b 38             	mov    (%rax),%rdi
  30bba6:	b8 c8 06 00 00       	mov    $0x6c8,%eax
  30bbab:	48 01 c7             	add    %rax,%rdi
  30bbae:	c5 f8 77             	vzeroupper
  30bbb1:	e8 8a 59 00 00       	call   311540 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::pop_front()>
  30bbb6:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30bbbd:	4c 8b 85 a0 f9 ff ff 	mov    -0x660(%rbp),%r8
  30bbc4:	e9 6d 01 00 00       	jmp    30bd36 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xaf6>
  30bbc9:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
  30bbd0:	48 81 c6 f8 06 00 00 	add    $0x6f8,%rsi
  30bbd7:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30bbde:	c5 f8 77             	vzeroupper
  30bbe1:	e8 5a 26 04 00       	call   34e240 <cnetmod::quic::quic_stream_scheduler@cnetmod.protocol.quic.send_pipeline::take()>
  30bbe6:	80 bd c8 fd ff ff 00 	cmpb   $0x0,-0x238(%rbp)
  30bbed:	0f 84 1d 04 00 00    	je     30c010 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xdd0>
  30bbf3:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30bbfa:	48 85 ff             	test   %rdi,%rdi
  30bbfd:	74 16                	je     30bc15 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x9d5>
  30bbff:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30bc06:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30bc0d:	48 29 fe             	sub    %rdi,%rsi
  30bc10:	e8 ab ed dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30bc15:	c5 f8 10 85 50 fd ff 	vmovups -0x2b0(%rbp),%xmm0
  30bc1c:	ff 
  30bc1d:	c5 f8 29 85 d0 fd ff 	vmovaps %xmm0,-0x230(%rbp)
  30bc24:	ff 
  30bc25:	48 8b 85 60 fd ff ff 	mov    -0x2a0(%rbp),%rax
  30bc2c:	48 89 85 e0 fd ff ff 	mov    %rax,-0x220(%rbp)
  30bc33:	48 8d 85 50 fd ff ff 	lea    -0x2b0(%rbp),%rax
  30bc3a:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30bc3e:	c5 f8 11 08          	vmovups %xmm1,(%rax)
  30bc42:	48 c7 40 10 00 00 00 	movq   $0x0,0x10(%rax)
  30bc49:	00 
  30bc4a:	c5 f8 10 85 68 fd ff 	vmovups -0x298(%rbp),%xmm0
  30bc51:	ff 
  30bc52:	c5 f8 11 48 18       	vmovups %xmm1,0x18(%rax)
  30bc57:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bc5e:	c5 f8 11 85 e8 fd ff 	vmovups %xmm0,-0x218(%rbp)
  30bc65:	ff 
  30bc66:	48 85 db             	test   %rbx,%rbx
  30bc69:	74 23                	je     30bc8e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xa4e>
  30bc6b:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30bc72:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30bc78:	48 85 c0             	test   %rax,%rax
  30bc7b:	75 11                	jne    30bc8e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xa4e>
  30bc7d:	48 8b 03             	mov    (%rbx),%rax
  30bc80:	48 89 df             	mov    %rbx,%rdi
  30bc83:	ff 50 10             	call   *0x10(%rax)
  30bc86:	48 89 df             	mov    %rbx,%rdi
  30bc89:	e8 92 eb dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30bc8e:	48 8d 85 50 fd ff ff 	lea    -0x2b0(%rbp),%rax
  30bc95:	c5 fe 6f 40 28       	vmovdqu 0x28(%rax),%ymm0
  30bc9a:	c5 fc 10 48 48       	vmovups 0x48(%rax),%ymm1
  30bc9f:	c5 fc 10 50 53       	vmovups 0x53(%rax),%ymm2
  30bca4:	48 8d 85 e8 fd ff ff 	lea    -0x218(%rbp),%rax
  30bcab:	c5 fc 11 50 3b       	vmovups %ymm2,0x3b(%rax)
  30bcb0:	c5 fc 11 48 30       	vmovups %ymm1,0x30(%rax)
  30bcb5:	c5 fe 7f 40 10       	vmovdqu %ymm0,0x10(%rax)
  30bcba:	4c 8b bd 40 fd ff ff 	mov    -0x2c0(%rbp),%r15
  30bcc1:	0f b7 85 48 fd ff ff 	movzwl -0x2b8(%rbp),%eax
  30bcc8:	48 89 85 58 f9 ff ff 	mov    %rax,-0x6a8(%rbp)
  30bccf:	80 bd c8 fd ff ff 01 	cmpb   $0x1,-0x238(%rbp)
  30bcd6:	75 57                	jne    30bd2f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xaef>
  30bcd8:	48 8b 9d 70 fd ff ff 	mov    -0x290(%rbp),%rbx
  30bcdf:	48 85 db             	test   %rbx,%rbx
  30bce2:	74 26                	je     30bd0a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xaca>
  30bce4:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30bceb:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30bcf1:	48 85 c0             	test   %rax,%rax
  30bcf4:	75 14                	jne    30bd0a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xaca>
  30bcf6:	48 8b 03             	mov    (%rbx),%rax
  30bcf9:	48 89 df             	mov    %rbx,%rdi
  30bcfc:	c5 f8 77             	vzeroupper
  30bcff:	ff 50 10             	call   *0x10(%rax)
  30bd02:	48 89 df             	mov    %rbx,%rdi
  30bd05:	e8 16 eb dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30bd0a:	48 8b bd 50 fd ff ff 	mov    -0x2b0(%rbp),%rdi
  30bd11:	48 85 ff             	test   %rdi,%rdi
  30bd14:	74 19                	je     30bd2f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xaef>
  30bd16:	48 89 bd 58 fd ff ff 	mov    %rdi,-0x2a8(%rbp)
  30bd1d:	48 8b b5 60 fd ff ff 	mov    -0x2a0(%rbp),%rsi
  30bd24:	48 29 fe             	sub    %rdi,%rsi
  30bd27:	c5 f8 77             	vzeroupper
  30bd2a:	e8 91 ec dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30bd2f:	4c 8b 85 a0 f9 ff ff 	mov    -0x660(%rbp),%r8
  30bd36:	48 8b 95 d0 fd ff ff 	mov    -0x230(%rbp),%rdx
  30bd3d:	48 8b 8d d8 fd ff ff 	mov    -0x228(%rbp),%rcx
  30bd44:	48 89 ce             	mov    %rcx,%rsi
  30bd47:	48 29 d6             	sub    %rdx,%rsi
  30bd4a:	48 8b 85 e8 fd ff ff 	mov    -0x218(%rbp),%rax
  30bd51:	48 8b bd 18 fe ff ff 	mov    -0x1e8(%rbp),%rdi
  30bd58:	48 01 f7             	add    %rsi,%rdi
  30bd5b:	48 03 bd 28 fe ff ff 	add    -0x1d8(%rbp),%rdi
  30bd62:	48 85 c0             	test   %rax,%rax
  30bd65:	48 0f 44 fe          	cmove  %rsi,%rdi
  30bd69:	4c 89 c6             	mov    %r8,%rsi
  30bd6c:	49 2b 76 08          	sub    0x8(%r14),%rsi
  30bd70:	49 03 36             	add    (%r14),%rsi
  30bd73:	48 39 f7             	cmp    %rsi,%rdi
  30bd76:	0f 86 44 01 00 00    	jbe    30bec0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xc80>
  30bd7c:	48 8b b5 98 f9 ff ff 	mov    -0x668(%rbp),%rsi
  30bd83:	4c 8b 2e             	mov    (%rsi),%r13
  30bd86:	4d 85 e4             	test   %r12,%r12
  30bd89:	0f 84 72 01 00 00    	je     30bf01 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xcc1>
  30bd8f:	49 8b 95 e8 06 00 00 	mov    0x6e8(%r13),%rdx
  30bd96:	48 85 d2             	test   %rdx,%rdx
  30bd99:	75 16                	jne    30bdb1 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xb71>
  30bd9b:	49 8d bd c8 06 00 00 	lea    0x6c8(%r13),%rdi
  30bda2:	c5 f8 77             	vzeroupper
  30bda5:	e8 76 db 00 00       	call   319920 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::__add_front_capacity()>
  30bdaa:	49 8b 95 e8 06 00 00 	mov    0x6e8(%r13),%rdx
  30bdb1:	49 8b 8d d0 06 00 00 	mov    0x6d0(%r13),%rcx
  30bdb8:	48 b8 f1 f0 f0 f0 f0 	movabs $0xf0f0f0f0f0f0f0f1,%rax
  30bdbf:	f0 f0 f0 
  30bdc2:	c4 e2 fb f6 c0       	mulx   %rax,%rax,%rax
  30bdc7:	48 89 c6             	mov    %rax,%rsi
  30bdca:	48 c1 ee 05          	shr    $0x5,%rsi
  30bdce:	48 bf e0 ff ff ff ff 	movabs $0x1fffffffffffffe0,%rdi
  30bdd5:	ff ff 1f 
  30bdd8:	48 21 f8             	and    %rdi,%rax
  30bddb:	48 8d 04 70          	lea    (%rax,%rsi,2),%rax
  30bddf:	48 29 c2             	sub    %rax,%rdx
  30bde2:	48 8b 3c f1          	mov    (%rcx,%rsi,8),%rdi
  30bde6:	48 6b c2 78          	imul   $0x78,%rdx,%rax
  30bdea:	48 01 f8             	add    %rdi,%rax
  30bded:	49 3b 8d d8 06 00 00 	cmp    0x6d8(%r13),%rcx
  30bdf4:	ba 00 00 00 00       	mov    $0x0,%edx
  30bdf9:	48 0f 44 c2          	cmove  %rdx,%rax
  30bdfd:	48 39 f8             	cmp    %rdi,%rax
  30be00:	75 0d                	jne    30be0f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xbcf>
  30be02:	48 8b 44 f1 f8       	mov    -0x8(%rcx,%rsi,8),%rax
  30be07:	b9 f0 0f 00 00       	mov    $0xff0,%ecx
  30be0c:	48 01 c8             	add    %rcx,%rax
  30be0f:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30be13:	c5 f8 11 48 88       	vmovups %xmm1,-0x78(%rax)
  30be18:	48 c7 40 98 00 00 00 	movq   $0x0,-0x68(%rax)
  30be1f:	00 
  30be20:	c5 f8 28 85 d0 fd ff 	vmovaps -0x230(%rbp),%xmm0
  30be27:	ff 
  30be28:	c5 f8 11 40 88       	vmovups %xmm0,-0x78(%rax)
  30be2d:	48 8b 8d e0 fd ff ff 	mov    -0x220(%rbp),%rcx
  30be34:	48 89 48 98          	mov    %rcx,-0x68(%rax)
  30be38:	c5 f8 29 8d d0 fd ff 	vmovaps %xmm1,-0x230(%rbp)
  30be3f:	ff 
  30be40:	48 c7 85 e0 fd ff ff 	movq   $0x0,-0x220(%rbp)
  30be47:	00 00 00 00 
  30be4b:	c5 f8 10 85 e8 fd ff 	vmovups -0x218(%rbp),%xmm0
  30be52:	ff 
  30be53:	c5 f8 11 40 a0       	vmovups %xmm0,-0x60(%rax)
  30be58:	48 8d 8d e8 fd ff ff 	lea    -0x218(%rbp),%rcx
  30be5f:	c5 f8 11 09          	vmovups %xmm1,(%rcx)
  30be63:	c5 fc 10 41 10       	vmovups 0x10(%rcx),%ymm0
  30be68:	c5 fc 10 49 30       	vmovups 0x30(%rcx),%ymm1
  30be6d:	c5 fc 10 51 3b       	vmovups 0x3b(%rcx),%ymm2
  30be72:	c5 fc 11 50 db       	vmovups %ymm2,-0x25(%rax)
  30be77:	c5 fc 11 48 d0       	vmovups %ymm1,-0x30(%rax)
  30be7c:	c5 fc 11 40 b0       	vmovups %ymm0,-0x50(%rax)
  30be81:	c5 f9 6f 05 47 d1 dc 	vmovdqa -0x232eb9(%rip),%xmm0        # d8fd0 <typeinfo name for cnetmod::io_uring_context@cnetmod.io.platform.io_uring+0xbc2>
  30be88:	ff 
  30be89:	c4 c1 79 d4 85 e8 06 	vpaddq 0x6e8(%r13),%xmm0,%xmm0
  30be90:	00 00 
  30be92:	c4 c1 7a 7f 85 e8 06 	vmovdqu %xmm0,0x6e8(%r13)
  30be99:	00 00 
  30be9b:	45 31 e4             	xor    %r12d,%r12d
  30be9e:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bea5:	48 85 db             	test   %rbx,%rbx
  30bea8:	0f 85 4c 03 00 00    	jne    30c1fa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfba>
  30beae:	e9 6d 03 00 00       	jmp    30c220 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfe0>
  30beb3:	66 66 66 66 2e 0f 1f 	data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
  30beba:	84 00 00 00 00 00 
  30bec0:	48 8d bd d0 fd ff ff 	lea    -0x230(%rbp),%rdi
  30bec7:	4c 89 f6             	mov    %r14,%rsi
  30beca:	c5 f8 77             	vzeroupper
  30becd:	e8 9e 17 04 00       	call   34d670 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::append_to(std::__1::vector<std::byte, std::__1::allocator<std::byte> >&) const>
  30bed2:	4d 85 e4             	test   %r12,%r12
  30bed5:	0f 85 35 02 00 00    	jne    30c110 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xed0>
  30bedb:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
  30bee2:	48 3b 85 48 ff ff ff 	cmp    -0xb8(%rbp),%rax
  30bee9:	0f 85 39 01 00 00    	jne    30c028 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xde8>
  30beef:	48 83 7d 88 00       	cmpq   $0x0,-0x78(%rbp)
  30bef4:	0f 85 2e 01 00 00    	jne    30c028 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xde8>
  30befa:	31 c0                	xor    %eax,%eax
  30befc:	e9 44 01 00 00       	jmp    30c045 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe05>
  30bf01:	4c 89 bd b8 fc ff ff 	mov    %r15,-0x348(%rbp)
  30bf08:	48 8b b5 58 f9 ff ff 	mov    -0x6a8(%rbp),%rsi
  30bf0f:	66 89 b5 c0 fc ff ff 	mov    %si,-0x340(%rbp)
  30bf16:	48 89 95 c8 fc ff ff 	mov    %rdx,-0x338(%rbp)
  30bf1d:	48 89 8d d0 fc ff ff 	mov    %rcx,-0x330(%rbp)
  30bf24:	48 8b 8d e0 fd ff ff 	mov    -0x220(%rbp),%rcx
  30bf2b:	48 8b 95 f0 fd ff ff 	mov    -0x210(%rbp),%rdx
  30bf32:	48 89 8d d8 fc ff ff 	mov    %rcx,-0x328(%rbp)
  30bf39:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30bf3d:	c5 f9 7f 85 d0 fd ff 	vmovdqa %xmm0,-0x230(%rbp)
  30bf44:	ff 
  30bf45:	48 c7 85 e0 fd ff ff 	movq   $0x0,-0x220(%rbp)
  30bf4c:	00 00 00 00 
  30bf50:	48 89 85 e0 fc ff ff 	mov    %rax,-0x320(%rbp)
  30bf57:	48 89 95 e8 fc ff ff 	mov    %rdx,-0x318(%rbp)
  30bf5e:	48 8d 85 e8 fd ff ff 	lea    -0x218(%rbp),%rax
  30bf65:	c5 fa 7f 00          	vmovdqu %xmm0,(%rax)
  30bf69:	c5 fe 6f 40 10       	vmovdqu 0x10(%rax),%ymm0
  30bf6e:	c5 fc 10 48 30       	vmovups 0x30(%rax),%ymm1
  30bf73:	c5 fc 10 50 3b       	vmovups 0x3b(%rax),%ymm2
  30bf78:	48 8d 85 f0 fc ff ff 	lea    -0x310(%rbp),%rax
  30bf7f:	c5 fc 11 50 2b       	vmovups %ymm2,0x2b(%rax)
  30bf84:	c5 fc 11 48 20       	vmovups %ymm1,0x20(%rax)
  30bf89:	c5 fe 7f 00          	vmovdqu %ymm0,(%rax)
  30bf8d:	4c 89 ef             	mov    %r13,%rdi
  30bf90:	48 8d b5 b8 fc ff ff 	lea    -0x348(%rbp),%rsi
  30bf97:	c5 f8 77             	vzeroupper
  30bf9a:	e8 61 3b ff ff       	call   2ffb00 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::requeue_prioritized_stream_frame(cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline)>
  30bf9f:	48 8b 9d e8 fc ff ff 	mov    -0x318(%rbp),%rbx
  30bfa6:	48 85 db             	test   %rbx,%rbx
  30bfa9:	74 23                	je     30bfce <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xd8e>
  30bfab:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30bfb2:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30bfb8:	48 85 c0             	test   %rax,%rax
  30bfbb:	75 11                	jne    30bfce <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xd8e>
  30bfbd:	48 8b 03             	mov    (%rbx),%rax
  30bfc0:	48 89 df             	mov    %rbx,%rdi
  30bfc3:	ff 50 10             	call   *0x10(%rax)
  30bfc6:	48 89 df             	mov    %rbx,%rdi
  30bfc9:	e8 52 e8 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30bfce:	48 8b bd c8 fc ff ff 	mov    -0x338(%rbp),%rdi
  30bfd5:	48 85 ff             	test   %rdi,%rdi
  30bfd8:	74 36                	je     30c010 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xdd0>
  30bfda:	48 89 bd d0 fc ff ff 	mov    %rdi,-0x330(%rbp)
  30bfe1:	48 8b b5 d8 fc ff ff 	mov    -0x328(%rbp),%rsi
  30bfe8:	48 29 fe             	sub    %rdi,%rsi
  30bfeb:	e8 d0 e9 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30bff0:	45 31 e4             	xor    %r12d,%r12d
  30bff3:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bffa:	48 85 db             	test   %rbx,%rbx
  30bffd:	0f 85 f7 01 00 00    	jne    30c1fa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfba>
  30c003:	e9 18 02 00 00       	jmp    30c220 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfe0>
  30c008:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
  30c00f:	00 
  30c010:	45 31 e4             	xor    %r12d,%r12d
  30c013:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30c01a:	48 85 db             	test   %rbx,%rbx
  30c01d:	0f 85 d7 01 00 00    	jne    30c1fa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfba>
  30c023:	e9 f8 01 00 00       	jmp    30c220 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfe0>
  30c028:	48 8b 45 c0          	mov    -0x40(%rbp),%rax
  30c02c:	48 2b 45 b8          	sub    -0x48(%rbp),%rax
  30c030:	48 c1 f8 03          	sar    $0x3,%rax
  30c034:	48 b9 ef ee ee ee ee 	movabs $0xeeeeeeeeeeeeeeef,%rcx
  30c03b:	ee ee ee 
  30c03e:	48 0f af c1          	imul   %rcx,%rax
  30c042:	48 ff c0             	inc    %rax
  30c045:	4c 89 bd 90 fa ff ff 	mov    %r15,-0x570(%rbp)
  30c04c:	48 8b 8d 58 f9 ff ff 	mov    -0x6a8(%rbp),%rcx
  30c053:	48 89 8d 98 fa ff ff 	mov    %rcx,-0x568(%rbp)
  30c05a:	48 89 85 e8 f9 ff ff 	mov    %rax,-0x618(%rbp)
  30c061:	80 bd 28 fa ff ff 00 	cmpb   $0x0,-0x5d8(%rbp)
  30c068:	74 70                	je     30c0da <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe9a>
  30c06a:	48 8b 8d b8 f9 ff ff 	mov    -0x648(%rbp),%rcx
  30c071:	48 89 8d f0 f9 ff ff 	mov    %rcx,-0x610(%rbp)
  30c078:	48 8d 95 f0 f9 ff ff 	lea    -0x610(%rbp),%rdx
  30c07f:	48 89 95 40 fd ff ff 	mov    %rdx,-0x2c0(%rbp)
  30c086:	48 8d 95 e8 f9 ff ff 	lea    -0x618(%rbp),%rdx
  30c08d:	48 89 95 48 fd ff ff 	mov    %rdx,-0x2b8(%rbp)
  30c094:	48 8d 95 90 fa ff ff 	lea    -0x570(%rbp),%rdx
  30c09b:	48 89 95 50 fd ff ff 	mov    %rdx,-0x2b0(%rbp)
  30c0a2:	48 8d 95 b0 f9 ff ff 	lea    -0x650(%rbp),%rdx
  30c0a9:	48 89 95 58 fd ff ff 	mov    %rdx,-0x2a8(%rbp)
  30c0b0:	48 3b 8d c0 f9 ff ff 	cmp    -0x640(%rbp),%rcx
  30c0b7:	0f 83 91 01 00 00    	jae    30c24e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x100e>
  30c0bd:	48 89 01             	mov    %rax,(%rcx)
  30c0c0:	c5 fa 6f 85 90 fa ff 	vmovdqu -0x570(%rbp),%xmm0
  30c0c7:	ff 
  30c0c8:	c5 fa 7f 41 08       	vmovdqu %xmm0,0x8(%rcx)
  30c0cd:	48 83 c1 18          	add    $0x18,%rcx
  30c0d1:	48 89 8d b8 f9 ff ff 	mov    %rcx,-0x648(%rbp)
  30c0d8:	eb 36                	jmp    30c110 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xed0>
  30c0da:	48 89 85 10 fa ff ff 	mov    %rax,-0x5f0(%rbp)
  30c0e1:	48 8b 85 90 fa ff ff 	mov    -0x570(%rbp),%rax
  30c0e8:	48 8d 8d 18 fa ff ff 	lea    -0x5e8(%rbp),%rcx
  30c0ef:	48 89 01             	mov    %rax,(%rcx)
  30c0f2:	48 8b 85 98 fa ff ff 	mov    -0x568(%rbp),%rax
  30c0f9:	48 89 41 08          	mov    %rax,0x8(%rcx)
  30c0fd:	c6 85 28 fa ff ff 01 	movb   $0x1,-0x5d8(%rbp)
  30c104:	66 66 66 2e 0f 1f 84 	data16 data16 cs nopw 0x0(%rax,%rax,1)
  30c10b:	00 00 00 00 00 
  30c110:	c5 f8 28 85 d0 fd ff 	vmovaps -0x230(%rbp),%xmm0
  30c117:	ff 
  30c118:	c5 f8 29 85 40 fc ff 	vmovaps %xmm0,-0x3c0(%rbp)
  30c11f:	ff 
  30c120:	48 8b 85 e0 fd ff ff 	mov    -0x220(%rbp),%rax
  30c127:	48 89 85 50 fc ff ff 	mov    %rax,-0x3b0(%rbp)
  30c12e:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30c132:	c5 f8 29 8d d0 fd ff 	vmovaps %xmm1,-0x230(%rbp)
  30c139:	ff 
  30c13a:	48 c7 85 e0 fd ff ff 	movq   $0x0,-0x220(%rbp)
  30c141:	00 00 00 00 
  30c145:	c5 f8 10 85 e8 fd ff 	vmovups -0x218(%rbp),%xmm0
  30c14c:	ff 
  30c14d:	c5 f8 11 85 58 fc ff 	vmovups %xmm0,-0x3a8(%rbp)
  30c154:	ff 
  30c155:	48 8d 85 e8 fd ff ff 	lea    -0x218(%rbp),%rax
  30c15c:	c5 f8 11 08          	vmovups %xmm1,(%rax)
  30c160:	c5 fe 6f 40 10       	vmovdqu 0x10(%rax),%ymm0
  30c165:	c5 fc 10 48 30       	vmovups 0x30(%rax),%ymm1
  30c16a:	c5 fc 10 50 3b       	vmovups 0x3b(%rax),%ymm2
  30c16f:	48 8d 85 68 fc ff ff 	lea    -0x398(%rbp),%rax
  30c176:	c5 fc 11 50 2b       	vmovups %ymm2,0x2b(%rax)
  30c17b:	c5 fc 11 48 20       	vmovups %ymm1,0x20(%rax)
  30c180:	c5 fe 7f 00          	vmovdqu %ymm0,(%rax)
  30c184:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30c18b:	48 8d b5 40 fc ff ff 	lea    -0x3c0(%rbp),%rsi
  30c192:	c5 f8 77             	vzeroupper
  30c195:	e8 96 c9 03 00       	call   348b30 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::push_back(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline)>
  30c19a:	48 8b 9d 60 fc ff ff 	mov    -0x3a0(%rbp),%rbx
  30c1a1:	48 85 db             	test   %rbx,%rbx
  30c1a4:	74 23                	je     30c1c9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xf89>
  30c1a6:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30c1ad:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30c1b3:	48 85 c0             	test   %rax,%rax
  30c1b6:	75 11                	jne    30c1c9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xf89>
  30c1b8:	48 8b 03             	mov    (%rbx),%rax
  30c1bb:	48 89 df             	mov    %rbx,%rdi
  30c1be:	ff 50 10             	call   *0x10(%rax)
  30c1c1:	48 89 df             	mov    %rbx,%rdi
  30c1c4:	e8 57 e6 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30c1c9:	48 8b bd 40 fc ff ff 	mov    -0x3c0(%rbp),%rdi
  30c1d0:	41 b4 01             	mov    $0x1,%r12b
  30c1d3:	48 85 ff             	test   %rdi,%rdi
  30c1d6:	74 16                	je     30c1ee <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfae>
  30c1d8:	48 89 bd 48 fc ff ff 	mov    %rdi,-0x3b8(%rbp)
  30c1df:	48 8b b5 50 fc ff ff 	mov    -0x3b0(%rbp),%rsi
  30c1e6:	48 29 fe             	sub    %rdi,%rsi
  30c1e9:	e8 d2 e7 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c1ee:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30c1f5:	48 85 db             	test   %rbx,%rbx
  30c1f8:	74 26                	je     30c220 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfe0>
  30c1fa:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30c201:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30c207:	48 85 c0             	test   %rax,%rax
  30c20a:	75 14                	jne    30c220 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfe0>
  30c20c:	48 8b 03             	mov    (%rbx),%rax
  30c20f:	48 89 df             	mov    %rbx,%rdi
  30c212:	c5 f8 77             	vzeroupper
  30c215:	ff 50 10             	call   *0x10(%rax)
  30c218:	48 89 df             	mov    %rbx,%rdi
  30c21b:	e8 00 e6 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30c220:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30c227:	48 85 ff             	test   %rdi,%rdi
  30c22a:	0f 84 a0 f7 ff ff    	je     30b9d0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x790>
  30c230:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30c237:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30c23e:	48 29 fe             	sub    %rdi,%rsi
  30c241:	c5 f8 77             	vzeroupper
  30c244:	e8 77 e7 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c249:	e9 82 f7 ff ff       	jmp    30b9d0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x790>
  30c24e:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30c255:	e8 f6 bb 00 00       	call   317e50 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const>
  30c25a:	48 8b 8d f0 f9 ff ff 	mov    -0x610(%rbp),%rcx
  30c261:	e9 6b fe ff ff       	jmp    30c0d1 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe91>
  30c266:	4c 8b bd 98 f9 ff ff 	mov    -0x668(%rbp),%r15
  30c26d:	48 83 be 68 08 00 00 	cmpq   $0x0,0x868(%rsi)
  30c274:	00 
  30c275:	4c 8b a5 88 f9 ff ff 	mov    -0x678(%rbp),%r12
  30c27c:	75 1d                	jne    30c29b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x105b>
  30c27e:	eb 76                	jmp    30c2f6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10b6>
  30c280:	4c 8b bd 98 f9 ff ff 	mov    -0x668(%rbp),%r15
  30c287:	49 8b 37             	mov    (%r15),%rsi
  30c28a:	48 83 be 68 08 00 00 	cmpq   $0x0,0x868(%rsi)
  30c291:	00 
  30c292:	4c 8b a5 88 f9 ff ff 	mov    -0x678(%rbp),%r12
  30c299:	74 5b                	je     30c2f6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10b6>
  30c29b:	48 8b 86 48 08 00 00 	mov    0x848(%rsi),%rax
  30c2a2:	48 8b 96 60 08 00 00 	mov    0x860(%rsi),%rdx
  30c2a9:	49 89 d9             	mov    %rbx,%r9
  30c2ac:	48 bb c1 c0 c0 c0 c0 	movabs $0xc0c0c0c0c0c0c0c1,%rbx
  30c2b3:	c0 c0 c0 
  30c2b6:	c4 e2 f3 f6 cb       	mulx   %rbx,%rcx,%rcx
  30c2bb:	48 c1 e9 07          	shr    $0x7,%rcx
  30c2bf:	48 8b 04 c8          	mov    (%rax,%rcx,8),%rax
  30c2c3:	48 69 c9 aa 00 00 00 	imul   $0xaa,%rcx,%rcx
  30c2ca:	48 29 ca             	sub    %rcx,%rdx
  30c2cd:	49 8b 76 08          	mov    0x8(%r14),%rsi
  30c2d1:	48 89 f7             	mov    %rsi,%rdi
  30c2d4:	49 2b 3e             	sub    (%r14),%rdi
  30c2d7:	48 8d 0c 52          	lea    (%rdx,%rdx,2),%rcx
  30c2db:	48 8b 14 c8          	mov    (%rax,%rcx,8),%rdx
  30c2df:	48 8b 4c c8 08       	mov    0x8(%rax,%rcx,8),%rcx
  30c2e4:	49 89 c8             	mov    %rcx,%r8
  30c2e7:	49 29 d0             	sub    %rdx,%r8
  30c2ea:	4c 01 c7             	add    %r8,%rdi
  30c2ed:	4c 39 cf             	cmp    %r9,%rdi
  30c2f0:	0f 86 f8 06 00 00    	jbe    30c9ee <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x17ae>
  30c2f6:	45 31 ed             	xor    %r13d,%r13d
  30c2f9:	80 bd af f9 ff ff 01 	cmpb   $0x1,-0x651(%rbp)
  30c300:	75 3f                	jne    30c341 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1101>
  30c302:	48 8b 85 70 f9 ff ff 	mov    -0x690(%rbp),%rax
  30c309:	0f b6 40 14          	movzbl 0x14(%rax),%eax
  30c30d:	48 8d 48 15          	lea    0x15(%rax),%rcx
  30c311:	48 39 8d 60 f9 ff ff 	cmp    %rcx,-0x6a0(%rbp)
  30c318:	76 27                	jbe    30c341 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1101>
  30c31a:	48 8b b5 60 f9 ff ff 	mov    -0x6a0(%rbp),%rsi
  30c321:	48 29 c6             	sub    %rax,%rsi
  30c324:	48 83 c6 eb          	add    $0xffffffffffffffeb,%rsi
  30c328:	49 8b 46 08          	mov    0x8(%r14),%rax
  30c32c:	49 2b 06             	sub    (%r14),%rax
  30c32f:	48 39 f0             	cmp    %rsi,%rax
  30c332:	73 0d                	jae    30c341 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1101>
  30c334:	4c 89 f7             	mov    %r14,%rdi
  30c337:	31 d2                	xor    %edx,%edx
  30c339:	c5 f8 77             	vzeroupper
  30c33c:	e8 1f 50 00 00       	call   311360 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::resize(unsigned long, std::byte const&)>
  30c341:	49 8b 16             	mov    (%r14),%rdx
  30c344:	49 8b 4e 08          	mov    0x8(%r14),%rcx
  30c348:	48 39 ca             	cmp    %rcx,%rdx
  30c34b:	0f 84 a8 08 00 00    	je     30cbf9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x19b9>
  30c351:	48 8b b5 40 ff ff ff 	mov    -0xc0(%rbp),%rsi
  30c358:	48 8b bd 48 ff ff ff 	mov    -0xb8(%rbp),%rdi
  30c35f:	48 89 f0             	mov    %rsi,%rax
  30c362:	48 31 f8             	xor    %rdi,%rax
  30c365:	48 0b 45 88          	or     -0x78(%rbp),%rax
  30c369:	0f 95 c0             	setne  %al
  30c36c:	48 31 f7             	xor    %rsi,%rdi
  30c36f:	74 2a                	je     30c39b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x115b>
  30c371:	0f b6 36             	movzbl (%rsi),%esi
  30c374:	40 80 e6 fe          	and    $0xfe,%sil
  30c378:	40 80 fe 1c          	cmp    $0x1c,%sil
  30c37c:	75 1d                	jne    30c39b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x115b>
  30c37e:	48 83 bd 58 ff ff ff 	cmpq   $0x0,-0xa8(%rbp)
  30c385:	00 
  30c386:	75 13                	jne    30c39b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x115b>
  30c388:	45 31 ff             	xor    %r15d,%r15d
  30c38b:	4c 8b 8d 70 f9 ff ff 	mov    -0x690(%rbp),%r9
  30c392:	4c 8b 95 50 f9 ff ff 	mov    -0x6b0(%rbp),%r10
  30c399:	eb 5f                	jmp    30c3fa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11ba>
  30c39b:	48 8b 75 b8          	mov    -0x48(%rbp),%rsi
  30c39f:	48 8b 7d c0          	mov    -0x40(%rbp),%rdi
  30c3a3:	48 39 fe             	cmp    %rdi,%rsi
  30c3a6:	4c 8b 8d 70 f9 ff ff 	mov    -0x690(%rbp),%r9
  30c3ad:	4c 8b 95 50 f9 ff ff 	mov    -0x6b0(%rbp),%r10
  30c3b4:	75 1b                	jne    30c3d1 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1191>
  30c3b6:	41 b7 01             	mov    $0x1,%r15b
  30c3b9:	eb 3f                	jmp    30c3fa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11ba>
  30c3bb:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
  30c3c0:	45 31 ff             	xor    %r15d,%r15d
  30c3c3:	45 84 ff             	test   %r15b,%r15b
  30c3c6:	75 2e                	jne    30c3f6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11b6>
  30c3c8:	48 83 c6 78          	add    $0x78,%rsi
  30c3cc:	48 39 fe             	cmp    %rdi,%rsi
  30c3cf:	74 25                	je     30c3f6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11b6>
  30c3d1:	4c 8b 06             	mov    (%rsi),%r8
  30c3d4:	4c 3b 46 08          	cmp    0x8(%rsi),%r8
  30c3d8:	74 e6                	je     30c3c0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1180>
  30c3da:	45 0f b6 00          	movzbl (%r8),%r8d
  30c3de:	41 80 e0 fe          	and    $0xfe,%r8b
  30c3e2:	41 80 f8 1c          	cmp    $0x1c,%r8b
  30c3e6:	75 d8                	jne    30c3c0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1180>
  30c3e8:	48 83 7e 18 00       	cmpq   $0x0,0x18(%rsi)
  30c3ed:	41 0f 94 c7          	sete   %r15b
  30c3f1:	45 84 ff             	test   %r15b,%r15b
  30c3f4:	74 d2                	je     30c3c8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1188>
  30c3f6:	41 80 f7 01          	xor    $0x1,%r15b
  30c3fa:	41 0f b6 71 14       	movzbl 0x14(%r9),%esi
  30c3ff:	49 8b 7a 78          	mov    0x78(%r10),%rdi
  30c403:	48 29 d1             	sub    %rdx,%rcx
  30c406:	48 01 f1             	add    %rsi,%rcx
  30c409:	4c 8d 04 0f          	lea    (%rdi,%rcx,1),%r8
  30c40d:	49 83 c0 05          	add    $0x5,%r8
  30c411:	48 8b 8d 98 f9 ff ff 	mov    -0x668(%rbp),%rcx
  30c418:	48 8b 09             	mov    (%rcx),%rcx
  30c41b:	83 79 60 01          	cmpl   $0x1,0x60(%rcx)
  30c41f:	75 49                	jne    30c46a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x122a>
  30c421:	48 8b 8d 80 f9 ff ff 	mov    -0x680(%rbp),%rcx
  30c428:	80 b9 82 00 00 00 00 	cmpb   $0x0,0x82(%rcx)
  30c42f:	75 39                	jne    30c46a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x122a>
  30c431:	48 8b b5 80 f9 ff ff 	mov    -0x680(%rbp),%rsi
  30c438:	48 8b 8e 98 00 00 00 	mov    0x98(%rsi),%rcx
  30c43f:	48 ba 55 55 55 55 55 	movabs $0x5555555555555555,%rdx
  30c446:	55 55 55 
  30c449:	48 39 d1             	cmp    %rdx,%rcx
  30c44c:	48 0f 42 d1          	cmovb  %rcx,%rdx
  30c450:	48 8d 0c 52          	lea    (%rdx,%rdx,2),%rcx
  30c454:	48 2b 8e a0 00 00 00 	sub    0xa0(%rsi),%rcx
  30c45b:	0f 82 71 07 00 00    	jb     30cbd2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1992>
  30c461:	49 39 c8             	cmp    %rcx,%r8
  30c464:	0f 87 68 07 00 00    	ja     30cbd2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1992>
  30c46a:	80 bd 6c f9 ff ff 00 	cmpb   $0x0,-0x694(%rbp)
  30c471:	75 37                	jne    30c4aa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x126a>
  30c473:	44 08 e8             	or     %r13b,%al
  30c476:	0a 85 af f9 ff ff    	or     -0x651(%rbp),%al
  30c47c:	44 20 f8             	and    %r15b,%al
  30c47f:	3c 01                	cmp    $0x1,%al
  30c481:	75 27                	jne    30c4aa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x126a>
  30c483:	48 8b 85 80 f9 ff ff 	mov    -0x680(%rbp),%rax
  30c48a:	48 8d b8 e0 00 00 00 	lea    0xe0(%rax),%rdi
  30c491:	4c 89 c6             	mov    %r8,%rsi
  30c494:	4c 89 c3             	mov    %r8,%rbx
  30c497:	c5 f8 77             	vzeroupper
  30c49a:	e8 01 04 04 00       	call   34c8a0 <cnetmod::quic::quic_recovery_controller@cnetmod.protocol.quic::can_send_datagram(unsigned long) const>
  30c49f:	49 89 d8             	mov    %rbx,%r8
  30c4a2:	84 c0                	test   %al,%al
  30c4a4:	0f 84 d6 07 00 00    	je     30cc80 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a40>
  30c4aa:	44 89 ad 58 f9 ff ff 	mov    %r13d,-0x6a8(%rbp)
  30c4b1:	48 8b 8d 80 f9 ff ff 	mov    -0x680(%rbp),%rcx
  30c4b8:	48 8b 81 a8 00 00 00 	mov    0xa8(%rcx),%rax
  30c4bf:	48 89 85 a0 f9 ff ff 	mov    %rax,-0x660(%rbp)
  30c4c6:	48 ff c0             	inc    %rax
  30c4c9:	48 89 81 a8 00 00 00 	mov    %rax,0xa8(%rcx)
  30c4d0:	48 8b 8d 78 f9 ff ff 	mov    -0x688(%rbp),%rcx
  30c4d7:	4c 8b 31             	mov    (%rcx),%r14
  30c4da:	4c 89 b5 d0 fd ff ff 	mov    %r14,-0x230(%rbp)
  30c4e1:	4c 8b 69 08          	mov    0x8(%rcx),%r13
  30c4e5:	4c 89 ad d8 fd ff ff 	mov    %r13,-0x228(%rbp)
  30c4ec:	48 8b 41 10          	mov    0x10(%rcx),%rax
  30c4f0:	48 89 85 e0 fd ff ff 	mov    %rax,-0x220(%rbp)
  30c4f7:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30c4fb:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30c4ff:	48 c7 41 10 00 00 00 	movq   $0x0,0x10(%rcx)
  30c506:	00 
  30c507:	48 89 c3             	mov    %rax,%rbx
  30c50a:	4c 29 f3             	sub    %r14,%rbx
  30c50d:	49 39 d8             	cmp    %rbx,%r8
  30c510:	76 7d                	jbe    30c58f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x134f>
  30c512:	4d 85 c0             	test   %r8,%r8
  30c515:	0f 88 4c 0e 00 00    	js     30d367 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2127>
  30c51b:	4c 89 c7             	mov    %r8,%rdi
  30c51e:	4d 89 c4             	mov    %r8,%r12
  30c521:	c5 f8 77             	vzeroupper
  30c524:	e8 07 e5 dd ff       	call   eaa30 <_Znwm$plt>
  30c529:	4d 29 f5             	sub    %r14,%r13
  30c52c:	4a 8d 0c 28          	lea    (%rax,%r13,1),%rcx
  30c530:	48 89 8d 78 f9 ff ff 	mov    %rcx,-0x688(%rbp)
  30c537:	49 01 c4             	add    %rax,%r12
  30c53a:	48 89 c7             	mov    %rax,%rdi
  30c53d:	4c 89 f6             	mov    %r14,%rsi
  30c540:	4c 89 ea             	mov    %r13,%rdx
  30c543:	49 89 c5             	mov    %rax,%r13
  30c546:	e8 55 e9 dd ff       	call   eaea0 <memcpy$plt>
  30c54b:	4c 89 ad d0 fd ff ff 	mov    %r13,-0x230(%rbp)
  30c552:	4c 8b ad 78 f9 ff ff 	mov    -0x688(%rbp),%r13
  30c559:	4c 89 ad d8 fd ff ff 	mov    %r13,-0x228(%rbp)
  30c560:	4c 89 a5 e0 fd ff ff 	mov    %r12,-0x220(%rbp)
  30c567:	4c 89 e0             	mov    %r12,%rax
  30c56a:	4d 85 f6             	test   %r14,%r14
  30c56d:	4c 8b a5 88 f9 ff ff 	mov    -0x678(%rbp),%r12
  30c574:	74 19                	je     30c58f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x134f>
  30c576:	4c 89 f7             	mov    %r14,%rdi
  30c579:	48 89 de             	mov    %rbx,%rsi
  30c57c:	e8 3f e4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30c581:	4c 8b ad d8 fd ff ff 	mov    -0x228(%rbp),%r13
  30c588:	48 8b 85 e0 fd ff ff 	mov    -0x220(%rbp),%rax
  30c58f:	48 8b 8d 98 f9 ff ff 	mov    -0x668(%rbp),%rcx
  30c596:	48 8b 09             	mov    (%rcx),%rcx
  30c599:	48 8b 89 58 03 00 00 	mov    0x358(%rcx),%rcx
  30c5a0:	0f b6 89 59 06 00 00 	movzbl 0x659(%rcx),%ecx
  30c5a7:	c0 e1 02             	shl    $0x2,%cl
  30c5aa:	80 c9 43             	or     $0x43,%cl
  30c5ad:	88 8d f0 f9 ff ff    	mov    %cl,-0x610(%rbp)
  30c5b3:	4c 89 ad 90 fa ff ff 	mov    %r13,-0x570(%rbp)
  30c5ba:	48 8d 95 90 fa ff ff 	lea    -0x570(%rbp),%rdx
  30c5c1:	48 89 95 40 fd ff ff 	mov    %rdx,-0x2c0(%rbp)
  30c5c8:	48 8d 95 f0 f9 ff ff 	lea    -0x610(%rbp),%rdx
  30c5cf:	48 89 95 48 fd ff ff 	mov    %rdx,-0x2b8(%rbp)
  30c5d6:	48 8d 95 d0 fd ff ff 	lea    -0x230(%rbp),%rdx
  30c5dd:	48 89 95 50 fd ff ff 	mov    %rdx,-0x2b0(%rbp)
  30c5e4:	49 39 c5             	cmp    %rax,%r13
  30c5e7:	0f 83 a1 0c 00 00    	jae    30d28e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x204e>
  30c5ed:	41 88 4d 00          	mov    %cl,0x0(%r13)
  30c5f1:	49 ff c5             	inc    %r13
  30c5f4:	48 8b 95 70 f9 ff ff 	mov    -0x690(%rbp),%rdx
  30c5fb:	4c 89 ad d8 fd ff ff 	mov    %r13,-0x228(%rbp)
  30c602:	44 0f b6 42 14       	movzbl 0x14(%rdx),%r8d
  30c607:	4a 8d 0c 02          	lea    (%rdx,%r8,1),%rcx
  30c60b:	48 8d 9d d0 fd ff ff 	lea    -0x230(%rbp),%rbx
  30c612:	48 89 df             	mov    %rbx,%rdi
  30c615:	4c 89 ee             	mov    %r13,%rsi
  30c618:	c5 f8 77             	vzeroupper
  30c61b:	e8 30 df fa ff       	call   2ba550 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::byte const*, std::byte const*>(std::__1::__wrap_iter<std::byte const*>, std::byte const*, std::byte const*, long)>
  30c620:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30c627:	44 89 c8             	mov    %r9d,%eax
  30c62a:	c1 e8 18             	shr    $0x18,%eax
  30c62d:	88 85 f0 f9 ff ff    	mov    %al,-0x610(%rbp)
  30c633:	4c 8b b5 d8 fd ff ff 	mov    -0x228(%rbp),%r14
  30c63a:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c641:	48 8d 8d 90 fa ff ff 	lea    -0x570(%rbp),%rcx
  30c648:	48 89 8d 40 fd ff ff 	mov    %rcx,-0x2c0(%rbp)
  30c64f:	48 8d 8d f0 f9 ff ff 	lea    -0x610(%rbp),%rcx
  30c656:	48 89 8d 48 fd ff ff 	mov    %rcx,-0x2b8(%rbp)
  30c65d:	48 89 9d 50 fd ff ff 	mov    %rbx,-0x2b0(%rbp)
  30c664:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c66b:	0f 83 3f 0c 00 00    	jae    30d2b0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2070>
  30c671:	41 88 06             	mov    %al,(%r14)
  30c674:	49 ff c6             	inc    %r14
  30c677:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c67e:	44 89 c8             	mov    %r9d,%eax
  30c681:	c1 e8 10             	shr    $0x10,%eax
  30c684:	88 85 f0 f9 ff ff    	mov    %al,-0x610(%rbp)
  30c68a:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c691:	48 8d 8d 90 fa ff ff 	lea    -0x570(%rbp),%rcx
  30c698:	48 89 8d 40 fd ff ff 	mov    %rcx,-0x2c0(%rbp)
  30c69f:	48 8d 8d f0 f9 ff ff 	lea    -0x610(%rbp),%rcx
  30c6a6:	48 89 8d 48 fd ff ff 	mov    %rcx,-0x2b8(%rbp)
  30c6ad:	48 89 9d 50 fd ff ff 	mov    %rbx,-0x2b0(%rbp)
  30c6b4:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c6bb:	0f 83 15 0c 00 00    	jae    30d2d6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2096>
  30c6c1:	41 88 06             	mov    %al,(%r14)
  30c6c4:	49 ff c6             	inc    %r14
  30c6c7:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c6ce:	44 89 c8             	mov    %r9d,%eax
  30c6d1:	c1 e8 08             	shr    $0x8,%eax
  30c6d4:	88 85 f0 f9 ff ff    	mov    %al,-0x610(%rbp)
  30c6da:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c6e1:	48 8d 8d 90 fa ff ff 	lea    -0x570(%rbp),%rcx
  30c6e8:	48 89 8d 40 fd ff ff 	mov    %rcx,-0x2c0(%rbp)
  30c6ef:	48 8d 8d f0 f9 ff ff 	lea    -0x610(%rbp),%rcx
  30c6f6:	48 89 8d 48 fd ff ff 	mov    %rcx,-0x2b8(%rbp)
  30c6fd:	48 89 9d 50 fd ff ff 	mov    %rbx,-0x2b0(%rbp)
  30c704:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c70b:	0f 83 eb 0b 00 00    	jae    30d2fc <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x20bc>
  30c711:	41 88 06             	mov    %al,(%r14)
  30c714:	49 ff c6             	inc    %r14
  30c717:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c71e:	44 88 8d f0 f9 ff ff 	mov    %r9b,-0x610(%rbp)
  30c725:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c72c:	48 8d 85 90 fa ff ff 	lea    -0x570(%rbp),%rax
  30c733:	48 89 85 40 fd ff ff 	mov    %rax,-0x2c0(%rbp)
  30c73a:	48 8d 85 f0 f9 ff ff 	lea    -0x610(%rbp),%rax
  30c741:	48 89 85 48 fd ff ff 	mov    %rax,-0x2b8(%rbp)
  30c748:	48 89 9d 50 fd ff ff 	mov    %rbx,-0x2b0(%rbp)
  30c74f:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c756:	0f 83 c6 0b 00 00    	jae    30d322 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x20e2>
  30c75c:	45 88 0e             	mov    %r9b,(%r14)
  30c75f:	49 ff c6             	inc    %r14
  30c762:	48 8b 9d 48 f9 ff ff 	mov    -0x6b8(%rbp),%rbx
  30c769:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c770:	4c 2b b5 d0 fd ff ff 	sub    -0x230(%rbp),%r14
  30c777:	48 8b 85 90 f9 ff ff 	mov    -0x670(%rbp),%rax
  30c77e:	48 8b 10             	mov    (%rax),%rdx
  30c781:	48 8b 48 08          	mov    0x8(%rax),%rcx
  30c785:	48 29 d1             	sub    %rdx,%rcx
  30c788:	48 8b 85 80 f9 ff ff 	mov    -0x680(%rbp),%rax
  30c78f:	8b 00                	mov    (%rax),%eax
  30c791:	89 04 24             	mov    %eax,(%rsp)
  30c794:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30c79b:	4c 8d 85 d0 fd ff ff 	lea    -0x230(%rbp),%r8
  30c7a2:	48 89 de             	mov    %rbx,%rsi
  30c7a5:	e8 b6 37 03 00       	call   33ff60 <cnetmod::quic::append_sealed_payload@cnetmod.protocol.quic(cnetmod::quic::quic_level_keys@cnetmod.protocol.quic const&, std::__1::span<std::byte const, 18446744073709551615ul>, std::__1::vector<std::byte, std::__1::allocator<std::byte> >&, unsigned long, unsigned int)>
  30c7aa:	80 bd 50 fd ff ff 00 	cmpb   $0x0,-0x2b0(%rbp)
  30c7b1:	0f 84 02 04 00 00    	je     30cbb9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1979>
  30c7b7:	49 83 c6 fc          	add    $0xfffffffffffffffc,%r14
  30c7bb:	48 8b 95 d0 fd ff ff 	mov    -0x230(%rbp),%rdx
  30c7c2:	48 8b 8d d8 fd ff ff 	mov    -0x228(%rbp),%rcx
  30c7c9:	48 29 d1             	sub    %rdx,%rcx
  30c7cc:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30c7d3:	48 89 de             	mov    %rbx,%rsi
  30c7d6:	4d 89 f0             	mov    %r14,%r8
  30c7d9:	45 31 c9             	xor    %r9d,%r9d
  30c7dc:	e8 8f 2e 03 00       	call   33f670 <cnetmod::quic::protect_header@cnetmod.protocol.quic(cnetmod::quic::quic_level_keys@cnetmod.protocol.quic const&, std::__1::span<std::byte, 18446744073709551615ul>, unsigned long, bool)>
  30c7e1:	80 bd 50 fd ff ff 00 	cmpb   $0x0,-0x2b0(%rbp)
  30c7e8:	0f 84 cb 03 00 00    	je     30cbb9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1979>
  30c7ee:	48 8b 85 d8 fd ff ff 	mov    -0x228(%rbp),%rax
  30c7f5:	48 2b 85 d0 fd ff ff 	sub    -0x230(%rbp),%rax
  30c7fc:	48 8b 95 80 f9 ff ff 	mov    -0x680(%rbp),%rdx
  30c803:	48 03 82 a0 00 00 00 	add    0xa0(%rdx),%rax
  30c80a:	48 c7 c1 ff ff ff ff 	mov    $0xffffffffffffffff,%rcx
  30c811:	48 0f 43 c8          	cmovae %rax,%rcx
  30c815:	48 89 8a a0 00 00 00 	mov    %rcx,0xa0(%rdx)
  30c81c:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30c823:	75 0c                	jne    30c831 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x15f1>
  30c825:	48 8b bd 40 f9 ff ff 	mov    -0x6c0(%rbp),%rdi
  30c82c:	e8 ff 44 ff ff       	call   300d30 <std::__1::set<unsigned long, std::__1::less<unsigned long>, std::__1::allocator<unsigned long> >::clear[abi:nqe220108]()>
  30c831:	80 bd af f9 ff ff 01 	cmpb   $0x1,-0x651(%rbp)
  30c838:	48 8b 9d 80 f9 ff ff 	mov    -0x680(%rbp),%rbx
  30c83f:	75 25                	jne    30c866 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1626>
  30c841:	48 8b 85 60 f9 ff ff 	mov    -0x6a0(%rbp),%rax
  30c848:	48 89 83 70 1c 00 00 	mov    %rax,0x1c70(%rbx)
  30c84f:	c6 83 78 1c 00 00 01 	movb   $0x1,0x1c78(%rbx)
  30c856:	80 bb 68 1c 00 00 01 	cmpb   $0x1,0x1c68(%rbx)
  30c85d:	75 07                	jne    30c866 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1626>
  30c85f:	c6 83 68 1c 00 00 00 	movb   $0x0,0x1c68(%rbx)
  30c866:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
  30c86d:	48 33 85 48 ff ff ff 	xor    -0xb8(%rbp),%rax
  30c874:	48 0b 45 88          	or     -0x78(%rbp),%rax
  30c878:	0f 95 c0             	setne  %al
  30c87b:	44 0f b6 ad af f9 ff 	movzbl -0x651(%rbp),%r13d
  30c882:	ff 
  30c883:	8b 8d 58 f9 ff ff    	mov    -0x6a8(%rbp),%ecx
  30c889:	44 08 e9             	or     %r13b,%cl
  30c88c:	08 c1                	or     %al,%cl
  30c88e:	41 20 cf             	and    %cl,%r15b
  30c891:	48 81 c3 e0 00 00 00 	add    $0xe0,%rbx
  30c898:	4c 8b b5 d8 fd ff ff 	mov    -0x228(%rbp),%r14
  30c89f:	4c 2b b5 d0 fd ff ff 	sub    -0x230(%rbp),%r14
  30c8a6:	e8 95 da dd ff       	call   ea340 <_ZNSt3__16chrono12steady_clock3nowEv$plt>
  30c8ab:	c5 f8 28 85 40 ff ff 	vmovaps -0xc0(%rbp),%xmm0
  30c8b2:	ff 
  30c8b3:	c5 f8 29 85 a0 fa ff 	vmovaps %xmm0,-0x560(%rbp)
  30c8ba:	ff 
  30c8bb:	48 8b 8d 50 ff ff ff 	mov    -0xb0(%rbp),%rcx
  30c8c2:	48 89 8d b0 fa ff ff 	mov    %rcx,-0x550(%rbp)
  30c8c9:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
  30c8cd:	c5 f8 29 85 40 ff ff 	vmovaps %xmm0,-0xc0(%rbp)
  30c8d4:	ff 
  30c8d5:	48 c7 85 50 ff ff ff 	movq   $0x0,-0xb0(%rbp)
  30c8dc:	00 00 00 00 
  30c8e0:	c5 f8 10 8d 58 ff ff 	vmovups -0xa8(%rbp),%xmm1
  30c8e7:	ff 
  30c8e8:	c5 f8 11 8d b8 fa ff 	vmovups %xmm1,-0x548(%rbp)
  30c8ef:	ff 
  30c8f0:	c5 f8 11 85 58 ff ff 	vmovups %xmm0,-0xa8(%rbp)
  30c8f7:	ff 
  30c8f8:	c5 fc 10 8d 68 ff ff 	vmovups -0x98(%rbp),%ymm1
  30c8ff:	ff 
  30c900:	c5 fc 11 8d c8 fa ff 	vmovups %ymm1,-0x538(%rbp)
  30c907:	ff 
  30c908:	c5 fc 10 4d 88       	vmovups -0x78(%rbp),%ymm1
  30c90d:	c5 fc 11 8d e8 fa ff 	vmovups %ymm1,-0x518(%rbp)
  30c914:	ff 
  30c915:	c5 fc 10 4d 93       	vmovups -0x6d(%rbp),%ymm1
  30c91a:	c5 fc 11 8d f3 fa ff 	vmovups %ymm1,-0x50d(%rbp)
  30c921:	ff 
  30c922:	c5 f8 10 4d b8       	vmovups -0x48(%rbp),%xmm1
  30c927:	c5 f8 11 8d 18 fb ff 	vmovups %xmm1,-0x4e8(%rbp)
  30c92e:	ff 
  30c92f:	48 8b 4d c8          	mov    -0x38(%rbp),%rcx
  30c933:	48 89 8d 28 fb ff ff 	mov    %rcx,-0x4d8(%rbp)
  30c93a:	48 8d 4d b8          	lea    -0x48(%rbp),%rcx
  30c93e:	c5 f8 11 01          	vmovups %xmm0,(%rcx)
  30c942:	48 c7 41 10 00 00 00 	movq   $0x0,0x10(%rcx)
  30c949:	00 
  30c94a:	48 8b 8d 60 f9 ff ff 	mov    -0x6a0(%rbp),%rcx
  30c951:	48 89 8d f8 f9 ff ff 	mov    %rcx,-0x608(%rbp)
  30c958:	44 88 ad 00 fa ff ff 	mov    %r13b,-0x600(%rbp)
  30c95f:	8b 8d 08 fa ff ff    	mov    -0x5f8(%rbp),%ecx
  30c965:	8b 95 0b fa ff ff    	mov    -0x5f5(%rbp),%edx
  30c96b:	89 8d 01 fa ff ff    	mov    %ecx,-0x5ff(%rbp)
  30c971:	89 95 04 fa ff ff    	mov    %edx,-0x5fc(%rbp)
  30c977:	c5 fa 6f 85 f8 f9 ff 	vmovdqu -0x608(%rbp),%xmm0
  30c97e:	ff 
  30c97f:	c5 fa 7f 44 24 08    	vmovdqu %xmm0,0x8(%rsp)
  30c985:	0f b6 8d 6c f9 ff ff 	movzbl -0x694(%rbp),%ecx
  30c98c:	89 4c 24 18          	mov    %ecx,0x18(%rsp)
  30c990:	48 8d 8d a0 fa ff ff 	lea    -0x560(%rbp),%rcx
  30c997:	48 89 0c 24          	mov    %rcx,(%rsp)
  30c99b:	45 0f b6 c7          	movzbl %r15b,%r8d
  30c99f:	48 89 df             	mov    %rbx,%rdi
  30c9a2:	48 8b b5 a0 f9 ff ff 	mov    -0x660(%rbp),%rsi
  30c9a9:	4c 89 f2             	mov    %r14,%rdx
  30c9ac:	48 89 c1             	mov    %rax,%rcx
  30c9af:	41 b9 02 00 00 00    	mov    $0x2,%r9d
  30c9b5:	c5 f8 77             	vzeroupper
  30c9b8:	e8 13 d2 03 00       	call   349bd0 <cnetmod::quic::quic_recovery_controller@cnetmod.protocol.quic::on_packet_sent(unsigned long, unsigned long, std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >, bool, cnetmod::quic::pn_space@cnetmod.protocol.quic, cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic, std::__1::optional<unsigned long>, bool)>
  30c9bd:	48 8d bd a0 fa ff ff 	lea    -0x560(%rbp),%rdi
  30c9c4:	e8 77 3a 00 00       	call   310440 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30c9c9:	c5 f9 6f 85 d0 fd ff 	vmovdqa -0x230(%rbp),%xmm0
  30c9d0:	ff 
  30c9d1:	c4 c1 7a 7f 04 24    	vmovdqu %xmm0,(%r12)
  30c9d7:	48 8b 85 e0 fd ff ff 	mov    -0x220(%rbp),%rax
  30c9de:	49 89 44 24 10       	mov    %rax,0x10(%r12)
  30c9e3:	41 c6 44 24 18 01    	movb   $0x1,0x18(%r12)
  30c9e9:	e9 fe 02 00 00       	jmp    30ccec <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1aac>
  30c9ee:	4c 89 f7             	mov    %r14,%rdi
  30c9f1:	c5 f8 77             	vzeroupper
  30c9f4:	e8 67 e1 fa ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30c9f9:	4d 8b 37             	mov    (%r15),%r14
  30c9fc:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30ca03:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30ca0a:	c4 e2 f3 f6 cb       	mulx   %rbx,%rcx,%rcx
  30ca0f:	48 c1 e9 07          	shr    $0x7,%rcx
  30ca13:	48 8b 04 c8          	mov    (%rax,%rcx,8),%rax
  30ca17:	48 69 c9 aa 00 00 00 	imul   $0xaa,%rcx,%rcx
  30ca1e:	48 89 d6             	mov    %rdx,%rsi
  30ca21:	48 29 ce             	sub    %rcx,%rsi
  30ca24:	48 8d 0c 76          	lea    (%rsi,%rsi,2),%rcx
  30ca28:	48 8b 3c c8          	mov    (%rax,%rcx,8),%rdi
  30ca2c:	48 85 ff             	test   %rdi,%rdi
  30ca2f:	74 1b                	je     30ca4c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x180c>
  30ca31:	48 8d 04 c8          	lea    (%rax,%rcx,8),%rax
  30ca35:	48 89 78 08          	mov    %rdi,0x8(%rax)
  30ca39:	48 8b 70 10          	mov    0x10(%rax),%rsi
  30ca3d:	48 29 fe             	sub    %rdi,%rsi
  30ca40:	e8 7b df dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30ca45:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30ca4c:	49 ff 8e 68 08 00 00 	decq   0x868(%r14)
  30ca53:	48 ff c2             	inc    %rdx
  30ca56:	49 89 96 60 08 00 00 	mov    %rdx,0x860(%r14)
  30ca5d:	48 81 fa 54 01 00 00 	cmp    $0x154,%rdx
  30ca64:	72 27                	jb     30ca8d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x184d>
  30ca66:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30ca6d:	48 8b 38             	mov    (%rax),%rdi
  30ca70:	be f0 0f 00 00       	mov    $0xff0,%esi
  30ca75:	e8 46 df dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30ca7a:	49 83 86 48 08 00 00 	addq   $0x8,0x848(%r14)
  30ca81:	08 
  30ca82:	49 81 86 60 08 00 00 	addq   $0xffffffffffffff56,0x860(%r14)
  30ca89:	56 ff ff ff 
  30ca8d:	49 8b 07             	mov    (%r15),%rax
  30ca90:	41 b5 01             	mov    $0x1,%r13b
  30ca93:	48 83 b8 68 08 00 00 	cmpq   $0x0,0x868(%rax)
  30ca9a:	00 
  30ca9b:	0f 84 8d 01 00 00    	je     30cc2e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x19ee>
  30caa1:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30caa8:	eb 18                	jmp    30cac2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1882>
  30caaa:	49 8b 07             	mov    (%r15),%rax
  30caad:	48 83 b8 68 08 00 00 	cmpq   $0x0,0x868(%rax)
  30cab4:	00 
  30cab5:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30cabc:	0f 84 37 f8 ff ff    	je     30c2f9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10b9>
  30cac2:	48 8b 88 48 08 00 00 	mov    0x848(%rax),%rcx
  30cac9:	48 8b 90 60 08 00 00 	mov    0x860(%rax),%rdx
  30cad0:	c4 e2 fb f6 c3       	mulx   %rbx,%rax,%rax
  30cad5:	48 c1 e8 07          	shr    $0x7,%rax
  30cad9:	48 8b 0c c1          	mov    (%rcx,%rax,8),%rcx
  30cadd:	48 69 c0 aa 00 00 00 	imul   $0xaa,%rax,%rax
  30cae4:	48 29 c2             	sub    %rax,%rdx
  30cae7:	49 8b 76 08          	mov    0x8(%r14),%rsi
  30caeb:	48 89 f0             	mov    %rsi,%rax
  30caee:	49 2b 06             	sub    (%r14),%rax
  30caf1:	48 8d 3c 52          	lea    (%rdx,%rdx,2),%rdi
  30caf5:	48 8b 14 f9          	mov    (%rcx,%rdi,8),%rdx
  30caf9:	48 8b 4c f9 08       	mov    0x8(%rcx,%rdi,8),%rcx
  30cafe:	49 89 c8             	mov    %rcx,%r8
  30cb01:	49 29 d0             	sub    %rdx,%r8
  30cb04:	4c 01 c0             	add    %r8,%rax
  30cb07:	48 3b 85 a0 f9 ff ff 	cmp    -0x660(%rbp),%rax
  30cb0e:	0f 87 e5 f7 ff ff    	ja     30c2f9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10b9>
  30cb14:	4c 89 f7             	mov    %r14,%rdi
  30cb17:	e8 44 e0 fa ff       	call   2bab60 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30cb1c:	4d 8b 37             	mov    (%r15),%r14
  30cb1f:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30cb26:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30cb2d:	c4 e2 f3 f6 cb       	mulx   %rbx,%rcx,%rcx
  30cb32:	48 c1 e9 07          	shr    $0x7,%rcx
  30cb36:	48 8b 04 c8          	mov    (%rax,%rcx,8),%rax
  30cb3a:	48 69 c9 aa 00 00 00 	imul   $0xaa,%rcx,%rcx
  30cb41:	48 89 d6             	mov    %rdx,%rsi
  30cb44:	48 29 ce             	sub    %rcx,%rsi
  30cb47:	48 8d 0c 76          	lea    (%rsi,%rsi,2),%rcx
  30cb4b:	48 8b 3c c8          	mov    (%rax,%rcx,8),%rdi
  30cb4f:	48 85 ff             	test   %rdi,%rdi
  30cb52:	74 1b                	je     30cb6f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x192f>
  30cb54:	48 8d 04 c8          	lea    (%rax,%rcx,8),%rax
  30cb58:	48 89 78 08          	mov    %rdi,0x8(%rax)
  30cb5c:	48 8b 70 10          	mov    0x10(%rax),%rsi
  30cb60:	48 29 fe             	sub    %rdi,%rsi
  30cb63:	e8 58 de dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30cb68:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30cb6f:	49 ff 8e 68 08 00 00 	decq   0x868(%r14)
  30cb76:	48 ff c2             	inc    %rdx
  30cb79:	49 89 96 60 08 00 00 	mov    %rdx,0x860(%r14)
  30cb80:	48 81 fa 54 01 00 00 	cmp    $0x154,%rdx
  30cb87:	0f 82 1d ff ff ff    	jb     30caaa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x186a>
  30cb8d:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30cb94:	48 8b 38             	mov    (%rax),%rdi
  30cb97:	be f0 0f 00 00       	mov    $0xff0,%esi
  30cb9c:	e8 1f de dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30cba1:	49 83 86 48 08 00 00 	addq   $0x8,0x848(%r14)
  30cba8:	08 
  30cba9:	49 81 86 60 08 00 00 	addq   $0xffffffffffffff56,0x860(%r14)
  30cbb0:	56 ff ff ff 
  30cbb4:	e9 f1 fe ff ff       	jmp    30caaa <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x186a>
  30cbb9:	8b 9d 40 fd ff ff    	mov    -0x2c0(%rbp),%ebx
  30cbbf:	85 db                	test   %ebx,%ebx
  30cbc1:	74 77                	je     30cc3a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x19fa>
  30cbc3:	8b 8d 44 fd ff ff    	mov    -0x2bc(%rbp),%ecx
  30cbc9:	48 8b 85 48 fd ff ff 	mov    -0x2b8(%rbp),%rax
  30cbd0:	eb 72                	jmp    30cc44 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a04>
  30cbd2:	84 c0                	test   %al,%al
  30cbd4:	74 23                	je     30cbf9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x19b9>
  30cbd6:	4c 8b 75 c0          	mov    -0x40(%rbp),%r14
  30cbda:	4c 2b 75 b8          	sub    -0x48(%rbp),%r14
  30cbde:	49 c1 fe 03          	sar    $0x3,%r14
  30cbe2:	48 b8 ef ee ee ee ee 	movabs $0xeeeeeeeeeeeeeeef,%rax
  30cbe9:	ee ee ee 
  30cbec:	4c 0f af f0          	imul   %rax,%r14
  30cbf0:	49 ff c6             	inc    %r14
  30cbf3:	0f 85 76 01 00 00    	jne    30cd6f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b2f>
  30cbf9:	48 8b 8d 78 f9 ff ff 	mov    -0x688(%rbp),%rcx
  30cc00:	c5 fa 6f 01          	vmovdqu (%rcx),%xmm0
  30cc04:	c4 c1 7a 7f 04 24    	vmovdqu %xmm0,(%r12)
  30cc0a:	48 8b 41 10          	mov    0x10(%rcx),%rax
  30cc0e:	49 89 44 24 10       	mov    %rax,0x10(%r12)
  30cc13:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30cc17:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30cc1b:	48 c7 41 10 00 00 00 	movq   $0x0,0x10(%rcx)
  30cc22:	00 
  30cc23:	41 c6 44 24 18 01    	movb   $0x1,0x18(%r12)
  30cc29:	e9 be 00 00 00       	jmp    30ccec <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1aac>
  30cc2e:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30cc35:	e9 bf f6 ff ff       	jmp    30c2f9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10b9>
  30cc3a:	bb 05 00 00 00       	mov    $0x5,%ebx
  30cc3f:	e8 9c db dd ff       	call   ea7e0 <_ZNSt3__116generic_categoryEv$plt>
  30cc44:	41 89 1c 24          	mov    %ebx,(%r12)
  30cc48:	41 89 4c 24 04       	mov    %ecx,0x4(%r12)
  30cc4d:	49 89 44 24 08       	mov    %rax,0x8(%r12)
  30cc52:	41 c6 44 24 18 00    	movb   $0x0,0x18(%r12)
  30cc58:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30cc5f:	48 85 ff             	test   %rdi,%rdi
  30cc62:	0f 84 84 00 00 00    	je     30ccec <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1aac>
  30cc68:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30cc6f:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30cc76:	48 29 fe             	sub    %rdi,%rsi
  30cc79:	e8 42 dd dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30cc7e:	eb 6c                	jmp    30ccec <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1aac>
  30cc80:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
  30cc87:	48 3b 85 48 ff ff ff 	cmp    -0xb8(%rbp),%rax
  30cc8e:	75 07                	jne    30cc97 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a57>
  30cc90:	48 83 7d 88 00       	cmpq   $0x0,-0x78(%rbp)
  30cc95:	74 23                	je     30ccba <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a7a>
  30cc97:	4c 8b 75 c0          	mov    -0x40(%rbp),%r14
  30cc9b:	4c 2b 75 b8          	sub    -0x48(%rbp),%r14
  30cc9f:	49 c1 fe 03          	sar    $0x3,%r14
  30cca3:	48 b8 ef ee ee ee ee 	movabs $0xeeeeeeeeeeeeeeef,%rax
  30ccaa:	ee ee ee 
  30ccad:	4c 0f af f0          	imul   %rax,%r14
  30ccb1:	49 ff c6             	inc    %r14
  30ccb4:	0f 85 f8 03 00 00    	jne    30d0b2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e72>
  30ccba:	48 8b 95 78 f9 ff ff 	mov    -0x688(%rbp),%rdx
  30ccc1:	c5 fa 6f 02          	vmovdqu (%rdx),%xmm0
  30ccc5:	48 8b 8d 88 f9 ff ff 	mov    -0x678(%rbp),%rcx
  30cccc:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30ccd0:	48 8b 42 10          	mov    0x10(%rdx),%rax
  30ccd4:	48 89 41 10          	mov    %rax,0x10(%rcx)
  30ccd8:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30ccdc:	c5 fa 7f 02          	vmovdqu %xmm0,(%rdx)
  30cce0:	48 c7 42 10 00 00 00 	movq   $0x0,0x10(%rdx)
  30cce7:	00 
  30cce8:	c6 41 18 01          	movb   $0x1,0x18(%rcx)
  30ccec:	48 8b bd b0 f9 ff ff 	mov    -0x650(%rbp),%rdi
  30ccf3:	48 85 ff             	test   %rdi,%rdi
  30ccf6:	74 19                	je     30cd11 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1ad1>
  30ccf8:	48 89 bd b8 f9 ff ff 	mov    %rdi,-0x648(%rbp)
  30ccff:	48 8b b5 c0 f9 ff ff 	mov    -0x640(%rbp),%rsi
  30cd06:	48 29 fe             	sub    %rdi,%rsi
  30cd09:	c5 f8 77             	vzeroupper
  30cd0c:	e8 af dc dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30cd11:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30cd18:	c5 f8 77             	vzeroupper
  30cd1b:	e8 20 37 00 00       	call   310440 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30cd20:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30cd27:	75 1b                	jne    30cd44 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b04>
  30cd29:	48 8b bd 50 fa ff ff 	mov    -0x5b0(%rbp),%rdi
  30cd30:	48 85 ff             	test   %rdi,%rdi
  30cd33:	74 0f                	je     30cd44 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b04>
  30cd35:	48 8b b5 60 fa ff ff 	mov    -0x5a0(%rbp),%rsi
  30cd3c:	48 29 fe             	sub    %rdi,%rsi
  30cd3f:	e8 7c dc dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30cd44:	48 8b bd d0 f9 ff ff 	mov    -0x630(%rbp),%rdi
  30cd4b:	48 85 ff             	test   %rdi,%rdi
  30cd4e:	0f 84 5f e6 ff ff    	je     30b3b3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x173>
  30cd54:	48 89 bd d8 f9 ff ff 	mov    %rdi,-0x628(%rbp)
  30cd5b:	48 8b b5 e0 f9 ff ff 	mov    -0x620(%rbp),%rsi
  30cd62:	48 29 fe             	sub    %rdi,%rsi
  30cd65:	e8 56 dc dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30cd6a:	e9 44 e6 ff ff       	jmp    30b3b3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x173>
  30cd6f:	4c 8d bd f0 fb ff ff 	lea    -0x410(%rbp),%r15
  30cd76:	4c 8b ad 10 fa ff ff 	mov    -0x5f0(%rbp),%r13
  30cd7d:	e9 e4 00 00 00       	jmp    30ce66 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c26>
  30cd82:	48 8d 4b 90          	lea    -0x70(%rbx),%rcx
  30cd86:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30cd8a:	c5 fa 7f 40 88       	vmovdqu %xmm0,-0x78(%rax)
  30cd8f:	48 8d 53 98          	lea    -0x68(%rbx),%rdx
  30cd93:	48 c7 40 98 00 00 00 	movq   $0x0,-0x68(%rax)
  30cd9a:	00 
  30cd9b:	49 8b 34 24          	mov    (%r12),%rsi
  30cd9f:	48 89 70 88          	mov    %rsi,-0x78(%rax)
  30cda3:	48 8d 73 a0          	lea    -0x60(%rbx),%rsi
  30cda7:	48 8d 7b a8          	lea    -0x58(%rbx),%rdi
  30cdab:	48 83 c3 b0          	add    $0xffffffffffffffb0,%rbx
  30cdaf:	4d 85 f6             	test   %r14,%r14
  30cdb2:	4c 8d 85 48 ff ff ff 	lea    -0xb8(%rbp),%r8
  30cdb9:	49 0f 44 c8          	cmove  %r8,%rcx
  30cdbd:	48 8b 09             	mov    (%rcx),%rcx
  30cdc0:	48 89 48 90          	mov    %rcx,-0x70(%rax)
  30cdc4:	48 8d 8d 50 ff ff ff 	lea    -0xb0(%rbp),%rcx
  30cdcb:	48 0f 44 d1          	cmove  %rcx,%rdx
  30cdcf:	48 8b 0a             	mov    (%rdx),%rcx
  30cdd2:	48 89 48 98          	mov    %rcx,-0x68(%rax)
  30cdd6:	c4 c1 7a 7f 04 24    	vmovdqu %xmm0,(%r12)
  30cddc:	49 c7 44 24 10 00 00 	movq   $0x0,0x10(%r12)
  30cde3:	00 00 
  30cde5:	48 8d 8d 58 ff ff ff 	lea    -0xa8(%rbp),%rcx
  30cdec:	48 0f 44 f1          	cmove  %rcx,%rsi
  30cdf0:	48 8b 0e             	mov    (%rsi),%rcx
  30cdf3:	48 89 48 a0          	mov    %rcx,-0x60(%rax)
  30cdf7:	48 8d 8d 60 ff ff ff 	lea    -0xa0(%rbp),%rcx
  30cdfe:	48 0f 44 f9          	cmove  %rcx,%rdi
  30ce02:	48 8b 0f             	mov    (%rdi),%rcx
  30ce05:	48 89 48 a8          	mov    %rcx,-0x58(%rax)
  30ce09:	c5 fa 7f 06          	vmovdqu %xmm0,(%rsi)
  30ce0d:	48 8d 8d 68 ff ff ff 	lea    -0x98(%rbp),%rcx
  30ce14:	48 0f 44 d9          	cmove  %rcx,%rbx
  30ce18:	c5 fc 10 03          	vmovups (%rbx),%ymm0
  30ce1c:	c5 fc 10 4b 20       	vmovups 0x20(%rbx),%ymm1
  30ce21:	c5 fc 10 53 2b       	vmovups 0x2b(%rbx),%ymm2
  30ce26:	c5 fc 11 50 db       	vmovups %ymm2,-0x25(%rax)
  30ce2b:	c5 fc 11 48 d0       	vmovups %ymm1,-0x30(%rax)
  30ce30:	c5 fc 11 40 b0       	vmovups %ymm0,-0x50(%rax)
  30ce35:	c5 f9 6f 05 93 c1 dc 	vmovdqa -0x233e6d(%rip),%xmm0        # d8fd0 <typeinfo name for cnetmod::io_uring_context@cnetmod.io.platform.io_uring+0xbc2>
  30ce3c:	ff 
  30ce3d:	c4 c1 79 d4 85 e8 06 	vpaddq 0x6e8(%r13),%xmm0,%xmm0
  30ce44:	00 00 
  30ce46:	c4 c1 7a 7f 85 e8 06 	vmovdqu %xmm0,0x6e8(%r13)
  30ce4d:	00 00 
  30ce4f:	4c 8b ad 10 fa ff ff 	mov    -0x5f0(%rbp),%r13
  30ce56:	4c 8b a5 88 f9 ff ff 	mov    -0x678(%rbp),%r12
  30ce5d:	4d 85 f6             	test   %r14,%r14
  30ce60:	0f 84 93 fd ff ff    	je     30cbf9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x19b9>
  30ce66:	49 ff ce             	dec    %r14
  30ce69:	48 8b 45 b8          	mov    -0x48(%rbp),%rax
  30ce6d:	49 6b ce 78          	imul   $0x78,%r14,%rcx
  30ce71:	48 8d 1c 08          	lea    (%rax,%rcx,1),%rbx
  30ce75:	4d 85 f6             	test   %r14,%r14
  30ce78:	4c 8d 64 08 88       	lea    -0x78(%rax,%rcx,1),%r12
  30ce7d:	48 8d 85 40 ff ff ff 	lea    -0xc0(%rbp),%rax
  30ce84:	4c 0f 44 e0          	cmove  %rax,%r12
  30ce88:	80 bd 28 fa ff ff 01 	cmpb   $0x1,-0x5d8(%rbp)
  30ce8f:	75 0c                	jne    30ce9d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c5d>
  30ce91:	48 8d 85 18 fa ff ff 	lea    -0x5e8(%rbp),%rax
  30ce98:	4d 39 f5             	cmp    %r14,%r13
  30ce9b:	74 33                	je     30ced0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c90>
  30ce9d:	48 8b 85 b0 f9 ff ff 	mov    -0x650(%rbp),%rax
  30cea4:	48 8b 8d b8 f9 ff ff 	mov    -0x648(%rbp),%rcx
  30ceab:	48 39 c8             	cmp    %rcx,%rax
  30ceae:	74 13                	je     30cec3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c83>
  30ceb0:	4c 39 30             	cmp    %r14,(%rax)
  30ceb3:	74 0e                	je     30cec3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c83>
  30ceb5:	48 83 c0 18          	add    $0x18,%rax
  30ceb9:	48 39 c8             	cmp    %rcx,%rax
  30cebc:	75 f2                	jne    30ceb0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c70>
  30cebe:	e9 5c 01 00 00       	jmp    30d01f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1ddf>
  30cec3:	48 39 c8             	cmp    %rcx,%rax
  30cec6:	0f 84 53 01 00 00    	je     30d01f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1ddf>
  30cecc:	48 83 c0 08          	add    $0x8,%rax
  30ced0:	48 8d 4b 90          	lea    -0x70(%rbx),%rcx
  30ced4:	48 8b 95 98 f9 ff ff 	mov    -0x668(%rbp),%rdx
  30cedb:	48 8b 3a             	mov    (%rdx),%rdi
  30cede:	48 8b 10             	mov    (%rax),%rdx
  30cee1:	48 89 95 b8 fb ff ff 	mov    %rdx,-0x448(%rbp)
  30cee8:	0f b6 50 08          	movzbl 0x8(%rax),%edx
  30ceec:	88 95 c0 fb ff ff    	mov    %dl,-0x440(%rbp)
  30cef2:	0f b6 40 09          	movzbl 0x9(%rax),%eax
  30cef6:	48 8d 53 98          	lea    -0x68(%rbx),%rdx
  30cefa:	88 85 c1 fb ff ff    	mov    %al,-0x43f(%rbp)
  30cf00:	49 8b 04 24          	mov    (%r12),%rax
  30cf04:	48 89 85 c8 fb ff ff 	mov    %rax,-0x438(%rbp)
  30cf0b:	48 8d 43 a0          	lea    -0x60(%rbx),%rax
  30cf0f:	48 8d 73 a8          	lea    -0x58(%rbx),%rsi
  30cf13:	48 83 c3 b0          	add    $0xffffffffffffffb0,%rbx
  30cf17:	4d 85 f6             	test   %r14,%r14
  30cf1a:	4c 8d 85 48 ff ff ff 	lea    -0xb8(%rbp),%r8
  30cf21:	49 0f 44 c8          	cmove  %r8,%rcx
  30cf25:	48 8b 09             	mov    (%rcx),%rcx
  30cf28:	48 89 8d d0 fb ff ff 	mov    %rcx,-0x430(%rbp)
  30cf2f:	48 8d 8d 50 ff ff ff 	lea    -0xb0(%rbp),%rcx
  30cf36:	48 0f 44 d1          	cmove  %rcx,%rdx
  30cf3a:	48 8b 0a             	mov    (%rdx),%rcx
  30cf3d:	48 89 8d d8 fb ff ff 	mov    %rcx,-0x428(%rbp)
  30cf44:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30cf48:	c4 c1 7a 7f 04 24    	vmovdqu %xmm0,(%r12)
  30cf4e:	49 c7 44 24 10 00 00 	movq   $0x0,0x10(%r12)
  30cf55:	00 00 
  30cf57:	48 8d 8d 58 ff ff ff 	lea    -0xa8(%rbp),%rcx
  30cf5e:	48 0f 44 c1          	cmove  %rcx,%rax
  30cf62:	48 8b 08             	mov    (%rax),%rcx
  30cf65:	48 89 8d e0 fb ff ff 	mov    %rcx,-0x420(%rbp)
  30cf6c:	48 8d 8d 60 ff ff ff 	lea    -0xa0(%rbp),%rcx
  30cf73:	48 0f 44 f1          	cmove  %rcx,%rsi
  30cf77:	48 8b 0e             	mov    (%rsi),%rcx
  30cf7a:	48 89 8d e8 fb ff ff 	mov    %rcx,-0x418(%rbp)
  30cf81:	c5 fa 7f 00          	vmovdqu %xmm0,(%rax)
  30cf85:	48 8d 85 68 ff ff ff 	lea    -0x98(%rbp),%rax
  30cf8c:	48 0f 44 d8          	cmove  %rax,%rbx
  30cf90:	c5 fe 6f 03          	vmovdqu (%rbx),%ymm0
  30cf94:	c5 fc 10 4b 20       	vmovups 0x20(%rbx),%ymm1
  30cf99:	c5 fc 10 53 2b       	vmovups 0x2b(%rbx),%ymm2
  30cf9e:	c4 c1 7c 11 57 2b    	vmovups %ymm2,0x2b(%r15)
  30cfa4:	c4 c1 7c 11 4f 20    	vmovups %ymm1,0x20(%r15)
  30cfaa:	c4 c1 7e 7f 07       	vmovdqu %ymm0,(%r15)
  30cfaf:	48 8d b5 b8 fb ff ff 	lea    -0x448(%rbp),%rsi
  30cfb6:	c5 f8 77             	vzeroupper
  30cfb9:	e8 42 2b ff ff       	call   2ffb00 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::requeue_prioritized_stream_frame(cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline)>
  30cfbe:	48 8b 9d e8 fb ff ff 	mov    -0x418(%rbp),%rbx
  30cfc5:	48 85 db             	test   %rbx,%rbx
  30cfc8:	4c 8b a5 88 f9 ff ff 	mov    -0x678(%rbp),%r12
  30cfcf:	74 23                	je     30cff4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1db4>
  30cfd1:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30cfd8:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30cfde:	48 85 c0             	test   %rax,%rax
  30cfe1:	75 11                	jne    30cff4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1db4>
  30cfe3:	48 8b 03             	mov    (%rbx),%rax
  30cfe6:	48 89 df             	mov    %rbx,%rdi
  30cfe9:	ff 50 10             	call   *0x10(%rax)
  30cfec:	48 89 df             	mov    %rbx,%rdi
  30cfef:	e8 2c d8 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30cff4:	48 8b bd c8 fb ff ff 	mov    -0x438(%rbp),%rdi
  30cffb:	48 85 ff             	test   %rdi,%rdi
  30cffe:	0f 84 59 fe ff ff    	je     30ce5d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c1d>
  30d004:	48 89 bd d0 fb ff ff 	mov    %rdi,-0x430(%rbp)
  30d00b:	48 8b b5 d8 fb ff ff 	mov    -0x428(%rbp),%rsi
  30d012:	48 29 fe             	sub    %rdi,%rsi
  30d015:	e8 a6 d9 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d01a:	e9 3e fe ff ff       	jmp    30ce5d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c1d>
  30d01f:	48 8b 85 98 f9 ff ff 	mov    -0x668(%rbp),%rax
  30d026:	4c 8b 28             	mov    (%rax),%r13
  30d029:	49 8b 95 e8 06 00 00 	mov    0x6e8(%r13),%rdx
  30d030:	48 85 d2             	test   %rdx,%rdx
  30d033:	75 16                	jne    30d04b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e0b>
  30d035:	49 8d bd c8 06 00 00 	lea    0x6c8(%r13),%rdi
  30d03c:	c5 f8 77             	vzeroupper
  30d03f:	e8 dc c8 00 00       	call   319920 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::__add_front_capacity()>
  30d044:	49 8b 95 e8 06 00 00 	mov    0x6e8(%r13),%rdx
  30d04b:	49 8b 8d d0 06 00 00 	mov    0x6d0(%r13),%rcx
  30d052:	48 b8 f1 f0 f0 f0 f0 	movabs $0xf0f0f0f0f0f0f0f1,%rax
  30d059:	f0 f0 f0 
  30d05c:	c4 e2 fb f6 c0       	mulx   %rax,%rax,%rax
  30d061:	48 89 c6             	mov    %rax,%rsi
  30d064:	48 c1 ee 05          	shr    $0x5,%rsi
  30d068:	48 bf e0 ff ff ff ff 	movabs $0x1fffffffffffffe0,%rdi
  30d06f:	ff ff 1f 
  30d072:	48 21 f8             	and    %rdi,%rax
  30d075:	48 8d 04 70          	lea    (%rax,%rsi,2),%rax
  30d079:	48 29 c2             	sub    %rax,%rdx
  30d07c:	48 8b 3c f1          	mov    (%rcx,%rsi,8),%rdi
  30d080:	48 6b c2 78          	imul   $0x78,%rdx,%rax
  30d084:	48 01 f8             	add    %rdi,%rax
  30d087:	49 3b 8d d8 06 00 00 	cmp    0x6d8(%r13),%rcx
  30d08e:	ba 00 00 00 00       	mov    $0x0,%edx
  30d093:	48 0f 44 c2          	cmove  %rdx,%rax
  30d097:	48 39 f8             	cmp    %rdi,%rax
  30d09a:	0f 85 e2 fc ff ff    	jne    30cd82 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b42>
  30d0a0:	48 8b 44 f1 f8       	mov    -0x8(%rcx,%rsi,8),%rax
  30d0a5:	b9 f0 0f 00 00       	mov    $0xff0,%ecx
  30d0aa:	48 01 c8             	add    %rcx,%rax
  30d0ad:	e9 d0 fc ff ff       	jmp    30cd82 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b42>
  30d0b2:	44 0f b6 a5 28 fa ff 	movzbl -0x5d8(%rbp),%r12d
  30d0b9:	ff 
  30d0ba:	4c 8b ad 10 fa ff ff 	mov    -0x5f0(%rbp),%r13
  30d0c1:	4c 8d bd 68 fb ff ff 	lea    -0x498(%rbp),%r15
  30d0c8:	eb 20                	jmp    30d0ea <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1eaa>
  30d0ca:	48 8b 85 98 f9 ff ff 	mov    -0x668(%rbp),%rax
  30d0d1:	48 8b 38             	mov    (%rax),%rdi
  30d0d4:	b8 c8 06 00 00       	mov    $0x6c8,%eax
  30d0d9:	48 01 c7             	add    %rax,%rdi
  30d0dc:	e8 bf 2b ff ff       	call   2ffca0 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::push_front(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline&&)>
  30d0e1:	4d 85 f6             	test   %r14,%r14
  30d0e4:	0f 84 d0 fb ff ff    	je     30ccba <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a7a>
  30d0ea:	49 ff ce             	dec    %r14
  30d0ed:	48 8b 45 b8          	mov    -0x48(%rbp),%rax
  30d0f1:	49 6b d6 78          	imul   $0x78,%r14,%rdx
  30d0f5:	4d 85 f6             	test   %r14,%r14
  30d0f8:	48 8d 74 10 88       	lea    -0x78(%rax,%rdx,1),%rsi
  30d0fd:	48 8d 8d 40 ff ff ff 	lea    -0xc0(%rbp),%rcx
  30d104:	48 0f 44 f1          	cmove  %rcx,%rsi
  30d108:	45 84 e4             	test   %r12b,%r12b
  30d10b:	74 0c                	je     30d119 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1ed9>
  30d10d:	48 8d 8d 18 fa ff ff 	lea    -0x5e8(%rbp),%rcx
  30d114:	4d 39 f5             	cmp    %r14,%r13
  30d117:	74 2c                	je     30d145 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f05>
  30d119:	48 8b 8d b0 f9 ff ff 	mov    -0x650(%rbp),%rcx
  30d120:	48 8b bd b8 f9 ff ff 	mov    -0x648(%rbp),%rdi
  30d127:	48 39 f9             	cmp    %rdi,%rcx
  30d12a:	74 10                	je     30d13c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1efc>
  30d12c:	4c 39 31             	cmp    %r14,(%rcx)
  30d12f:	74 0b                	je     30d13c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1efc>
  30d131:	48 83 c1 18          	add    $0x18,%rcx
  30d135:	48 39 f9             	cmp    %rdi,%rcx
  30d138:	75 f2                	jne    30d12c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1eec>
  30d13a:	eb 8e                	jmp    30d0ca <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e8a>
  30d13c:	48 39 f9             	cmp    %rdi,%rcx
  30d13f:	74 89                	je     30d0ca <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e8a>
  30d141:	48 83 c1 08          	add    $0x8,%rcx
  30d145:	48 01 d0             	add    %rdx,%rax
  30d148:	48 8d 50 90          	lea    -0x70(%rax),%rdx
  30d14c:	48 8b bd 98 f9 ff ff 	mov    -0x668(%rbp),%rdi
  30d153:	48 8b 3f             	mov    (%rdi),%rdi
  30d156:	4c 8b 01             	mov    (%rcx),%r8
  30d159:	4c 89 85 30 fb ff ff 	mov    %r8,-0x4d0(%rbp)
  30d160:	44 0f b6 41 08       	movzbl 0x8(%rcx),%r8d
  30d165:	44 88 85 38 fb ff ff 	mov    %r8b,-0x4c8(%rbp)
  30d16c:	0f b6 49 09          	movzbl 0x9(%rcx),%ecx
  30d170:	4c 8d 40 98          	lea    -0x68(%rax),%r8
  30d174:	88 8d 39 fb ff ff    	mov    %cl,-0x4c7(%rbp)
  30d17a:	48 8b 0e             	mov    (%rsi),%rcx
  30d17d:	48 89 8d 40 fb ff ff 	mov    %rcx,-0x4c0(%rbp)
  30d184:	48 8d 48 a0          	lea    -0x60(%rax),%rcx
  30d188:	4c 8d 48 a8          	lea    -0x58(%rax),%r9
  30d18c:	48 83 c0 b0          	add    $0xffffffffffffffb0,%rax
  30d190:	4d 85 f6             	test   %r14,%r14
  30d193:	4c 8d 95 48 ff ff ff 	lea    -0xb8(%rbp),%r10
  30d19a:	49 0f 44 d2          	cmove  %r10,%rdx
  30d19e:	48 8b 12             	mov    (%rdx),%rdx
  30d1a1:	48 89 95 48 fb ff ff 	mov    %rdx,-0x4b8(%rbp)
  30d1a8:	48 8d 95 50 ff ff ff 	lea    -0xb0(%rbp),%rdx
  30d1af:	4c 0f 44 c2          	cmove  %rdx,%r8
  30d1b3:	49 8b 10             	mov    (%r8),%rdx
  30d1b6:	48 89 95 50 fb ff ff 	mov    %rdx,-0x4b0(%rbp)
  30d1bd:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30d1c1:	c5 fa 7f 06          	vmovdqu %xmm0,(%rsi)
  30d1c5:	48 c7 46 10 00 00 00 	movq   $0x0,0x10(%rsi)
  30d1cc:	00 
  30d1cd:	48 8d 95 58 ff ff ff 	lea    -0xa8(%rbp),%rdx
  30d1d4:	48 0f 44 ca          	cmove  %rdx,%rcx
  30d1d8:	48 8b 11             	mov    (%rcx),%rdx
  30d1db:	48 89 95 58 fb ff ff 	mov    %rdx,-0x4a8(%rbp)
  30d1e2:	48 8d 95 60 ff ff ff 	lea    -0xa0(%rbp),%rdx
  30d1e9:	4c 0f 44 ca          	cmove  %rdx,%r9
  30d1ed:	49 8b 11             	mov    (%r9),%rdx
  30d1f0:	48 89 95 60 fb ff ff 	mov    %rdx,-0x4a0(%rbp)
  30d1f7:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30d1fb:	48 8d 8d 68 ff ff ff 	lea    -0x98(%rbp),%rcx
  30d202:	48 0f 44 c1          	cmove  %rcx,%rax
  30d206:	c5 fe 6f 00          	vmovdqu (%rax),%ymm0
  30d20a:	c5 fc 10 48 20       	vmovups 0x20(%rax),%ymm1
  30d20f:	c5 fc 10 50 2b       	vmovups 0x2b(%rax),%ymm2
  30d214:	c4 c1 7c 11 57 2b    	vmovups %ymm2,0x2b(%r15)
  30d21a:	c4 c1 7c 11 4f 20    	vmovups %ymm1,0x20(%r15)
  30d220:	c4 c1 7e 7f 07       	vmovdqu %ymm0,(%r15)
  30d225:	48 8d b5 30 fb ff ff 	lea    -0x4d0(%rbp),%rsi
  30d22c:	c5 f8 77             	vzeroupper
  30d22f:	e8 cc 28 ff ff       	call   2ffb00 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::requeue_prioritized_stream_frame(cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline)>
  30d234:	48 8b 9d 60 fb ff ff 	mov    -0x4a0(%rbp),%rbx
  30d23b:	48 85 db             	test   %rbx,%rbx
  30d23e:	74 23                	je     30d263 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2023>
  30d240:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30d247:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30d24d:	48 85 c0             	test   %rax,%rax
  30d250:	75 11                	jne    30d263 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2023>
  30d252:	48 8b 03             	mov    (%rbx),%rax
  30d255:	48 89 df             	mov    %rbx,%rdi
  30d258:	ff 50 10             	call   *0x10(%rax)
  30d25b:	48 89 df             	mov    %rbx,%rdi
  30d25e:	e8 bd d5 dd ff       	call   ea820 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30d263:	48 8b bd 40 fb ff ff 	mov    -0x4c0(%rbp),%rdi
  30d26a:	48 85 ff             	test   %rdi,%rdi
  30d26d:	0f 84 6e fe ff ff    	je     30d0e1 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1ea1>
  30d273:	48 89 bd 48 fb ff ff 	mov    %rdi,-0x4b8(%rbp)
  30d27a:	48 8b b5 50 fb ff ff 	mov    -0x4b0(%rbp),%rsi
  30d281:	48 29 fe             	sub    %rdi,%rsi
  30d284:	e8 37 d7 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d289:	e9 53 fe ff ff       	jmp    30d0e1 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1ea1>
  30d28e:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d295:	c5 f8 77             	vzeroupper
  30d298:	e8 63 9b fa ff       	call   2b6e00 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d29d:	4c 8b ad 90 fa ff ff 	mov    -0x570(%rbp),%r13
  30d2a4:	4c 8b a5 88 f9 ff ff 	mov    -0x678(%rbp),%r12
  30d2ab:	e9 44 f3 ff ff       	jmp    30c5f4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x13b4>
  30d2b0:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d2b7:	e8 44 9b fa ff       	call   2b6e00 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d2bc:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d2c3:	4c 8b a5 88 f9 ff ff 	mov    -0x678(%rbp),%r12
  30d2ca:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d2d1:	e9 a1 f3 ff ff       	jmp    30c677 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1437>
  30d2d6:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d2dd:	e8 1e 9b fa ff       	call   2b6e00 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d2e2:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d2e9:	4c 8b a5 88 f9 ff ff 	mov    -0x678(%rbp),%r12
  30d2f0:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d2f7:	e9 cb f3 ff ff       	jmp    30c6c7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1487>
  30d2fc:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d303:	e8 f8 9a fa ff       	call   2b6e00 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d308:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d30f:	4c 8b a5 88 f9 ff ff 	mov    -0x678(%rbp),%r12
  30d316:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d31d:	e9 f5 f3 ff ff       	jmp    30c717 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x14d7>
  30d322:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d329:	e8 d2 9a fa ff       	call   2b6e00 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d32e:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d335:	4c 8b a5 88 f9 ff ff 	mov    -0x678(%rbp),%r12
  30d33c:	48 8b 9d 48 f9 ff ff 	mov    -0x6b8(%rbp),%rbx
  30d343:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d34a:	e9 1a f4 ff ff       	jmp    30c769 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1529>
  30d34f:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30d356:	00 00 
  30d358:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30d35c:	0f 85 7c 02 00 00    	jne    30d5de <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x239e>
  30d362:	e8 89 9a fa ff       	call   2b6df0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__throw_length_error[abi:nqe220108]()>
  30d367:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30d36e:	00 00 
  30d370:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30d374:	0f 85 64 02 00 00    	jne    30d5de <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x239e>
  30d37a:	c5 f8 77             	vzeroupper
  30d37d:	e8 6e 9a fa ff       	call   2b6df0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__throw_length_error[abi:nqe220108]()>
  30d382:	e9 0e 01 00 00       	jmp    30d495 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2255>
  30d387:	e9 09 01 00 00       	jmp    30d495 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2255>
  30d38c:	49 89 c6             	mov    %rax,%r14
  30d38f:	48 8d bd a0 fa ff ff 	lea    -0x560(%rbp),%rdi
  30d396:	e8 a5 30 00 00       	call   310440 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30d39b:	e9 f8 00 00 00       	jmp    30d498 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2258>
  30d3a0:	49 89 c6             	mov    %rax,%r14
  30d3a3:	48 8d bd 50 fe ff ff 	lea    -0x1b0(%rbp),%rdi
  30d3aa:	e8 e1 26 ff ff       	call   2ffa90 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30d3af:	eb 03                	jmp    30d3b4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2174>
  30d3b1:	49 89 c6             	mov    %rax,%r14
  30d3b4:	48 8b bd 40 fd ff ff 	mov    -0x2c0(%rbp),%rdi
  30d3bb:	48 85 ff             	test   %rdi,%rdi
  30d3be:	74 1b                	je     30d3db <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x219b>
  30d3c0:	48 89 bd 48 fd ff ff 	mov    %rdi,-0x2b8(%rbp)
  30d3c7:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30d3ce:	48 29 fe             	sub    %rdi,%rsi
  30d3d1:	e8 ea d5 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d3d6:	eb 03                	jmp    30d3db <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x219b>
  30d3d8:	49 89 c6             	mov    %rax,%r14
  30d3db:	4d 85 ff             	test   %r15,%r15
  30d3de:	0f 84 6f 01 00 00    	je     30d553 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2313>
  30d3e4:	4d 29 fc             	sub    %r15,%r12
  30d3e7:	4c 89 ff             	mov    %r15,%rdi
  30d3ea:	4c 89 e6             	mov    %r12,%rsi
  30d3ed:	e8 ce d5 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d3f2:	e9 5c 01 00 00       	jmp    30d553 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2313>
  30d3f7:	e9 30 01 00 00       	jmp    30d52c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x22ec>
  30d3fc:	e9 2b 01 00 00       	jmp    30d52c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x22ec>
  30d401:	e9 26 01 00 00       	jmp    30d52c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x22ec>
  30d406:	49 89 c6             	mov    %rax,%r14
  30d409:	e9 97 01 00 00       	jmp    30d5a5 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2365>
  30d40e:	e9 82 00 00 00       	jmp    30d495 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2255>
  30d413:	49 89 c6             	mov    %rax,%r14
  30d416:	48 8b bd 68 ff ff ff 	mov    -0x98(%rbp),%rdi
  30d41d:	48 85 ff             	test   %rdi,%rdi
  30d420:	0f 84 64 01 00 00    	je     30d58a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x234a>
  30d426:	48 8b b5 78 ff ff ff 	mov    -0x88(%rbp),%rsi
  30d42d:	48 29 fe             	sub    %rdi,%rsi
  30d430:	e8 8b d5 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d435:	e9 50 01 00 00       	jmp    30d58a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x234a>
  30d43a:	49 89 c6             	mov    %rax,%r14
  30d43d:	e9 48 01 00 00       	jmp    30d58a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x234a>
  30d442:	49 89 c6             	mov    %rax,%r14
  30d445:	e9 40 01 00 00       	jmp    30d58a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x234a>
  30d44a:	e9 dd 00 00 00       	jmp    30d52c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x22ec>
  30d44f:	49 89 c6             	mov    %rax,%r14
  30d452:	48 8d bd c8 fe ff ff 	lea    -0x138(%rbp),%rdi
  30d459:	e9 f0 00 00 00       	jmp    30d54e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x230e>
  30d45e:	49 89 c6             	mov    %rax,%r14
  30d461:	48 85 db             	test   %rbx,%rbx
  30d464:	0f 84 e9 00 00 00    	je     30d553 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2313>
  30d46a:	49 29 dd             	sub    %rbx,%r13
  30d46d:	48 89 df             	mov    %rbx,%rdi
  30d470:	4c 89 ee             	mov    %r13,%rsi
  30d473:	e8 48 d5 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d478:	e9 d6 00 00 00       	jmp    30d553 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2313>
  30d47d:	49 89 c6             	mov    %rax,%r14
  30d480:	48 8d bd 30 fb ff ff 	lea    -0x4d0(%rbp),%rdi
  30d487:	e8 a4 27 ff ff       	call   2ffc30 <cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline::~scheduled_quic_frame()>
  30d48c:	e9 c2 00 00 00       	jmp    30d553 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2313>
  30d491:	eb 02                	jmp    30d495 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2255>
  30d493:	eb 00                	jmp    30d495 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2255>
  30d495:	49 89 c6             	mov    %rax,%r14
  30d498:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30d49f:	48 85 ff             	test   %rdi,%rdi
  30d4a2:	0f 84 ab 00 00 00    	je     30d553 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2313>
  30d4a8:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30d4af:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30d4b6:	48 29 fe             	sub    %rdi,%rsi
  30d4b9:	e8 02 d5 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d4be:	e9 90 00 00 00       	jmp    30d553 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2313>
  30d4c3:	49 89 c6             	mov    %rax,%r14
  30d4c6:	be 01 00 00 00       	mov    $0x1,%esi
  30d4cb:	4c 89 ef             	mov    %r13,%rdi
  30d4ce:	e8 ed d4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d4d3:	eb 7e                	jmp    30d553 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2313>
  30d4d5:	49 89 c6             	mov    %rax,%r14
  30d4d8:	4d 85 ed             	test   %r13,%r13
  30d4db:	0f 84 a9 00 00 00    	je     30d58a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x234a>
  30d4e1:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30d4e8:	4c 29 ee             	sub    %r13,%rsi
  30d4eb:	4c 89 ef             	mov    %r13,%rdi
  30d4ee:	e8 cd d4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d4f3:	e9 92 00 00 00       	jmp    30d58a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x234a>
  30d4f8:	eb 32                	jmp    30d52c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x22ec>
  30d4fa:	49 89 c6             	mov    %rax,%r14
  30d4fd:	48 8d bd b8 fb ff ff 	lea    -0x448(%rbp),%rdi
  30d504:	e8 27 27 ff ff       	call   2ffc30 <cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline::~scheduled_quic_frame()>
  30d509:	eb 48                	jmp    30d553 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2313>
  30d50b:	49 89 c6             	mov    %rax,%r14
  30d50e:	e9 92 00 00 00       	jmp    30d5a5 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2365>
  30d513:	49 89 c6             	mov    %rax,%r14
  30d516:	e9 8a 00 00 00       	jmp    30d5a5 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2365>
  30d51b:	49 89 c6             	mov    %rax,%r14
  30d51e:	48 8d bd b8 fc ff ff 	lea    -0x348(%rbp),%rdi
  30d525:	e8 06 27 ff ff       	call   2ffc30 <cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline::~scheduled_quic_frame()>
  30d52a:	eb 1b                	jmp    30d547 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2307>
  30d52c:	49 89 c6             	mov    %rax,%r14
  30d52f:	eb 22                	jmp    30d553 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2313>
  30d531:	eb 11                	jmp    30d544 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2304>
  30d533:	49 89 c6             	mov    %rax,%r14
  30d536:	48 8d bd 40 fc ff ff 	lea    -0x3c0(%rbp),%rdi
  30d53d:	e8 4e 25 ff ff       	call   2ffa90 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30d542:	eb 03                	jmp    30d547 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2307>
  30d544:	49 89 c6             	mov    %rax,%r14
  30d547:	48 8d bd d0 fd ff ff 	lea    -0x230(%rbp),%rdi
  30d54e:	e8 3d 25 ff ff       	call   2ffa90 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30d553:	48 8b bd b0 f9 ff ff 	mov    -0x650(%rbp),%rdi
  30d55a:	48 85 ff             	test   %rdi,%rdi
  30d55d:	74 16                	je     30d575 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2335>
  30d55f:	48 89 bd b8 f9 ff ff 	mov    %rdi,-0x648(%rbp)
  30d566:	48 8b b5 c0 f9 ff ff 	mov    -0x640(%rbp),%rsi
  30d56d:	48 29 fe             	sub    %rdi,%rsi
  30d570:	e8 4b d4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d575:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30d57c:	e8 bf 2e 00 00       	call   310440 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30d581:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30d588:	75 1b                	jne    30d5a5 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2365>
  30d58a:	48 8b bd 50 fa ff ff 	mov    -0x5b0(%rbp),%rdi
  30d591:	48 85 ff             	test   %rdi,%rdi
  30d594:	74 0f                	je     30d5a5 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2365>
  30d596:	48 8b b5 60 fa ff ff 	mov    -0x5a0(%rbp),%rsi
  30d59d:	48 29 fe             	sub    %rdi,%rsi
  30d5a0:	e8 1b d4 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d5a5:	48 8b bd d0 f9 ff ff 	mov    -0x630(%rbp),%rdi
  30d5ac:	48 85 ff             	test   %rdi,%rdi
  30d5af:	74 16                	je     30d5c7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2387>
  30d5b1:	48 89 bd d8 f9 ff ff 	mov    %rdi,-0x628(%rbp)
  30d5b8:	48 8b b5 e0 f9 ff ff 	mov    -0x620(%rbp),%rsi
  30d5bf:	48 29 fe             	sub    %rdi,%rsi
  30d5c2:	e8 f9 d3 dd ff       	call   ea9c0 <_ZdlPvm$plt>
  30d5c7:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30d5ce:	00 00 
  30d5d0:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30d5d4:	75 08                	jne    30d5de <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x239e>
  30d5d6:	4c 89 f7             	mov    %r14,%rdi
  30d5d9:	e8 b2 d4 dd ff       	call   eaa90 <_Unwind_Resume$plt>
  30d5de:	c5 f8 77             	vzeroupper
  30d5e1:	e8 4a d5 dd ff       	call   eab30 <__stack_chk_fail$plt>
  30d5e6:	cc                   	int3
  30d5e7:	cc                   	int3
  30d5e8:	cc                   	int3
  30d5e9:	cc                   	int3
  30d5ea:	cc                   	int3
  30d5eb:	cc                   	int3
  30d5ec:	cc                   	int3
  30d5ed:	cc                   	int3
  30d5ee:	cc                   	int3
  30d5ef:	cc                   	int3
