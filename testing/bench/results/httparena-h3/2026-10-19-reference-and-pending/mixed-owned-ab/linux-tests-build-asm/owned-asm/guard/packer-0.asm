
/var/tmp/cnetmod-pmtu-deadline-5afc506d/baseline-guard/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

000000000030b1f0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)>:
  30b1f0:	55                   	push   %rbp
  30b1f1:	48 89 e5             	mov    %rsp,%rbp
  30b1f4:	41 57                	push   %r15
  30b1f6:	41 56                	push   %r14
  30b1f8:	41 55                	push   %r13
  30b1fa:	41 54                	push   %r12
  30b1fc:	53                   	push   %rbx
  30b1fd:	48 81 ec b8 06 00 00 	sub    $0x6b8,%rsp
  30b204:	48 89 8d 78 f9 ff ff 	mov    %rcx,-0x688(%rbp)
  30b20b:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30b212:	00 00 
  30b214:	48 89 45 d0          	mov    %rax,-0x30(%rbp)
  30b218:	48 8b 0e             	mov    (%rsi),%rcx
  30b21b:	48 8b 81 58 03 00 00 	mov    0x358(%rcx),%rax
  30b222:	48 85 c0             	test   %rax,%rax
  30b225:	0f 84 2f 01 00 00    	je     30b35a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b22b:	49 89 f4             	mov    %rsi,%r12
  30b22e:	4c 8d 98 00 04 00 00 	lea    0x400(%rax),%r11
  30b235:	48 8b b0 08 04 00 00 	mov    0x408(%rax),%rsi
  30b23c:	45 31 f6             	xor    %r14d,%r14d
  30b23f:	48 3b b0 10 04 00 00 	cmp    0x410(%rax),%rsi
  30b246:	4d 0f 45 f3          	cmovne %r11,%r14
  30b24a:	0f 84 0a 01 00 00    	je     30b35a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b250:	4c 8b 89 80 0e 00 00 	mov    0xe80(%rcx),%r9
  30b257:	4d 85 c9             	test   %r9,%r9
  30b25a:	74 2d                	je     30b289 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x99>
  30b25c:	8b b1 a8 0e 00 00    	mov    0xea8(%rcx),%esi
  30b262:	eb 14                	jmp    30b278 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x88>
  30b264:	66 66 66 2e 0f 1f 84 	data16 data16 cs nopw 0x0(%rax,%rax,1)
  30b26b:	00 00 00 00 00 
  30b270:	4d 8b 09             	mov    (%r9),%r9
  30b273:	4d 85 c9             	test   %r9,%r9
  30b276:	74 11                	je     30b289 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x99>
  30b278:	41 3b 71 20          	cmp    0x20(%r9),%esi
  30b27c:	72 f2                	jb     30b270 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x80>
  30b27e:	74 28                	je     30b2a8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xb8>
  30b280:	4d 8b 49 08          	mov    0x8(%r9),%r9
  30b284:	4d 85 c9             	test   %r9,%r9
  30b287:	75 ef                	jne    30b278 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x88>
  30b289:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30b290:	00 00 
  30b292:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30b296:	0f 85 32 23 00 00    	jne    30d5ce <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23de>
  30b29c:	48 8d 3d ce 1d dd ff 	lea    -0x22e232(%rip),%rdi        # dd071 <cnetmod::quic::max_udp_receive_payload@cnetmod.protocol.quic+0x3181>
  30b2a3:	e8 58 ae fa ff       	call   2b6100 <std::__1::__throw_out_of_range[abi:nqe220108](char const*)>
  30b2a8:	4d 8b 51 28          	mov    0x28(%r9),%r10
  30b2ac:	4d 8d 4a 32          	lea    0x32(%r10),%r9
  30b2b0:	41 0f b6 72 47       	movzbl 0x47(%r10),%esi
  30b2b5:	4c 89 cb             	mov    %r9,%rbx
  30b2b8:	40 84 f6             	test   %sil,%sil
  30b2bb:	74 79                	je     30b336 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x146>
  30b2bd:	41 80 ba 83 00 00 00 	cmpb   $0x0,0x83(%r10)
  30b2c4:	00 
  30b2c5:	0f 85 8f 00 00 00    	jne    30b35a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b2cb:	41 80 ba 84 00 00 00 	cmpb   $0x1,0x84(%r10)
  30b2d2:	01 
  30b2d3:	0f 84 81 00 00 00    	je     30b35a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b2d9:	4c 89 b5 50 f9 ff ff 	mov    %r14,-0x6b0(%rbp)
  30b2e0:	48 89 9d 68 f9 ff ff 	mov    %rbx,-0x698(%rbp)
  30b2e7:	89 95 74 f9 ff ff    	mov    %edx,-0x68c(%rbp)
  30b2ed:	48 89 bd 90 f9 ff ff 	mov    %rdi,-0x670(%rbp)
  30b2f4:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b2f8:	c5 f9 7f 85 d0 f9 ff 	vmovdqa %xmm0,-0x630(%rbp)
  30b2ff:	ff 
  30b300:	48 c7 85 e0 f9 ff ff 	movq   $0x0,-0x620(%rbp)
  30b307:	00 00 00 00 
  30b30b:	4d 85 c0             	test   %r8,%r8
  30b30e:	4c 8d b5 d0 f9 ff ff 	lea    -0x630(%rbp),%r14
  30b315:	4c 89 c7             	mov    %r8,%rdi
  30b318:	49 0f 44 fe          	cmove  %r14,%rdi
  30b31c:	48 8b 1f             	mov    (%rdi),%rbx
  30b31f:	48 89 5f 08          	mov    %rbx,0x8(%rdi)
  30b323:	40 84 f6             	test   %sil,%sil
  30b326:	74 6b                	je     30b393 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a3>
  30b328:	41 0f b6 51 14       	movzbl 0x14(%r9),%edx
  30b32d:	48 83 c2 05          	add    $0x5,%rdx
  30b331:	e9 80 00 00 00       	jmp    30b3b6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c6>
  30b336:	41 83 3a 00          	cmpl   $0x0,(%r10)
  30b33a:	75 1e                	jne    30b35a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b33c:	80 b9 8f 01 00 00 01 	cmpb   $0x1,0x18f(%rcx)
  30b343:	75 15                	jne    30b35a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x16a>
  30b345:	48 8d 99 7a 01 00 00 	lea    0x17a(%rcx),%rbx
  30b34c:	41 80 ba 83 00 00 00 	cmpb   $0x0,0x83(%r10)
  30b353:	00 
  30b354:	0f 84 71 ff ff ff    	je     30b2cb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xdb>
  30b35a:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b35e:	c5 fa 7f 07          	vmovdqu %xmm0,(%rdi)
  30b362:	48 c7 47 10 00 00 00 	movq   $0x0,0x10(%rdi)
  30b369:	00 
  30b36a:	c6 47 18 01          	movb   $0x1,0x18(%rdi)
  30b36e:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30b375:	00 00 
  30b377:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30b37b:	0f 85 4d 22 00 00    	jne    30d5ce <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23de>
  30b381:	48 81 c4 b8 06 00 00 	add    $0x6b8,%rsp
  30b388:	5b                   	pop    %rbx
  30b389:	41 5c                	pop    %r12
  30b38b:	41 5d                	pop    %r13
  30b38d:	41 5e                	pop    %r14
  30b38f:	41 5f                	pop    %r15
  30b391:	5d                   	pop    %rbp
  30b392:	c3                   	ret
  30b393:	ba 19 00 00 00       	mov    $0x19,%edx
  30b398:	41 83 3a 00          	cmpl   $0x0,(%r10)
  30b39c:	75 18                	jne    30b3b6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c6>
  30b39e:	80 b9 8f 01 00 00 01 	cmpb   $0x1,0x18f(%rcx)
  30b3a5:	75 0f                	jne    30b3b6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c6>
  30b3a7:	48 81 c1 7a 01 00 00 	add    $0x17a,%rcx
  30b3ae:	49 89 c9             	mov    %rcx,%r9
  30b3b1:	e9 72 ff ff ff       	jmp    30b328 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x138>
  30b3b6:	48 89 bd 88 f9 ff ff 	mov    %rdi,-0x678(%rbp)
  30b3bd:	4c 89 9d 48 f9 ff ff 	mov    %r11,-0x6b8(%rbp)
  30b3c4:	48 03 90 78 04 00 00 	add    0x478(%rax),%rdx
  30b3cb:	4c 89 95 80 f9 ff ff 	mov    %r10,-0x680(%rbp)
  30b3d2:	49 8b 82 58 1c 00 00 	mov    0x1c58(%r10),%rax
  30b3d9:	45 31 ff             	xor    %r15d,%r15d
  30b3dc:	48 29 d0             	sub    %rdx,%rax
  30b3df:	4c 0f 43 f8          	cmovae %rax,%r15
  30b3e3:	4d 85 c0             	test   %r8,%r8
  30b3e6:	4d 0f 45 f0          	cmovne %r8,%r14
  30b3ea:	4d 8b 6e 10          	mov    0x10(%r14),%r13
  30b3ee:	49 29 dd             	sub    %rbx,%r13
  30b3f1:	4d 39 ef             	cmp    %r13,%r15
  30b3f4:	4c 89 bd 98 f9 ff ff 	mov    %r15,-0x668(%rbp)
  30b3fb:	76 44                	jbe    30b441 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x251>
  30b3fd:	4d 85 ff             	test   %r15,%r15
  30b400:	0f 88 39 1f 00 00    	js     30d33f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x214f>
  30b406:	48 8b bd 98 f9 ff ff 	mov    -0x668(%rbp),%rdi
  30b40d:	e8 fe f5 dd ff       	call   eaa10 <_Znwm$plt>
  30b412:	48 89 c1             	mov    %rax,%rcx
  30b415:	48 8b 95 88 f9 ff ff 	mov    -0x678(%rbp),%rdx
  30b41c:	48 89 02             	mov    %rax,(%rdx)
  30b41f:	4c 8b bd 98 f9 ff ff 	mov    -0x668(%rbp),%r15
  30b426:	4c 01 f9             	add    %r15,%rcx
  30b429:	48 89 42 08          	mov    %rax,0x8(%rdx)
  30b42d:	49 89 4e 10          	mov    %rcx,0x10(%r14)
  30b431:	48 85 db             	test   %rbx,%rbx
  30b434:	74 0b                	je     30b441 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x251>
  30b436:	48 89 df             	mov    %rbx,%rdi
  30b439:	4c 89 ee             	mov    %r13,%rsi
  30b43c:	e8 5f f5 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30b441:	48 8b 85 78 f9 ff ff 	mov    -0x688(%rbp),%rax
  30b448:	48 8b 18             	mov    (%rax),%rbx
  30b44b:	4c 8b 68 10          	mov    0x10(%rax),%r13
  30b44f:	48 89 58 08          	mov    %rbx,0x8(%rax)
  30b453:	48 8b 85 68 f9 ff ff 	mov    -0x698(%rbp),%rax
  30b45a:	0f b6 40 14          	movzbl 0x14(%rax),%eax
  30b45e:	48 8b 8d 50 f9 ff ff 	mov    -0x6b0(%rbp),%rcx
  30b465:	48 8b 49 78          	mov    0x78(%rcx),%rcx
  30b469:	4c 01 f8             	add    %r15,%rax
  30b46c:	4c 8d 3c 01          	lea    (%rcx,%rax,1),%r15
  30b470:	49 83 c7 05          	add    $0x5,%r15
  30b474:	49 29 dd             	sub    %rbx,%r13
  30b477:	4d 39 ef             	cmp    %r13,%r15
  30b47a:	76 36                	jbe    30b4b2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2c2>
  30b47c:	4d 85 ff             	test   %r15,%r15
  30b47f:	0f 88 ba 1e 00 00    	js     30d33f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x214f>
  30b485:	4c 89 ff             	mov    %r15,%rdi
  30b488:	e8 83 f5 dd ff       	call   eaa10 <_Znwm$plt>
  30b48d:	48 8b 8d 78 f9 ff ff 	mov    -0x688(%rbp),%rcx
  30b494:	48 89 01             	mov    %rax,(%rcx)
  30b497:	49 01 c7             	add    %rax,%r15
  30b49a:	48 89 41 08          	mov    %rax,0x8(%rcx)
  30b49e:	4c 89 79 10          	mov    %r15,0x10(%rcx)
  30b4a2:	48 85 db             	test   %rbx,%rbx
  30b4a5:	74 0b                	je     30b4b2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2c2>
  30b4a7:	48 89 df             	mov    %rbx,%rdi
  30b4aa:	4c 89 ee             	mov    %r13,%rsi
  30b4ad:	e8 ee f4 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30b4b2:	49 8b 04 24          	mov    (%r12),%rax
  30b4b6:	80 b8 82 06 00 00 01 	cmpb   $0x1,0x682(%rax)
  30b4bd:	4c 8b b5 80 f9 ff ff 	mov    -0x680(%rbp),%r14
  30b4c4:	4c 89 f3             	mov    %r14,%rbx
  30b4c7:	4c 8b bd 88 f9 ff ff 	mov    -0x678(%rbp),%r15
  30b4ce:	75 71                	jne    30b541 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x351>
  30b4d0:	48 8b 88 78 0e 00 00 	mov    0xe78(%rax),%rcx
  30b4d7:	48 05 80 0e 00 00    	add    $0xe80,%rax
  30b4dd:	4c 89 f3             	mov    %r14,%rbx
  30b4e0:	48 39 c1             	cmp    %rax,%rcx
  30b4e3:	75 10                	jne    30b4f5 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x305>
  30b4e5:	eb 5a                	jmp    30b541 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x351>
  30b4e7:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
  30b4ee:	00 00 
  30b4f0:	48 39 c1             	cmp    %rax,%rcx
  30b4f3:	74 49                	je     30b53e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x34e>
  30b4f5:	48 8b 51 28          	mov    0x28(%rcx),%rdx
  30b4f9:	48 83 ba d0 00 00 00 	cmpq   $0x0,0xd0(%rdx)
  30b500:	00 
  30b501:	75 2b                	jne    30b52e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x33e>
  30b503:	48 8b 51 08          	mov    0x8(%rcx),%rdx
  30b507:	48 85 d2             	test   %rdx,%rdx
  30b50a:	74 14                	je     30b520 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x330>
  30b50c:	0f 1f 40 00          	nopl   0x0(%rax)
  30b510:	48 89 d1             	mov    %rdx,%rcx
  30b513:	48 8b 12             	mov    (%rdx),%rdx
  30b516:	48 85 d2             	test   %rdx,%rdx
  30b519:	75 f5                	jne    30b510 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x320>
  30b51b:	eb d3                	jmp    30b4f0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x300>
  30b51d:	0f 1f 00             	nopl   (%rax)
  30b520:	48 89 ca             	mov    %rcx,%rdx
  30b523:	48 8b 49 10          	mov    0x10(%rcx),%rcx
  30b527:	48 3b 11             	cmp    (%rcx),%rdx
  30b52a:	75 f4                	jne    30b520 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x330>
  30b52c:	eb c2                	jmp    30b4f0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x300>
  30b52e:	8b 71 20             	mov    0x20(%rcx),%esi
  30b531:	48 8b 38             	mov    (%rax),%rdi
  30b534:	e8 47 74 ff ff       	call   302980 <std::__1::map<unsigned int, std::__1::unique_ptr<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state, std::__1::default_delete<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state> >, std::__1::less<unsigned int>, std::__1::allocator<std::__1::pair<unsigned int const, std::__1::unique_ptr<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state, std::__1::default_delete<cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::application_path_state> > > > >::at(unsigned int const&)>
  30b539:	48 8b 18             	mov    (%rax),%rbx
  30b53c:	eb 03                	jmp    30b541 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x351>
  30b53e:	4c 89 f3             	mov    %r14,%rbx
  30b541:	48 8d b3 c0 00 00 00 	lea    0xc0(%rbx),%rsi
  30b548:	48 8d bd 30 fa ff ff 	lea    -0x5d0(%rbp),%rdi
  30b54f:	48 89 b5 40 f9 ff ff 	mov    %rsi,-0x6c0(%rbp)
  30b556:	e8 95 59 00 00       	call   310ef0 <cnetmod::quic::(anonymous namespace)::take_ack_frame(std::__1::set<unsigned long, std::__1::less<unsigned long>, std::__1::allocator<unsigned long> >&)>
  30b55b:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30b562:	0f 85 b3 00 00 00    	jne    30b61b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x42b>
  30b568:	49 8b 04 24          	mov    (%r12),%rax
  30b56c:	80 b8 82 06 00 00 01 	cmpb   $0x1,0x682(%rax)
  30b573:	75 52                	jne    30b5c7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x3d7>
  30b575:	8b 03                	mov    (%rbx),%eax
  30b577:	89 85 40 ff ff ff    	mov    %eax,-0xc0(%rbp)
  30b57d:	48 8d bd 48 ff ff ff 	lea    -0xb8(%rbp),%rdi
  30b584:	48 8d b5 30 fa ff ff 	lea    -0x5d0(%rbp),%rsi
  30b58b:	e8 e0 5e 00 00       	call   311470 <cnetmod::quic::ack_frame@cnetmod.protocol.quic::ack_frame(cnetmod::quic::ack_frame@cnetmod.protocol.quic const&)>
  30b590:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30b597:	48 8d b5 40 ff ff ff 	lea    -0xc0(%rbp),%rsi
  30b59e:	e8 cd b8 03 00       	call   346e70 <cnetmod::quic::encode_frame@cnetmod.protocol.quic(cnetmod::quic::path_ack_frame@cnetmod.protocol.quic const&)>
  30b5a3:	48 8b bd 68 ff ff ff 	mov    -0x98(%rbp),%rdi
  30b5aa:	48 85 ff             	test   %rdi,%rdi
  30b5ad:	74 2b                	je     30b5da <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x3ea>
  30b5af:	48 89 bd 70 ff ff ff 	mov    %rdi,-0x90(%rbp)
  30b5b6:	48 8b b5 78 ff ff ff 	mov    -0x88(%rbp),%rsi
  30b5bd:	48 29 fe             	sub    %rdi,%rsi
  30b5c0:	e8 db f3 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30b5c5:	eb 13                	jmp    30b5da <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x3ea>
  30b5c7:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30b5ce:	48 8d b5 30 fa ff ff 	lea    -0x5d0(%rbp),%rsi
  30b5d5:	e8 46 ac 03 00       	call   346220 <cnetmod::quic::encode_frame@cnetmod.protocol.quic(cnetmod::quic::ack_frame@cnetmod.protocol.quic const&)>
  30b5da:	49 8b 77 08          	mov    0x8(%r15),%rsi
  30b5de:	4c 89 ff             	mov    %r15,%rdi
  30b5e1:	4c 8b ad 40 fd ff ff 	mov    -0x2c0(%rbp),%r13
  30b5e8:	48 8b 8d 48 fd ff ff 	mov    -0x2b8(%rbp),%rcx
  30b5ef:	49 89 c8             	mov    %rcx,%r8
  30b5f2:	4d 29 e8             	sub    %r13,%r8
  30b5f5:	4c 89 ea             	mov    %r13,%rdx
  30b5f8:	e8 23 f5 fa ff       	call   2bab20 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30b5fd:	4d 85 ed             	test   %r13,%r13
  30b600:	74 19                	je     30b61b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x42b>
  30b602:	4c 89 ad 48 fd ff ff 	mov    %r13,-0x2b8(%rbp)
  30b609:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30b610:	4c 29 ee             	sub    %r13,%rsi
  30b613:	4c 89 ef             	mov    %r13,%rdi
  30b616:	e8 85 f3 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30b61b:	c6 45 a8 00          	movb   $0x0,-0x58(%rbp)
  30b61f:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b623:	c5 fe 7f 85 40 ff ff 	vmovdqu %ymm0,-0xc0(%rbp)
  30b62a:	ff 
  30b62b:	c5 fe 7f 85 60 ff ff 	vmovdqu %ymm0,-0xa0(%rbp)
  30b632:	ff 
  30b633:	c5 fe 7f 45 80       	vmovdqu %ymm0,-0x80(%rbp)
  30b638:	c6 45 a0 00          	movb   $0x0,-0x60(%rbp)
  30b63c:	66 c7 45 b0 03 00    	movw   $0x3,-0x50(%rbp)
  30b642:	c6 45 b2 00          	movb   $0x0,-0x4e(%rbp)
  30b646:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b64a:	c5 fa 7f 45 b8       	vmovdqu %xmm0,-0x48(%rbp)
  30b64f:	48 c7 45 c8 00 00 00 	movq   $0x0,-0x38(%rbp)
  30b656:	00 
  30b657:	c6 85 10 fa ff ff 00 	movb   $0x0,-0x5f0(%rbp)
  30b65e:	c6 85 28 fa ff ff 00 	movb   $0x0,-0x5d8(%rbp)
  30b665:	c5 f9 7f 85 b0 f9 ff 	vmovdqa %xmm0,-0x650(%rbp)
  30b66c:	ff 
  30b66d:	48 c7 85 c0 f9 ff ff 	movq   $0x0,-0x640(%rbp)
  30b674:	00 00 00 00 
  30b678:	49 8b 86 60 1c 00 00 	mov    0x1c60(%r14),%rax
  30b67f:	48 89 85 58 f9 ff ff 	mov    %rax,-0x6a8(%rbp)
  30b686:	41 0f b6 8e 68 1c 00 	movzbl 0x1c68(%r14),%ecx
  30b68d:	00 
  30b68e:	41 8b 86 6c 1c 00 00 	mov    0x1c6c(%r14),%eax
  30b695:	89 85 0b fa ff ff    	mov    %eax,-0x5f5(%rbp)
  30b69b:	41 8b 86 69 1c 00 00 	mov    0x1c69(%r14),%eax
  30b6a2:	89 85 08 fa ff ff    	mov    %eax,-0x5f8(%rbp)
  30b6a8:	88 8d af f9 ff ff    	mov    %cl,-0x651(%rbp)
  30b6ae:	80 f9 01             	cmp    $0x1,%cl
  30b6b1:	48 8b 9d 98 f9 ff ff 	mov    -0x668(%rbp),%rbx
  30b6b8:	75 3f                	jne    30b6f9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x509>
  30b6ba:	bf 01 00 00 00       	mov    $0x1,%edi
  30b6bf:	c5 f8 77             	vzeroupper
  30b6c2:	e8 49 f3 dd ff       	call   eaa10 <_Znwm$plt>
  30b6c7:	49 89 c5             	mov    %rax,%r13
  30b6ca:	48 89 c1             	mov    %rax,%rcx
  30b6cd:	48 ff c1             	inc    %rcx
  30b6d0:	c6 00 01             	movb   $0x1,(%rax)
  30b6d3:	48 8b bd 88 f9 ff ff 	mov    -0x678(%rbp),%rdi
  30b6da:	48 8b 77 08          	mov    0x8(%rdi),%rsi
  30b6de:	41 b8 01 00 00 00    	mov    $0x1,%r8d
  30b6e4:	48 89 c2             	mov    %rax,%rdx
  30b6e7:	e8 34 f4 fa ff       	call   2bab20 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30b6ec:	be 01 00 00 00       	mov    $0x1,%esi
  30b6f1:	4c 89 ef             	mov    %r13,%rdi
  30b6f4:	e8 a7 f2 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30b6f9:	49 8b 04 24          	mov    (%r12),%rax
  30b6fd:	83 78 60 01          	cmpl   $0x1,0x60(%rax)
  30b701:	0f 85 6f 02 00 00    	jne    30b976 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x786>
  30b707:	48 83 b8 50 2c 00 00 	cmpq   $0x0,0x2c50(%rax)
  30b70e:	00 
  30b70f:	0f 84 f4 00 00 00    	je     30b809 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x619>
  30b715:	48 8d b8 28 2c 00 00 	lea    0x2c28(%rax),%rdi
  30b71c:	48 8b 88 30 2c 00 00 	mov    0x2c30(%rax),%rcx
  30b723:	48 8b 90 48 2c 00 00 	mov    0x2c48(%rax),%rdx
  30b72a:	48 b8 c1 c0 c0 c0 c0 	movabs $0xc0c0c0c0c0c0c0c1,%rax
  30b731:	c0 c0 c0 
  30b734:	c4 e2 fb f6 c0       	mulx   %rax,%rax,%rax
  30b739:	48 c1 e8 07          	shr    $0x7,%rax
  30b73d:	48 8b 0c c1          	mov    (%rcx,%rax,8),%rcx
  30b741:	48 69 c0 aa 00 00 00 	imul   $0xaa,%rax,%rax
  30b748:	48 29 c2             	sub    %rax,%rdx
  30b74b:	48 8d 04 52          	lea    (%rdx,%rdx,2),%rax
  30b74f:	48 8b 1c c1          	mov    (%rcx,%rax,8),%rbx
  30b753:	4c 8b 7c c1 08       	mov    0x8(%rcx,%rax,8),%r15
  30b758:	4c 8b 6c c1 10       	mov    0x10(%rcx,%rax,8),%r13
  30b75d:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b761:	c5 fa 7f 04 c1       	vmovdqu %xmm0,(%rcx,%rax,8)
  30b766:	48 c7 44 c1 10 00 00 	movq   $0x0,0x10(%rcx,%rax,8)
  30b76d:	00 00 
  30b76f:	c5 f8 77             	vzeroupper
  30b772:	e8 e9 4a 00 00       	call   310260 <std::__1::deque<std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::allocator<std::__1::vector<std::byte, std::__1::allocator<std::byte> > > >::pop_front()>
  30b777:	48 8b bd 88 f9 ff ff 	mov    -0x678(%rbp),%rdi
  30b77e:	48 8b 77 08          	mov    0x8(%rdi),%rsi
  30b782:	4d 89 f8             	mov    %r15,%r8
  30b785:	49 29 d8             	sub    %rbx,%r8
  30b788:	48 89 da             	mov    %rbx,%rdx
  30b78b:	4c 89 f9             	mov    %r15,%rcx
  30b78e:	e8 bd dc fa ff       	call   2b9450 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*>, long)>
  30b793:	48 89 9d c8 fe ff ff 	mov    %rbx,-0x138(%rbp)
  30b79a:	4c 89 bd d0 fe ff ff 	mov    %r15,-0x130(%rbp)
  30b7a1:	4c 89 ad d8 fe ff ff 	mov    %r13,-0x128(%rbp)
  30b7a8:	c6 85 30 ff ff ff 00 	movb   $0x0,-0xd0(%rbp)
  30b7af:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b7b3:	c5 fe 7f 85 e0 fe ff 	vmovdqu %ymm0,-0x120(%rbp)
  30b7ba:	ff 
  30b7bb:	c5 fe 7f 85 00 ff ff 	vmovdqu %ymm0,-0x100(%rbp)
  30b7c2:	ff 
  30b7c3:	c5 fe 7f 85 09 ff ff 	vmovdqu %ymm0,-0xf7(%rbp)
  30b7ca:	ff 
  30b7cb:	66 c7 85 38 ff ff ff 	movw   $0x3,-0xc8(%rbp)
  30b7d2:	03 00 
  30b7d4:	c6 85 3a ff ff ff 00 	movb   $0x0,-0xc6(%rbp)
  30b7db:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30b7e2:	48 8d b5 c8 fe ff ff 	lea    -0x138(%rbp),%rsi
  30b7e9:	c5 f8 77             	vzeroupper
  30b7ec:	e8 bf cf 03 00       	call   3487b0 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::push_back(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline)>
  30b7f1:	48 8d bd c8 fe ff ff 	lea    -0x138(%rbp),%rdi
  30b7f8:	e8 53 42 ff ff       	call   2ffa50 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30b7fd:	48 8b 9d 98 f9 ff ff 	mov    -0x668(%rbp),%rbx
  30b804:	e9 6d 01 00 00       	jmp    30b976 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x786>
  30b809:	48 8b 80 58 03 00 00 	mov    0x358(%rax),%rax
  30b810:	4c 8b a8 b8 06 00 00 	mov    0x6b8(%rax),%r13
  30b817:	48 8b 98 c0 06 00 00 	mov    0x6c0(%rax),%rbx
  30b81e:	4c 8b b8 c8 06 00 00 	mov    0x6c8(%rax),%r15
  30b825:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b829:	c5 fa 7f 80 b8 06 00 	vmovdqu %xmm0,0x6b8(%rax)
  30b830:	00 
  30b831:	48 c7 80 c8 06 00 00 	movq   $0x0,0x6c8(%rax)
  30b838:	00 00 00 00 
  30b83c:	49 39 dd             	cmp    %rbx,%r13
  30b83f:	0f 84 14 01 00 00    	je     30b959 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x769>
  30b845:	49 8b 04 24          	mov    (%r12),%rax
  30b849:	48 8b 80 90 2b 00 00 	mov    0x2b90(%rax),%rax
  30b850:	48 89 85 d0 fd ff ff 	mov    %rax,-0x230(%rbp)
  30b857:	4c 89 ad d8 fd ff ff 	mov    %r13,-0x228(%rbp)
  30b85e:	4c 29 eb             	sub    %r13,%rbx
  30b861:	48 89 9d e0 fd ff ff 	mov    %rbx,-0x220(%rbp)
  30b868:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30b86f:	48 8d b5 d0 fd ff ff 	lea    -0x230(%rbp),%rsi
  30b876:	c5 f8 77             	vzeroupper
  30b879:	e8 62 ad 03 00       	call   3465e0 <cnetmod::quic::encode_frame@cnetmod.protocol.quic(cnetmod::quic::crypto_frame@cnetmod.protocol.quic const&)>
  30b87e:	49 8b 04 24          	mov    (%r12),%rax
  30b882:	48 01 98 90 2b 00 00 	add    %rbx,0x2b90(%rax)
  30b889:	48 8b bd 88 f9 ff ff 	mov    -0x678(%rbp),%rdi
  30b890:	48 8b 77 08          	mov    0x8(%rdi),%rsi
  30b894:	48 8b 95 40 fd ff ff 	mov    -0x2c0(%rbp),%rdx
  30b89b:	48 8b 8d 48 fd ff ff 	mov    -0x2b8(%rbp),%rcx
  30b8a2:	49 89 c8             	mov    %rcx,%r8
  30b8a5:	49 29 d0             	sub    %rdx,%r8
  30b8a8:	e8 a3 db fa ff       	call   2b9450 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte*>, std::__1::__wrap_iter<std::byte*>, long)>
  30b8ad:	48 8b 85 50 fd ff ff 	mov    -0x2b0(%rbp),%rax
  30b8b4:	c5 f9 6f 85 40 fd ff 	vmovdqa -0x2c0(%rbp),%xmm0
  30b8bb:	ff 
  30b8bc:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30b8c0:	c5 f8 29 8d 40 fd ff 	vmovaps %xmm1,-0x2c0(%rbp)
  30b8c7:	ff 
  30b8c8:	48 c7 85 50 fd ff ff 	movq   $0x0,-0x2b0(%rbp)
  30b8cf:	00 00 00 00 
  30b8d3:	c5 f9 7f 85 50 fe ff 	vmovdqa %xmm0,-0x1b0(%rbp)
  30b8da:	ff 
  30b8db:	48 89 85 60 fe ff ff 	mov    %rax,-0x1a0(%rbp)
  30b8e2:	c6 85 b8 fe ff ff 00 	movb   $0x0,-0x148(%rbp)
  30b8e9:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30b8ed:	c5 fe 7f 85 68 fe ff 	vmovdqu %ymm0,-0x198(%rbp)
  30b8f4:	ff 
  30b8f5:	c5 fe 7f 85 88 fe ff 	vmovdqu %ymm0,-0x178(%rbp)
  30b8fc:	ff 
  30b8fd:	c5 fe 7f 85 91 fe ff 	vmovdqu %ymm0,-0x16f(%rbp)
  30b904:	ff 
  30b905:	66 c7 85 c0 fe ff ff 	movw   $0x3,-0x140(%rbp)
  30b90c:	03 00 
  30b90e:	c6 85 c2 fe ff ff 00 	movb   $0x0,-0x13e(%rbp)
  30b915:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30b91c:	48 8d b5 50 fe ff ff 	lea    -0x1b0(%rbp),%rsi
  30b923:	c5 f8 77             	vzeroupper
  30b926:	e8 85 ce 03 00       	call   3487b0 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::push_back(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline)>
  30b92b:	48 8d bd 50 fe ff ff 	lea    -0x1b0(%rbp),%rdi
  30b932:	e8 19 41 ff ff       	call   2ffa50 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30b937:	48 8b bd 40 fd ff ff 	mov    -0x2c0(%rbp),%rdi
  30b93e:	48 85 ff             	test   %rdi,%rdi
  30b941:	74 16                	je     30b959 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x769>
  30b943:	48 89 bd 48 fd ff ff 	mov    %rdi,-0x2b8(%rbp)
  30b94a:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30b951:	48 29 fe             	sub    %rdi,%rsi
  30b954:	e8 47 f0 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30b959:	4d 85 ed             	test   %r13,%r13
  30b95c:	48 8b 9d 98 f9 ff ff 	mov    -0x668(%rbp),%rbx
  30b963:	74 11                	je     30b976 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x786>
  30b965:	4d 29 ef             	sub    %r13,%r15
  30b968:	4c 89 ef             	mov    %r13,%rdi
  30b96b:	4c 89 fe             	mov    %r15,%rsi
  30b96e:	c5 f8 77             	vzeroupper
  30b971:	e8 2a f0 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30b976:	4c 89 a5 a0 f9 ff ff 	mov    %r12,-0x660(%rbp)
  30b97d:	4c 8b ad 88 f9 ff ff 	mov    -0x678(%rbp),%r13
  30b984:	eb 1a                	jmp    30b9a0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x7b0>
  30b986:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
  30b98d:	00 00 00 
  30b990:	45 84 f6             	test   %r14b,%r14b
  30b993:	48 8b 9d 98 f9 ff ff 	mov    -0x668(%rbp),%rbx
  30b99a:	0f 84 bc 08 00 00    	je     30c25c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x106c>
  30b9a0:	49 8b 34 24          	mov    (%r12),%rsi
  30b9a4:	4c 8b b6 f0 06 00 00 	mov    0x6f0(%rsi),%r14
  30b9ab:	4d 85 f6             	test   %r14,%r14
  30b9ae:	75 70                	jne    30ba20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30b9b0:	48 8b 86 f8 06 00 00 	mov    0x6f8(%rsi),%rax
  30b9b7:	48 83 78 40 00       	cmpq   $0x0,0x40(%rax)
  30b9bc:	75 62                	jne    30ba20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30b9be:	48 83 b8 30 01 00 00 	cmpq   $0x0,0x130(%rax)
  30b9c5:	00 
  30b9c6:	75 58                	jne    30ba20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30b9c8:	48 83 b8 60 01 00 00 	cmpq   $0x0,0x160(%rax)
  30b9cf:	00 
  30b9d0:	75 4e                	jne    30ba20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30b9d2:	48 83 b8 90 01 00 00 	cmpq   $0x0,0x190(%rax)
  30b9d9:	00 
  30b9da:	75 44                	jne    30ba20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30b9dc:	48 83 b8 c0 01 00 00 	cmpq   $0x0,0x1c0(%rax)
  30b9e3:	00 
  30b9e4:	75 3a                	jne    30ba20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30b9e6:	48 83 b8 f0 01 00 00 	cmpq   $0x0,0x1f0(%rax)
  30b9ed:	00 
  30b9ee:	75 30                	jne    30ba20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30b9f0:	48 83 b8 20 02 00 00 	cmpq   $0x0,0x220(%rax)
  30b9f7:	00 
  30b9f8:	75 26                	jne    30ba20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30b9fa:	48 83 b8 50 02 00 00 	cmpq   $0x0,0x250(%rax)
  30ba01:	00 
  30ba02:	75 1c                	jne    30ba20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x830>
  30ba04:	48 83 b8 80 02 00 00 	cmpq   $0x0,0x280(%rax)
  30ba0b:	00 
  30ba0c:	0f 84 4e 08 00 00    	je     30c260 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1070>
  30ba12:	66 66 66 66 66 2e 0f 	data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
  30ba19:	1f 84 00 00 00 00 00 
  30ba20:	49 8b 45 08          	mov    0x8(%r13),%rax
  30ba24:	49 2b 45 00          	sub    0x0(%r13),%rax
  30ba28:	48 39 d8             	cmp    %rbx,%rax
  30ba2b:	0f 83 2f 08 00 00    	jae    30c260 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1070>
  30ba31:	c6 85 38 fe ff ff 00 	movb   $0x0,-0x1c8(%rbp)
  30ba38:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30ba3c:	c5 fe 7f 85 d0 fd ff 	vmovdqu %ymm0,-0x230(%rbp)
  30ba43:	ff 
  30ba44:	c5 fe 7f 85 f0 fd ff 	vmovdqu %ymm0,-0x210(%rbp)
  30ba4b:	ff 
  30ba4c:	c5 fe 7f 85 10 fe ff 	vmovdqu %ymm0,-0x1f0(%rbp)
  30ba53:	ff 
  30ba54:	c6 85 30 fe ff ff 00 	movb   $0x0,-0x1d0(%rbp)
  30ba5b:	49 81 e7 00 ff ff ff 	and    $0xffffffffffffff00,%r15
  30ba62:	66 c7 85 40 fe ff ff 	movw   $0x3,-0x1c0(%rbp)
  30ba69:	03 00 
  30ba6b:	c6 85 42 fe ff ff 00 	movb   $0x0,-0x1be(%rbp)
  30ba72:	4d 85 f6             	test   %r14,%r14
  30ba75:	0f 84 c5 02 00 00    	je     30bd40 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xb50>
  30ba7b:	48 8b 86 d0 06 00 00 	mov    0x6d0(%rsi),%rax
  30ba82:	48 8b 96 e8 06 00 00 	mov    0x6e8(%rsi),%rdx
  30ba89:	48 b9 f1 f0 f0 f0 f0 	movabs $0xf0f0f0f0f0f0f0f1,%rcx
  30ba90:	f0 f0 f0 
  30ba93:	c4 e2 f3 f6 c9       	mulx   %rcx,%rcx,%rcx
  30ba98:	48 89 ce             	mov    %rcx,%rsi
  30ba9b:	48 c1 ee 05          	shr    $0x5,%rsi
  30ba9f:	4c 8b 24 f0          	mov    (%rax,%rsi,8),%r12
  30baa3:	48 b8 e0 ff ff ff ff 	movabs $0x1fffffffffffffe0,%rax
  30baaa:	ff ff 1f 
  30baad:	48 21 c1             	and    %rax,%rcx
  30bab0:	48 8d 04 71          	lea    (%rcx,%rsi,2),%rax
  30bab4:	48 29 c2             	sub    %rax,%rdx
  30bab7:	4c 6b ea 78          	imul   $0x78,%rdx,%r13
  30babb:	c4 81 78 10 04 2c    	vmovups (%r12,%r13,1),%xmm0
  30bac1:	c5 f8 29 85 d0 fd ff 	vmovaps %xmm0,-0x230(%rbp)
  30bac8:	ff 
  30bac9:	4b 8b 44 2c 10       	mov    0x10(%r12,%r13,1),%rax
  30bace:	48 89 85 e0 fd ff ff 	mov    %rax,-0x220(%rbp)
  30bad5:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30bad9:	c4 81 78 11 0c 2c    	vmovups %xmm1,(%r12,%r13,1)
  30badf:	4b c7 44 2c 10 00 00 	movq   $0x0,0x10(%r12,%r13,1)
  30bae6:	00 00 
  30bae8:	c4 81 78 10 44 2c 18 	vmovups 0x18(%r12,%r13,1),%xmm0
  30baef:	c4 81 78 11 4c 2c 18 	vmovups %xmm1,0x18(%r12,%r13,1)
  30baf6:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bafd:	c5 f8 11 85 e8 fd ff 	vmovups %xmm0,-0x218(%rbp)
  30bb04:	ff 
  30bb05:	48 85 db             	test   %rbx,%rbx
  30bb08:	74 26                	je     30bb30 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x940>
  30bb0a:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30bb11:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30bb17:	48 85 c0             	test   %rax,%rax
  30bb1a:	75 14                	jne    30bb30 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x940>
  30bb1c:	48 8b 03             	mov    (%rbx),%rax
  30bb1f:	48 89 df             	mov    %rbx,%rdi
  30bb22:	c5 f8 77             	vzeroupper
  30bb25:	ff 50 10             	call   *0x10(%rax)
  30bb28:	48 89 df             	mov    %rbx,%rdi
  30bb2b:	e8 d0 ec dd ff       	call   ea800 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30bb30:	4d 01 ec             	add    %r13,%r12
  30bb33:	c4 c1 7e 6f 44 24 28 	vmovdqu 0x28(%r12),%ymm0
  30bb3a:	c4 c1 7c 10 4c 24 48 	vmovups 0x48(%r12),%ymm1
  30bb41:	c4 c1 7c 10 54 24 53 	vmovups 0x53(%r12),%ymm2
  30bb48:	48 8d 85 f8 fd ff ff 	lea    -0x208(%rbp),%rax
  30bb4f:	c5 fc 11 50 2b       	vmovups %ymm2,0x2b(%rax)
  30bb54:	c5 fc 11 48 20       	vmovups %ymm1,0x20(%rax)
  30bb59:	c5 fe 7f 00          	vmovdqu %ymm0,(%rax)
  30bb5d:	4c 8b a5 a0 f9 ff ff 	mov    -0x660(%rbp),%r12
  30bb64:	49 8b 3c 24          	mov    (%r12),%rdi
  30bb68:	b8 c8 06 00 00       	mov    $0x6c8,%eax
  30bb6d:	48 01 c7             	add    %rax,%rdi
  30bb70:	c5 f8 77             	vzeroupper
  30bb73:	e8 b8 59 00 00       	call   311530 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::pop_front()>
  30bb78:	4c 8b ad 88 f9 ff ff 	mov    -0x678(%rbp),%r13
  30bb7f:	48 8b 95 d0 fd ff ff 	mov    -0x230(%rbp),%rdx
  30bb86:	48 8b 8d d8 fd ff ff 	mov    -0x228(%rbp),%rcx
  30bb8d:	48 8b 85 18 fe ff ff 	mov    -0x1e8(%rbp),%rax
  30bb94:	48 8b bd 28 fe ff ff 	mov    -0x1d8(%rbp),%rdi
  30bb9b:	48 01 c7             	add    %rax,%rdi
  30bb9e:	49 89 c8             	mov    %rcx,%r8
  30bba1:	49 29 d0             	sub    %rdx,%r8
  30bba4:	49 0f 45 f8          	cmovne %r8,%rdi
  30bba8:	49 8b 75 08          	mov    0x8(%r13),%rsi
  30bbac:	4c 8b 8d 98 f9 ff ff 	mov    -0x668(%rbp),%r9
  30bbb3:	49 29 f1             	sub    %rsi,%r9
  30bbb6:	4d 03 4d 00          	add    0x0(%r13),%r9
  30bbba:	4c 39 cf             	cmp    %r9,%rdi
  30bbbd:	0f 86 3d 01 00 00    	jbe    30bd00 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xb10>
  30bbc3:	49 8b 1c 24          	mov    (%r12),%rbx
  30bbc7:	4d 85 f6             	test   %r14,%r14
  30bbca:	0f 84 db 02 00 00    	je     30beab <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xcbb>
  30bbd0:	48 8b 93 e8 06 00 00 	mov    0x6e8(%rbx),%rdx
  30bbd7:	48 85 d2             	test   %rdx,%rdx
  30bbda:	75 16                	jne    30bbf2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xa02>
  30bbdc:	48 8d bb c8 06 00 00 	lea    0x6c8(%rbx),%rdi
  30bbe3:	c5 f8 77             	vzeroupper
  30bbe6:	e8 85 da 00 00       	call   319670 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::__add_front_capacity()>
  30bbeb:	48 8b 93 e8 06 00 00 	mov    0x6e8(%rbx),%rdx
  30bbf2:	48 8b 8b d0 06 00 00 	mov    0x6d0(%rbx),%rcx
  30bbf9:	48 b8 f1 f0 f0 f0 f0 	movabs $0xf0f0f0f0f0f0f0f1,%rax
  30bc00:	f0 f0 f0 
  30bc03:	c4 e2 fb f6 c0       	mulx   %rax,%rax,%rax
  30bc08:	48 89 c6             	mov    %rax,%rsi
  30bc0b:	48 c1 ee 05          	shr    $0x5,%rsi
  30bc0f:	48 bf e0 ff ff ff ff 	movabs $0x1fffffffffffffe0,%rdi
  30bc16:	ff ff 1f 
  30bc19:	48 21 f8             	and    %rdi,%rax
  30bc1c:	48 8d 04 70          	lea    (%rax,%rsi,2),%rax
  30bc20:	48 29 c2             	sub    %rax,%rdx
  30bc23:	48 8b 3c f1          	mov    (%rcx,%rsi,8),%rdi
  30bc27:	48 6b c2 78          	imul   $0x78,%rdx,%rax
  30bc2b:	48 01 f8             	add    %rdi,%rax
  30bc2e:	48 3b 8b d8 06 00 00 	cmp    0x6d8(%rbx),%rcx
  30bc35:	ba 00 00 00 00       	mov    $0x0,%edx
  30bc3a:	48 0f 44 c2          	cmove  %rdx,%rax
  30bc3e:	48 39 f8             	cmp    %rdi,%rax
  30bc41:	75 0d                	jne    30bc50 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xa60>
  30bc43:	48 8b 44 f1 f8       	mov    -0x8(%rcx,%rsi,8),%rax
  30bc48:	b9 f0 0f 00 00       	mov    $0xff0,%ecx
  30bc4d:	48 01 c8             	add    %rcx,%rax
  30bc50:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30bc54:	c5 f8 11 48 88       	vmovups %xmm1,-0x78(%rax)
  30bc59:	48 c7 40 98 00 00 00 	movq   $0x0,-0x68(%rax)
  30bc60:	00 
  30bc61:	c5 f8 28 85 d0 fd ff 	vmovaps -0x230(%rbp),%xmm0
  30bc68:	ff 
  30bc69:	c5 f8 11 40 88       	vmovups %xmm0,-0x78(%rax)
  30bc6e:	48 8b 8d e0 fd ff ff 	mov    -0x220(%rbp),%rcx
  30bc75:	48 89 48 98          	mov    %rcx,-0x68(%rax)
  30bc79:	c5 f8 29 8d d0 fd ff 	vmovaps %xmm1,-0x230(%rbp)
  30bc80:	ff 
  30bc81:	48 c7 85 e0 fd ff ff 	movq   $0x0,-0x220(%rbp)
  30bc88:	00 00 00 00 
  30bc8c:	c5 f8 10 85 e8 fd ff 	vmovups -0x218(%rbp),%xmm0
  30bc93:	ff 
  30bc94:	c5 f8 11 40 a0       	vmovups %xmm0,-0x60(%rax)
  30bc99:	48 8d 8d e8 fd ff ff 	lea    -0x218(%rbp),%rcx
  30bca0:	c5 f8 11 09          	vmovups %xmm1,(%rcx)
  30bca4:	48 8d 8d f8 fd ff ff 	lea    -0x208(%rbp),%rcx
  30bcab:	c5 fc 10 01          	vmovups (%rcx),%ymm0
  30bcaf:	c5 fc 10 49 20       	vmovups 0x20(%rcx),%ymm1
  30bcb4:	c5 fc 10 51 2b       	vmovups 0x2b(%rcx),%ymm2
  30bcb9:	c5 fc 11 50 db       	vmovups %ymm2,-0x25(%rax)
  30bcbe:	c5 fc 11 48 d0       	vmovups %ymm1,-0x30(%rax)
  30bcc3:	c5 fc 11 40 b0       	vmovups %ymm0,-0x50(%rax)
  30bcc8:	c5 f9 6f 05 e0 d2 dc 	vmovdqa -0x232d20(%rip),%xmm0        # d8fb0 <typeinfo name for cnetmod::io_uring_context@cnetmod.io.platform.io_uring+0xbc2>
  30bccf:	ff 
  30bcd0:	c5 f9 d4 83 e8 06 00 	vpaddq 0x6e8(%rbx),%xmm0,%xmm0
  30bcd7:	00 
  30bcd8:	c5 fa 7f 83 e8 06 00 	vmovdqu %xmm0,0x6e8(%rbx)
  30bcdf:	00 
  30bce0:	45 31 f6             	xor    %r14d,%r14d
  30bce3:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bcea:	48 85 db             	test   %rbx,%rbx
  30bced:	0f 85 fd 04 00 00    	jne    30c1f0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1000>
  30bcf3:	e9 1e 05 00 00       	jmp    30c216 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1026>
  30bcf8:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
  30bcff:	00 
  30bd00:	48 39 ca             	cmp    %rcx,%rdx
  30bd03:	0f 85 e8 02 00 00    	jne    30bff1 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xe01>
  30bd09:	48 8d 95 f8 fd ff ff 	lea    -0x208(%rbp),%rdx
  30bd10:	48 8d 0c 02          	lea    (%rdx,%rax,1),%rcx
  30bd14:	4c 89 ef             	mov    %r13,%rdi
  30bd17:	49 89 c0             	mov    %rax,%r8
  30bd1a:	c5 f8 77             	vzeroupper
  30bd1d:	e8 ee e7 fa ff       	call   2ba510 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::byte const*, std::byte const*>(std::__1::__wrap_iter<std::byte const*>, std::byte const*, std::byte const*, long)>
  30bd22:	48 8b 95 e8 fd ff ff 	mov    -0x218(%rbp),%rdx
  30bd29:	f6 02 01             	testb  $0x1,(%rdx)
  30bd2c:	0f 84 a6 02 00 00    	je     30bfd8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xde8>
  30bd32:	48 8b 52 10          	mov    0x10(%rdx),%rdx
  30bd36:	e9 a0 02 00 00       	jmp    30bfdb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xdeb>
  30bd3b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
  30bd40:	48 81 c6 f8 06 00 00 	add    $0x6f8,%rsi
  30bd47:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30bd4e:	c5 f8 77             	vzeroupper
  30bd51:	e8 8a 20 04 00       	call   34dde0 <cnetmod::quic::quic_stream_scheduler@cnetmod.protocol.quic.send_pipeline::take()>
  30bd56:	80 bd c8 fd ff ff 00 	cmpb   $0x0,-0x238(%rbp)
  30bd5d:	0f 84 5d 02 00 00    	je     30bfc0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xdd0>
  30bd63:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30bd6a:	48 85 ff             	test   %rdi,%rdi
  30bd6d:	74 16                	je     30bd85 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xb95>
  30bd6f:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30bd76:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30bd7d:	48 29 fe             	sub    %rdi,%rsi
  30bd80:	e8 1b ec dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30bd85:	c5 f8 10 85 50 fd ff 	vmovups -0x2b0(%rbp),%xmm0
  30bd8c:	ff 
  30bd8d:	c5 f8 29 85 d0 fd ff 	vmovaps %xmm0,-0x230(%rbp)
  30bd94:	ff 
  30bd95:	48 8b 85 60 fd ff ff 	mov    -0x2a0(%rbp),%rax
  30bd9c:	48 89 85 e0 fd ff ff 	mov    %rax,-0x220(%rbp)
  30bda3:	48 8d 85 50 fd ff ff 	lea    -0x2b0(%rbp),%rax
  30bdaa:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30bdae:	c5 f8 11 08          	vmovups %xmm1,(%rax)
  30bdb2:	48 c7 40 10 00 00 00 	movq   $0x0,0x10(%rax)
  30bdb9:	00 
  30bdba:	c5 f8 10 85 68 fd ff 	vmovups -0x298(%rbp),%xmm0
  30bdc1:	ff 
  30bdc2:	c5 f8 11 48 18       	vmovups %xmm1,0x18(%rax)
  30bdc7:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bdce:	c5 f8 11 85 e8 fd ff 	vmovups %xmm0,-0x218(%rbp)
  30bdd5:	ff 
  30bdd6:	48 85 db             	test   %rbx,%rbx
  30bdd9:	74 23                	je     30bdfe <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xc0e>
  30bddb:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30bde2:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30bde8:	48 85 c0             	test   %rax,%rax
  30bdeb:	75 11                	jne    30bdfe <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xc0e>
  30bded:	48 8b 03             	mov    (%rbx),%rax
  30bdf0:	48 89 df             	mov    %rbx,%rdi
  30bdf3:	ff 50 10             	call   *0x10(%rax)
  30bdf6:	48 89 df             	mov    %rbx,%rdi
  30bdf9:	e8 02 ea dd ff       	call   ea800 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30bdfe:	48 8d 85 50 fd ff ff 	lea    -0x2b0(%rbp),%rax
  30be05:	c5 fe 6f 40 28       	vmovdqu 0x28(%rax),%ymm0
  30be0a:	c5 fc 10 48 48       	vmovups 0x48(%rax),%ymm1
  30be0f:	c5 fc 10 50 53       	vmovups 0x53(%rax),%ymm2
  30be14:	48 8d 85 f8 fd ff ff 	lea    -0x208(%rbp),%rax
  30be1b:	c5 fc 11 50 2b       	vmovups %ymm2,0x2b(%rax)
  30be20:	c5 fc 11 48 20       	vmovups %ymm1,0x20(%rax)
  30be25:	c5 fe 7f 00          	vmovdqu %ymm0,(%rax)
  30be29:	4c 8b bd 40 fd ff ff 	mov    -0x2c0(%rbp),%r15
  30be30:	0f b7 85 48 fd ff ff 	movzwl -0x2b8(%rbp),%eax
  30be37:	48 89 85 60 f9 ff ff 	mov    %rax,-0x6a0(%rbp)
  30be3e:	80 bd c8 fd ff ff 01 	cmpb   $0x1,-0x238(%rbp)
  30be45:	0f 85 34 fd ff ff    	jne    30bb7f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x98f>
  30be4b:	48 8b 9d 70 fd ff ff 	mov    -0x290(%rbp),%rbx
  30be52:	48 85 db             	test   %rbx,%rbx
  30be55:	74 26                	je     30be7d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xc8d>
  30be57:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30be5e:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30be64:	48 85 c0             	test   %rax,%rax
  30be67:	75 14                	jne    30be7d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xc8d>
  30be69:	48 8b 03             	mov    (%rbx),%rax
  30be6c:	48 89 df             	mov    %rbx,%rdi
  30be6f:	c5 f8 77             	vzeroupper
  30be72:	ff 50 10             	call   *0x10(%rax)
  30be75:	48 89 df             	mov    %rbx,%rdi
  30be78:	e8 83 e9 dd ff       	call   ea800 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30be7d:	48 8b bd 50 fd ff ff 	mov    -0x2b0(%rbp),%rdi
  30be84:	48 85 ff             	test   %rdi,%rdi
  30be87:	0f 84 f2 fc ff ff    	je     30bb7f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x98f>
  30be8d:	48 89 bd 58 fd ff ff 	mov    %rdi,-0x2a8(%rbp)
  30be94:	48 8b b5 60 fd ff ff 	mov    -0x2a0(%rbp),%rsi
  30be9b:	48 29 fe             	sub    %rdi,%rsi
  30be9e:	c5 f8 77             	vzeroupper
  30bea1:	e8 fa ea dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30bea6:	e9 d4 fc ff ff       	jmp    30bb7f <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x98f>
  30beab:	4c 89 bd b8 fc ff ff 	mov    %r15,-0x348(%rbp)
  30beb2:	48 8b 85 60 f9 ff ff 	mov    -0x6a0(%rbp),%rax
  30beb9:	66 89 85 c0 fc ff ff 	mov    %ax,-0x340(%rbp)
  30bec0:	48 89 95 c8 fc ff ff 	mov    %rdx,-0x338(%rbp)
  30bec7:	48 89 8d d0 fc ff ff 	mov    %rcx,-0x330(%rbp)
  30bece:	48 8b 85 e0 fd ff ff 	mov    -0x220(%rbp),%rax
  30bed5:	48 89 85 d8 fc ff ff 	mov    %rax,-0x328(%rbp)
  30bedc:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30bee0:	c5 f8 29 8d d0 fd ff 	vmovaps %xmm1,-0x230(%rbp)
  30bee7:	ff 
  30bee8:	48 c7 85 e0 fd ff ff 	movq   $0x0,-0x220(%rbp)
  30beef:	00 00 00 00 
  30bef3:	c5 f8 10 85 e8 fd ff 	vmovups -0x218(%rbp),%xmm0
  30befa:	ff 
  30befb:	c5 f8 11 85 e0 fc ff 	vmovups %xmm0,-0x320(%rbp)
  30bf02:	ff 
  30bf03:	48 8d 85 e8 fd ff ff 	lea    -0x218(%rbp),%rax
  30bf0a:	c5 f8 11 08          	vmovups %xmm1,(%rax)
  30bf0e:	48 8d 85 f8 fd ff ff 	lea    -0x208(%rbp),%rax
  30bf15:	c5 fe 6f 00          	vmovdqu (%rax),%ymm0
  30bf19:	c5 fc 10 48 20       	vmovups 0x20(%rax),%ymm1
  30bf1e:	c5 fc 10 50 2b       	vmovups 0x2b(%rax),%ymm2
  30bf23:	48 8d 85 f0 fc ff ff 	lea    -0x310(%rbp),%rax
  30bf2a:	c5 fc 11 50 2b       	vmovups %ymm2,0x2b(%rax)
  30bf2f:	c5 fc 11 48 20       	vmovups %ymm1,0x20(%rax)
  30bf34:	c5 fe 7f 00          	vmovdqu %ymm0,(%rax)
  30bf38:	48 89 df             	mov    %rbx,%rdi
  30bf3b:	48 8d b5 b8 fc ff ff 	lea    -0x348(%rbp),%rsi
  30bf42:	c5 f8 77             	vzeroupper
  30bf45:	e8 76 3b ff ff       	call   2ffac0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::requeue_prioritized_stream_frame(cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline)>
  30bf4a:	48 8b 9d e8 fc ff ff 	mov    -0x318(%rbp),%rbx
  30bf51:	48 85 db             	test   %rbx,%rbx
  30bf54:	74 23                	je     30bf79 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xd89>
  30bf56:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30bf5d:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30bf63:	48 85 c0             	test   %rax,%rax
  30bf66:	75 11                	jne    30bf79 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xd89>
  30bf68:	48 8b 03             	mov    (%rbx),%rax
  30bf6b:	48 89 df             	mov    %rbx,%rdi
  30bf6e:	ff 50 10             	call   *0x10(%rax)
  30bf71:	48 89 df             	mov    %rbx,%rdi
  30bf74:	e8 87 e8 dd ff       	call   ea800 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30bf79:	48 8b bd c8 fc ff ff 	mov    -0x338(%rbp),%rdi
  30bf80:	48 85 ff             	test   %rdi,%rdi
  30bf83:	74 3b                	je     30bfc0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xdd0>
  30bf85:	48 89 bd d0 fc ff ff 	mov    %rdi,-0x330(%rbp)
  30bf8c:	48 8b b5 d8 fc ff ff 	mov    -0x328(%rbp),%rsi
  30bf93:	48 29 fe             	sub    %rdi,%rsi
  30bf96:	e8 05 ea dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30bf9b:	45 31 f6             	xor    %r14d,%r14d
  30bf9e:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30bfa5:	48 85 db             	test   %rbx,%rbx
  30bfa8:	0f 85 42 02 00 00    	jne    30c1f0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1000>
  30bfae:	e9 63 02 00 00       	jmp    30c216 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1026>
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
  30c020:	48 8b 45 c0          	mov    -0x40(%rbp),%rax
  30c024:	48 2b 45 b8          	sub    -0x48(%rbp),%rax
  30c028:	48 c1 f8 03          	sar    $0x3,%rax
  30c02c:	48 b9 ef ee ee ee ee 	movabs $0xeeeeeeeeeeeeeeef,%rcx
  30c033:	ee ee ee 
  30c036:	48 0f af c1          	imul   %rcx,%rax
  30c03a:	48 ff c0             	inc    %rax
  30c03d:	4c 89 bd 90 fa ff ff 	mov    %r15,-0x570(%rbp)
  30c044:	48 8b 8d 60 f9 ff ff 	mov    -0x6a0(%rbp),%rcx
  30c04b:	48 89 8d 98 fa ff ff 	mov    %rcx,-0x568(%rbp)
  30c052:	48 89 85 e8 f9 ff ff 	mov    %rax,-0x618(%rbp)
  30c059:	80 bd 28 fa ff ff 00 	cmpb   $0x0,-0x5d8(%rbp)
  30c060:	74 70                	je     30c0d2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xee2>
  30c062:	48 8b 8d b8 f9 ff ff 	mov    -0x648(%rbp),%rcx
  30c069:	48 89 8d f0 f9 ff ff 	mov    %rcx,-0x610(%rbp)
  30c070:	48 8d 95 f0 f9 ff ff 	lea    -0x610(%rbp),%rdx
  30c077:	48 89 95 40 fd ff ff 	mov    %rdx,-0x2c0(%rbp)
  30c07e:	48 8d 95 e8 f9 ff ff 	lea    -0x618(%rbp),%rdx
  30c085:	48 89 95 48 fd ff ff 	mov    %rdx,-0x2b8(%rbp)
  30c08c:	48 8d 95 90 fa ff ff 	lea    -0x570(%rbp),%rdx
  30c093:	48 89 95 50 fd ff ff 	mov    %rdx,-0x2b0(%rbp)
  30c09a:	48 8d 95 b0 f9 ff ff 	lea    -0x650(%rbp),%rdx
  30c0a1:	48 89 95 58 fd ff ff 	mov    %rdx,-0x2a8(%rbp)
  30c0a8:	48 3b 8d c0 f9 ff ff 	cmp    -0x640(%rbp),%rcx
  30c0af:	0f 83 8f 01 00 00    	jae    30c244 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1054>
  30c0b5:	48 89 01             	mov    %rax,(%rcx)
  30c0b8:	c5 fa 6f 85 90 fa ff 	vmovdqu -0x570(%rbp),%xmm0
  30c0bf:	ff 
  30c0c0:	c5 fa 7f 41 08       	vmovdqu %xmm0,0x8(%rcx)
  30c0c5:	48 83 c1 18          	add    $0x18,%rcx
  30c0c9:	48 89 8d b8 f9 ff ff 	mov    %rcx,-0x648(%rbp)
  30c0d0:	eb 2e                	jmp    30c100 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xf10>
  30c0d2:	48 89 85 10 fa ff ff 	mov    %rax,-0x5f0(%rbp)
  30c0d9:	48 8b 85 90 fa ff ff 	mov    -0x570(%rbp),%rax
  30c0e0:	48 8d 8d 18 fa ff ff 	lea    -0x5e8(%rbp),%rcx
  30c0e7:	48 89 01             	mov    %rax,(%rcx)
  30c0ea:	48 8b 85 98 fa ff ff 	mov    -0x568(%rbp),%rax
  30c0f1:	48 89 41 08          	mov    %rax,0x8(%rcx)
  30c0f5:	c6 85 28 fa ff ff 01 	movb   $0x1,-0x5d8(%rbp)
  30c0fc:	0f 1f 40 00          	nopl   0x0(%rax)
  30c100:	c5 f8 28 85 d0 fd ff 	vmovaps -0x230(%rbp),%xmm0
  30c107:	ff 
  30c108:	c5 f8 29 85 40 fc ff 	vmovaps %xmm0,-0x3c0(%rbp)
  30c10f:	ff 
  30c110:	48 8b 85 e0 fd ff ff 	mov    -0x220(%rbp),%rax
  30c117:	48 89 85 50 fc ff ff 	mov    %rax,-0x3b0(%rbp)
  30c11e:	c5 f0 57 c9          	vxorps %xmm1,%xmm1,%xmm1
  30c122:	c5 f8 29 8d d0 fd ff 	vmovaps %xmm1,-0x230(%rbp)
  30c129:	ff 
  30c12a:	48 c7 85 e0 fd ff ff 	movq   $0x0,-0x220(%rbp)
  30c131:	00 00 00 00 
  30c135:	c5 f8 10 85 e8 fd ff 	vmovups -0x218(%rbp),%xmm0
  30c13c:	ff 
  30c13d:	c5 f8 11 85 58 fc ff 	vmovups %xmm0,-0x3a8(%rbp)
  30c144:	ff 
  30c145:	48 8d 85 e8 fd ff ff 	lea    -0x218(%rbp),%rax
  30c14c:	c5 f8 11 08          	vmovups %xmm1,(%rax)
  30c150:	48 8d 85 f8 fd ff ff 	lea    -0x208(%rbp),%rax
  30c157:	c5 fe 6f 00          	vmovdqu (%rax),%ymm0
  30c15b:	c5 fc 10 48 20       	vmovups 0x20(%rax),%ymm1
  30c160:	c5 fc 10 50 2b       	vmovups 0x2b(%rax),%ymm2
  30c165:	48 8d 85 68 fc ff ff 	lea    -0x398(%rbp),%rax
  30c16c:	c5 fc 11 50 2b       	vmovups %ymm2,0x2b(%rax)
  30c171:	c5 fc 11 48 20       	vmovups %ymm1,0x20(%rax)
  30c176:	c5 fe 7f 00          	vmovdqu %ymm0,(%rax)
  30c17a:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30c181:	48 8d b5 40 fc ff ff 	lea    -0x3c0(%rbp),%rsi
  30c188:	c5 f8 77             	vzeroupper
  30c18b:	e8 20 c6 03 00       	call   3487b0 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::push_back(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline)>
  30c190:	48 8b 9d 60 fc ff ff 	mov    -0x3a0(%rbp),%rbx
  30c197:	48 85 db             	test   %rbx,%rbx
  30c19a:	74 23                	je     30c1bf <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfcf>
  30c19c:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30c1a3:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30c1a9:	48 85 c0             	test   %rax,%rax
  30c1ac:	75 11                	jne    30c1bf <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xfcf>
  30c1ae:	48 8b 03             	mov    (%rbx),%rax
  30c1b1:	48 89 df             	mov    %rbx,%rdi
  30c1b4:	ff 50 10             	call   *0x10(%rax)
  30c1b7:	48 89 df             	mov    %rbx,%rdi
  30c1ba:	e8 41 e6 dd ff       	call   ea800 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30c1bf:	48 8b bd 40 fc ff ff 	mov    -0x3c0(%rbp),%rdi
  30c1c6:	41 b6 01             	mov    $0x1,%r14b
  30c1c9:	48 85 ff             	test   %rdi,%rdi
  30c1cc:	74 16                	je     30c1e4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xff4>
  30c1ce:	48 89 bd 48 fc ff ff 	mov    %rdi,-0x3b8(%rbp)
  30c1d5:	48 8b b5 50 fc ff ff 	mov    -0x3b0(%rbp),%rsi
  30c1dc:	48 29 fe             	sub    %rdi,%rsi
  30c1df:	e8 bc e7 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30c1e4:	48 8b 9d f0 fd ff ff 	mov    -0x210(%rbp),%rbx
  30c1eb:	48 85 db             	test   %rbx,%rbx
  30c1ee:	74 26                	je     30c216 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1026>
  30c1f0:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30c1f7:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30c1fd:	48 85 c0             	test   %rax,%rax
  30c200:	75 14                	jne    30c216 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1026>
  30c202:	48 8b 03             	mov    (%rbx),%rax
  30c205:	48 89 df             	mov    %rbx,%rdi
  30c208:	c5 f8 77             	vzeroupper
  30c20b:	ff 50 10             	call   *0x10(%rax)
  30c20e:	48 89 df             	mov    %rbx,%rdi
  30c211:	e8 ea e5 dd ff       	call   ea800 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30c216:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30c21d:	48 85 ff             	test   %rdi,%rdi
  30c220:	0f 84 6a f7 ff ff    	je     30b990 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x7a0>
  30c226:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30c22d:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30c234:	48 29 fe             	sub    %rdi,%rsi
  30c237:	c5 f8 77             	vzeroupper
  30c23a:	e8 61 e7 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30c23f:	e9 4c f7 ff ff       	jmp    30b990 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x7a0>
  30c244:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30c24b:	e8 50 b9 00 00       	call   317ba0 <std::__1::vector<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata>, std::__1::allocator<std::__1::pair<unsigned long, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata> > >::emplace_back<unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&>(unsigned long&, cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)::scheduled_stream_frame_metadata&)::{lambda()#2}::operator()() const>
  30c250:	48 8b 8d f0 f9 ff ff 	mov    -0x610(%rbp),%rcx
  30c257:	e9 6d fe ff ff       	jmp    30c0c9 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0xed9>
  30c25c:	49 8b 34 24          	mov    (%r12),%rsi
  30c260:	48 83 be 68 08 00 00 	cmpq   $0x0,0x868(%rsi)
  30c267:	00 
  30c268:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30c26f:	74 5c                	je     30c2cd <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10dd>
  30c271:	48 8b 86 48 08 00 00 	mov    0x848(%rsi),%rax
  30c278:	48 8b 96 60 08 00 00 	mov    0x860(%rsi),%rdx
  30c27f:	49 89 d9             	mov    %rbx,%r9
  30c282:	48 bb c1 c0 c0 c0 c0 	movabs $0xc0c0c0c0c0c0c0c1,%rbx
  30c289:	c0 c0 c0 
  30c28c:	c4 e2 f3 f6 cb       	mulx   %rbx,%rcx,%rcx
  30c291:	48 c1 e9 07          	shr    $0x7,%rcx
  30c295:	48 8b 04 c8          	mov    (%rax,%rcx,8),%rax
  30c299:	48 69 c9 aa 00 00 00 	imul   $0xaa,%rcx,%rcx
  30c2a0:	48 29 ca             	sub    %rcx,%rdx
  30c2a3:	49 8b 75 08          	mov    0x8(%r13),%rsi
  30c2a7:	48 89 f7             	mov    %rsi,%rdi
  30c2aa:	49 2b 7d 00          	sub    0x0(%r13),%rdi
  30c2ae:	48 8d 0c 52          	lea    (%rdx,%rdx,2),%rcx
  30c2b2:	48 8b 14 c8          	mov    (%rax,%rcx,8),%rdx
  30c2b6:	48 8b 4c c8 08       	mov    0x8(%rax,%rcx,8),%rcx
  30c2bb:	49 89 c8             	mov    %rcx,%r8
  30c2be:	49 29 d0             	sub    %rdx,%r8
  30c2c1:	4c 01 c7             	add    %r8,%rdi
  30c2c4:	4c 39 cf             	cmp    %r9,%rdi
  30c2c7:	0f 86 17 07 00 00    	jbe    30c9e4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x17f4>
  30c2cd:	45 31 ff             	xor    %r15d,%r15d
  30c2d0:	80 bd af f9 ff ff 01 	cmpb   $0x1,-0x651(%rbp)
  30c2d7:	75 40                	jne    30c319 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1129>
  30c2d9:	48 8b 85 68 f9 ff ff 	mov    -0x698(%rbp),%rax
  30c2e0:	0f b6 40 14          	movzbl 0x14(%rax),%eax
  30c2e4:	48 8d 48 15          	lea    0x15(%rax),%rcx
  30c2e8:	48 39 8d 58 f9 ff ff 	cmp    %rcx,-0x6a8(%rbp)
  30c2ef:	76 28                	jbe    30c319 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1129>
  30c2f1:	48 8b b5 58 f9 ff ff 	mov    -0x6a8(%rbp),%rsi
  30c2f8:	48 29 c6             	sub    %rax,%rsi
  30c2fb:	48 83 c6 eb          	add    $0xffffffffffffffeb,%rsi
  30c2ff:	49 8b 45 08          	mov    0x8(%r13),%rax
  30c303:	49 2b 45 00          	sub    0x0(%r13),%rax
  30c307:	48 39 f0             	cmp    %rsi,%rax
  30c30a:	73 0d                	jae    30c319 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1129>
  30c30c:	4c 89 ef             	mov    %r13,%rdi
  30c30f:	31 d2                	xor    %edx,%edx
  30c311:	c5 f8 77             	vzeroupper
  30c314:	e8 37 50 00 00       	call   311350 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::resize(unsigned long, std::byte const&)>
  30c319:	49 8b 55 00          	mov    0x0(%r13),%rdx
  30c31d:	49 8b 4d 08          	mov    0x8(%r13),%rcx
  30c321:	48 39 ca             	cmp    %rcx,%rdx
  30c324:	0f 84 88 06 00 00    	je     30c9b2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x17c2>
  30c32a:	48 8b b5 40 ff ff ff 	mov    -0xc0(%rbp),%rsi
  30c331:	48 8b bd 48 ff ff ff 	mov    -0xb8(%rbp),%rdi
  30c338:	48 89 f0             	mov    %rsi,%rax
  30c33b:	48 31 f8             	xor    %rdi,%rax
  30c33e:	48 0b 45 88          	or     -0x78(%rbp),%rax
  30c342:	0f 95 c0             	setne  %al
  30c345:	48 31 f7             	xor    %rsi,%rdi
  30c348:	44 89 bd 98 f9 ff ff 	mov    %r15d,-0x668(%rbp)
  30c34f:	74 20                	je     30c371 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1181>
  30c351:	0f b6 36             	movzbl (%rsi),%esi
  30c354:	40 80 e6 fe          	and    $0xfe,%sil
  30c358:	40 80 fe 1c          	cmp    $0x1c,%sil
  30c35c:	75 13                	jne    30c371 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1181>
  30c35e:	45 31 ff             	xor    %r15d,%r15d
  30c361:	44 8b 8d 74 f9 ff ff 	mov    -0x68c(%rbp),%r9d
  30c368:	4c 8b b5 68 f9 ff ff 	mov    -0x698(%rbp),%r14
  30c36f:	eb 53                	jmp    30c3c4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11d4>
  30c371:	48 8b 75 b8          	mov    -0x48(%rbp),%rsi
  30c375:	48 8b 7d c0          	mov    -0x40(%rbp),%rdi
  30c379:	48 39 fe             	cmp    %rdi,%rsi
  30c37c:	44 8b 8d 74 f9 ff ff 	mov    -0x68c(%rbp),%r9d
  30c383:	4c 8b b5 68 f9 ff ff 	mov    -0x698(%rbp),%r14
  30c38a:	75 16                	jne    30c3a2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11b2>
  30c38c:	41 b7 01             	mov    $0x1,%r15b
  30c38f:	eb 33                	jmp    30c3c4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11d4>
  30c391:	45 31 ff             	xor    %r15d,%r15d
  30c394:	45 84 ff             	test   %r15b,%r15b
  30c397:	75 27                	jne    30c3c0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11d0>
  30c399:	48 83 c6 78          	add    $0x78,%rsi
  30c39d:	48 39 fe             	cmp    %rdi,%rsi
  30c3a0:	74 1e                	je     30c3c0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11d0>
  30c3a2:	4c 8b 06             	mov    (%rsi),%r8
  30c3a5:	4c 3b 46 08          	cmp    0x8(%rsi),%r8
  30c3a9:	74 e6                	je     30c391 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11a1>
  30c3ab:	45 0f b6 00          	movzbl (%r8),%r8d
  30c3af:	41 80 e0 fe          	and    $0xfe,%r8b
  30c3b3:	41 80 f8 1c          	cmp    $0x1c,%r8b
  30c3b7:	41 0f 94 c7          	sete   %r15b
  30c3bb:	45 84 ff             	test   %r15b,%r15b
  30c3be:	74 d9                	je     30c399 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x11a9>
  30c3c0:	41 80 f7 01          	xor    $0x1,%r15b
  30c3c4:	41 0f b6 76 14       	movzbl 0x14(%r14),%esi
  30c3c9:	48 8b bd 50 f9 ff ff 	mov    -0x6b0(%rbp),%rdi
  30c3d0:	48 8b 7f 78          	mov    0x78(%rdi),%rdi
  30c3d4:	48 29 d1             	sub    %rdx,%rcx
  30c3d7:	48 01 f1             	add    %rsi,%rcx
  30c3da:	4c 8d 04 0f          	lea    (%rdi,%rcx,1),%r8
  30c3de:	49 83 c0 05          	add    $0x5,%r8
  30c3e2:	49 8b 0c 24          	mov    (%r12),%rcx
  30c3e6:	83 79 60 01          	cmpl   $0x1,0x60(%rcx)
  30c3ea:	75 49                	jne    30c435 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1245>
  30c3ec:	48 8b 8d 80 f9 ff ff 	mov    -0x680(%rbp),%rcx
  30c3f3:	80 b9 82 00 00 00 00 	cmpb   $0x0,0x82(%rcx)
  30c3fa:	75 39                	jne    30c435 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1245>
  30c3fc:	48 8b b5 80 f9 ff ff 	mov    -0x680(%rbp),%rsi
  30c403:	48 8b 8e 98 00 00 00 	mov    0x98(%rsi),%rcx
  30c40a:	48 ba 55 55 55 55 55 	movabs $0x5555555555555555,%rdx
  30c411:	55 55 55 
  30c414:	48 39 d1             	cmp    %rdx,%rcx
  30c417:	48 0f 42 d1          	cmovb  %rcx,%rdx
  30c41b:	48 8d 0c 52          	lea    (%rdx,%rdx,2),%rcx
  30c41f:	48 2b 8e a0 00 00 00 	sub    0xa0(%rsi),%rcx
  30c426:	0f 82 a8 07 00 00    	jb     30cbd4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x19e4>
  30c42c:	49 39 c8             	cmp    %rcx,%r8
  30c42f:	0f 87 9f 07 00 00    	ja     30cbd4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x19e4>
  30c435:	45 84 c9             	test   %r9b,%r9b
  30c438:	75 3a                	jne    30c474 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1284>
  30c43a:	0a 85 98 f9 ff ff    	or     -0x668(%rbp),%al
  30c440:	0a 85 af f9 ff ff    	or     -0x651(%rbp),%al
  30c446:	44 20 f8             	and    %r15b,%al
  30c449:	3c 01                	cmp    $0x1,%al
  30c44b:	75 27                	jne    30c474 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1284>
  30c44d:	48 8b 85 80 f9 ff ff 	mov    -0x680(%rbp),%rax
  30c454:	48 8d b8 e0 00 00 00 	lea    0xe0(%rax),%rdi
  30c45b:	4c 89 c6             	mov    %r8,%rsi
  30c45e:	4c 89 c3             	mov    %r8,%rbx
  30c461:	c5 f8 77             	vzeroupper
  30c464:	e8 f7 01 04 00       	call   34c660 <cnetmod::quic::quic_recovery_controller@cnetmod.protocol.quic::can_send_datagram(unsigned long) const>
  30c469:	49 89 d8             	mov    %rbx,%r8
  30c46c:	84 c0                	test   %al,%al
  30c46e:	0f 84 16 0b 00 00    	je     30cf8a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1d9a>
  30c474:	48 8b 8d 80 f9 ff ff 	mov    -0x680(%rbp),%rcx
  30c47b:	48 8b 81 a8 00 00 00 	mov    0xa8(%rcx),%rax
  30c482:	48 89 85 a0 f9 ff ff 	mov    %rax,-0x660(%rbp)
  30c489:	48 ff c0             	inc    %rax
  30c48c:	48 89 81 a8 00 00 00 	mov    %rax,0xa8(%rcx)
  30c493:	48 8b 8d 78 f9 ff ff 	mov    -0x688(%rbp),%rcx
  30c49a:	48 8b 11             	mov    (%rcx),%rdx
  30c49d:	48 89 95 d0 fd ff ff 	mov    %rdx,-0x230(%rbp)
  30c4a4:	48 8b 59 08          	mov    0x8(%rcx),%rbx
  30c4a8:	48 89 9d d8 fd ff ff 	mov    %rbx,-0x228(%rbp)
  30c4af:	48 8b 41 10          	mov    0x10(%rcx),%rax
  30c4b3:	48 89 85 e0 fd ff ff 	mov    %rax,-0x220(%rbp)
  30c4ba:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30c4be:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30c4c2:	48 c7 41 10 00 00 00 	movq   $0x0,0x10(%rcx)
  30c4c9:	00 
  30c4ca:	49 89 c5             	mov    %rax,%r13
  30c4cd:	48 89 95 60 f9 ff ff 	mov    %rdx,-0x6a0(%rbp)
  30c4d4:	49 29 d5             	sub    %rdx,%r13
  30c4d7:	4d 39 e8             	cmp    %r13,%r8
  30c4da:	0f 86 90 00 00 00    	jbe    30c570 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1380>
  30c4e0:	4d 85 c0             	test   %r8,%r8
  30c4e3:	0f 88 6e 0e 00 00    	js     30d357 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2167>
  30c4e9:	4c 89 c7             	mov    %r8,%rdi
  30c4ec:	4c 89 85 78 f9 ff ff 	mov    %r8,-0x688(%rbp)
  30c4f3:	c5 f8 77             	vzeroupper
  30c4f6:	e8 15 e5 dd ff       	call   eaa10 <_Znwm$plt>
  30c4fb:	48 8b b5 60 f9 ff ff 	mov    -0x6a0(%rbp),%rsi
  30c502:	48 29 f3             	sub    %rsi,%rbx
  30c505:	48 8d 0c 18          	lea    (%rax,%rbx,1),%rcx
  30c509:	48 89 8d 50 f9 ff ff 	mov    %rcx,-0x6b0(%rbp)
  30c510:	4c 8b b5 78 f9 ff ff 	mov    -0x688(%rbp),%r14
  30c517:	49 01 c6             	add    %rax,%r14
  30c51a:	48 89 c7             	mov    %rax,%rdi
  30c51d:	48 89 da             	mov    %rbx,%rdx
  30c520:	48 89 c3             	mov    %rax,%rbx
  30c523:	e8 58 e9 dd ff       	call   eae80 <memcpy$plt>
  30c528:	48 8b bd 60 f9 ff ff 	mov    -0x6a0(%rbp),%rdi
  30c52f:	48 89 9d d0 fd ff ff 	mov    %rbx,-0x230(%rbp)
  30c536:	48 8b 9d 50 f9 ff ff 	mov    -0x6b0(%rbp),%rbx
  30c53d:	48 89 9d d8 fd ff ff 	mov    %rbx,-0x228(%rbp)
  30c544:	4c 89 b5 e0 fd ff ff 	mov    %r14,-0x220(%rbp)
  30c54b:	4c 89 f0             	mov    %r14,%rax
  30c54e:	48 85 ff             	test   %rdi,%rdi
  30c551:	4c 8b b5 68 f9 ff ff 	mov    -0x698(%rbp),%r14
  30c558:	74 16                	je     30c570 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1380>
  30c55a:	4c 89 ee             	mov    %r13,%rsi
  30c55d:	e8 3e e4 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30c562:	48 8b 9d d8 fd ff ff 	mov    -0x228(%rbp),%rbx
  30c569:	48 8b 85 e0 fd ff ff 	mov    -0x220(%rbp),%rax
  30c570:	49 8b 0c 24          	mov    (%r12),%rcx
  30c574:	48 8b 89 58 03 00 00 	mov    0x358(%rcx),%rcx
  30c57b:	0f b6 89 59 06 00 00 	movzbl 0x659(%rcx),%ecx
  30c582:	c0 e1 02             	shl    $0x2,%cl
  30c585:	80 c9 43             	or     $0x43,%cl
  30c588:	88 8d f0 f9 ff ff    	mov    %cl,-0x610(%rbp)
  30c58e:	48 89 9d 90 fa ff ff 	mov    %rbx,-0x570(%rbp)
  30c595:	48 8d 95 90 fa ff ff 	lea    -0x570(%rbp),%rdx
  30c59c:	48 89 95 40 fd ff ff 	mov    %rdx,-0x2c0(%rbp)
  30c5a3:	4c 8d a5 f0 f9 ff ff 	lea    -0x610(%rbp),%r12
  30c5aa:	4c 89 a5 48 fd ff ff 	mov    %r12,-0x2b8(%rbp)
  30c5b1:	48 8d 95 d0 fd ff ff 	lea    -0x230(%rbp),%rdx
  30c5b8:	48 89 95 50 fd ff ff 	mov    %rdx,-0x2b0(%rbp)
  30c5bf:	48 39 c3             	cmp    %rax,%rbx
  30c5c2:	0f 83 cb 0c 00 00    	jae    30d293 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x20a3>
  30c5c8:	88 0b                	mov    %cl,(%rbx)
  30c5ca:	48 ff c3             	inc    %rbx
  30c5cd:	48 89 9d d8 fd ff ff 	mov    %rbx,-0x228(%rbp)
  30c5d4:	45 0f b6 46 14       	movzbl 0x14(%r14),%r8d
  30c5d9:	4b 8d 0c 06          	lea    (%r14,%r8,1),%rcx
  30c5dd:	4c 8d ad d0 fd ff ff 	lea    -0x230(%rbp),%r13
  30c5e4:	4c 89 ef             	mov    %r13,%rdi
  30c5e7:	48 89 de             	mov    %rbx,%rsi
  30c5ea:	4c 89 f2             	mov    %r14,%rdx
  30c5ed:	c5 f8 77             	vzeroupper
  30c5f0:	e8 1b df fa ff       	call   2ba510 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::byte const*, std::byte const*>(std::__1::__wrap_iter<std::byte const*>, std::byte const*, std::byte const*, long)>
  30c5f5:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30c5fc:	44 89 c8             	mov    %r9d,%eax
  30c5ff:	c1 e8 18             	shr    $0x18,%eax
  30c602:	88 85 f0 f9 ff ff    	mov    %al,-0x610(%rbp)
  30c608:	4c 8b b5 d8 fd ff ff 	mov    -0x228(%rbp),%r14
  30c60f:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c616:	48 8d 8d 90 fa ff ff 	lea    -0x570(%rbp),%rcx
  30c61d:	48 89 8d 40 fd ff ff 	mov    %rcx,-0x2c0(%rbp)
  30c624:	4c 89 a5 48 fd ff ff 	mov    %r12,-0x2b8(%rbp)
  30c62b:	4c 89 ad 50 fd ff ff 	mov    %r13,-0x2b0(%rbp)
  30c632:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c639:	0f 83 76 0c 00 00    	jae    30d2b5 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x20c5>
  30c63f:	41 88 06             	mov    %al,(%r14)
  30c642:	49 ff c6             	inc    %r14
  30c645:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c64c:	44 89 c8             	mov    %r9d,%eax
  30c64f:	c1 e8 10             	shr    $0x10,%eax
  30c652:	88 85 f0 f9 ff ff    	mov    %al,-0x610(%rbp)
  30c658:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c65f:	48 8d 8d 90 fa ff ff 	lea    -0x570(%rbp),%rcx
  30c666:	48 89 8d 40 fd ff ff 	mov    %rcx,-0x2c0(%rbp)
  30c66d:	4c 89 a5 48 fd ff ff 	mov    %r12,-0x2b8(%rbp)
  30c674:	4c 89 ad 50 fd ff ff 	mov    %r13,-0x2b0(%rbp)
  30c67b:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c682:	0f 83 4c 0c 00 00    	jae    30d2d4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x20e4>
  30c688:	41 88 06             	mov    %al,(%r14)
  30c68b:	49 ff c6             	inc    %r14
  30c68e:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c695:	44 89 c8             	mov    %r9d,%eax
  30c698:	c1 e8 08             	shr    $0x8,%eax
  30c69b:	88 85 f0 f9 ff ff    	mov    %al,-0x610(%rbp)
  30c6a1:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c6a8:	48 8d 8d 90 fa ff ff 	lea    -0x570(%rbp),%rcx
  30c6af:	48 89 8d 40 fd ff ff 	mov    %rcx,-0x2c0(%rbp)
  30c6b6:	4c 89 a5 48 fd ff ff 	mov    %r12,-0x2b8(%rbp)
  30c6bd:	4c 89 ad 50 fd ff ff 	mov    %r13,-0x2b0(%rbp)
  30c6c4:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c6cb:	0f 83 22 0c 00 00    	jae    30d2f3 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2103>
  30c6d1:	41 88 06             	mov    %al,(%r14)
  30c6d4:	49 ff c6             	inc    %r14
  30c6d7:	4c 89 b5 d8 fd ff ff 	mov    %r14,-0x228(%rbp)
  30c6de:	44 88 8d f0 f9 ff ff 	mov    %r9b,-0x610(%rbp)
  30c6e5:	4c 89 b5 90 fa ff ff 	mov    %r14,-0x570(%rbp)
  30c6ec:	48 8d 85 90 fa ff ff 	lea    -0x570(%rbp),%rax
  30c6f3:	48 89 85 40 fd ff ff 	mov    %rax,-0x2c0(%rbp)
  30c6fa:	4c 89 a5 48 fd ff ff 	mov    %r12,-0x2b8(%rbp)
  30c701:	4c 89 ad 50 fd ff ff 	mov    %r13,-0x2b0(%rbp)
  30c708:	4c 3b b5 e0 fd ff ff 	cmp    -0x220(%rbp),%r14
  30c70f:	0f 83 fd 0b 00 00    	jae    30d312 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2122>
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
  30c79a:	80 bd 50 fd ff ff 00 	cmpb   $0x0,-0x2b0(%rbp)
  30c7a1:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30c7a8:	0f 84 99 07 00 00    	je     30cf47 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1d57>
  30c7ae:	48 8b 85 d8 fd ff ff 	mov    -0x228(%rbp),%rax
  30c7b5:	48 2b 85 d0 fd ff ff 	sub    -0x230(%rbp),%rax
  30c7bc:	48 8b 95 80 f9 ff ff 	mov    -0x680(%rbp),%rdx
  30c7c3:	48 03 82 a0 00 00 00 	add    0xa0(%rdx),%rax
  30c7ca:	48 c7 c1 ff ff ff ff 	mov    $0xffffffffffffffff,%rcx
  30c7d1:	48 0f 43 c8          	cmovae %rax,%rcx
  30c7d5:	48 89 8a a0 00 00 00 	mov    %rcx,0xa0(%rdx)
  30c7dc:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30c7e3:	75 0c                	jne    30c7f1 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1601>
  30c7e5:	48 8b bd 40 f9 ff ff 	mov    -0x6c0(%rbp),%rdi
  30c7ec:	e8 ff 44 ff ff       	call   300cf0 <std::__1::set<unsigned long, std::__1::less<unsigned long>, std::__1::allocator<unsigned long> >::clear[abi:nqe220108]()>
  30c7f1:	80 bd af f9 ff ff 01 	cmpb   $0x1,-0x651(%rbp)
  30c7f8:	4c 8b a5 80 f9 ff ff 	mov    -0x680(%rbp),%r12
  30c7ff:	75 2c                	jne    30c82d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x163d>
  30c801:	48 8b 85 58 f9 ff ff 	mov    -0x6a8(%rbp),%rax
  30c808:	49 89 84 24 70 1c 00 	mov    %rax,0x1c70(%r12)
  30c80f:	00 
  30c810:	41 c6 84 24 78 1c 00 	movb   $0x1,0x1c78(%r12)
  30c817:	00 01 
  30c819:	41 80 bc 24 68 1c 00 	cmpb   $0x1,0x1c68(%r12)
  30c820:	00 01 
  30c822:	75 09                	jne    30c82d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x163d>
  30c824:	41 c6 84 24 68 1c 00 	movb   $0x0,0x1c68(%r12)
  30c82b:	00 00 
  30c82d:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
  30c834:	48 33 85 48 ff ff ff 	xor    -0xb8(%rbp),%rax
  30c83b:	48 0b 45 88          	or     -0x78(%rbp),%rax
  30c83f:	0f 95 c0             	setne  %al
  30c842:	44 0f b6 ad af f9 ff 	movzbl -0x651(%rbp),%r13d
  30c849:	ff 
  30c84a:	8b 8d 98 f9 ff ff    	mov    -0x668(%rbp),%ecx
  30c850:	44 08 e9             	or     %r13b,%cl
  30c853:	08 c1                	or     %al,%cl
  30c855:	41 20 cf             	and    %cl,%r15b
  30c858:	49 81 c4 e0 00 00 00 	add    $0xe0,%r12
  30c85f:	48 8b 9d d8 fd ff ff 	mov    -0x228(%rbp),%rbx
  30c866:	48 2b 9d d0 fd ff ff 	sub    -0x230(%rbp),%rbx
  30c86d:	e8 ae da dd ff       	call   ea320 <_ZNSt3__16chrono12steady_clock3nowEv$plt>
  30c872:	c5 f8 28 85 40 ff ff 	vmovaps -0xc0(%rbp),%xmm0
  30c879:	ff 
  30c87a:	c5 f8 29 85 a0 fa ff 	vmovaps %xmm0,-0x560(%rbp)
  30c881:	ff 
  30c882:	48 8b 8d 50 ff ff ff 	mov    -0xb0(%rbp),%rcx
  30c889:	48 89 8d b0 fa ff ff 	mov    %rcx,-0x550(%rbp)
  30c890:	c5 f8 57 c0          	vxorps %xmm0,%xmm0,%xmm0
  30c894:	c5 f8 29 85 40 ff ff 	vmovaps %xmm0,-0xc0(%rbp)
  30c89b:	ff 
  30c89c:	48 c7 85 50 ff ff ff 	movq   $0x0,-0xb0(%rbp)
  30c8a3:	00 00 00 00 
  30c8a7:	c5 f8 10 8d 58 ff ff 	vmovups -0xa8(%rbp),%xmm1
  30c8ae:	ff 
  30c8af:	c5 f8 11 8d b8 fa ff 	vmovups %xmm1,-0x548(%rbp)
  30c8b6:	ff 
  30c8b7:	c5 f8 11 85 58 ff ff 	vmovups %xmm0,-0xa8(%rbp)
  30c8be:	ff 
  30c8bf:	c5 fc 10 8d 68 ff ff 	vmovups -0x98(%rbp),%ymm1
  30c8c6:	ff 
  30c8c7:	c5 fc 11 8d c8 fa ff 	vmovups %ymm1,-0x538(%rbp)
  30c8ce:	ff 
  30c8cf:	c5 fc 10 4d 88       	vmovups -0x78(%rbp),%ymm1
  30c8d4:	c5 fc 11 8d e8 fa ff 	vmovups %ymm1,-0x518(%rbp)
  30c8db:	ff 
  30c8dc:	c5 fc 10 4d 93       	vmovups -0x6d(%rbp),%ymm1
  30c8e1:	c5 fc 11 8d f3 fa ff 	vmovups %ymm1,-0x50d(%rbp)
  30c8e8:	ff 
  30c8e9:	c5 f8 10 4d b8       	vmovups -0x48(%rbp),%xmm1
  30c8ee:	c5 f8 11 8d 18 fb ff 	vmovups %xmm1,-0x4e8(%rbp)
  30c8f5:	ff 
  30c8f6:	48 8b 4d c8          	mov    -0x38(%rbp),%rcx
  30c8fa:	48 89 8d 28 fb ff ff 	mov    %rcx,-0x4d8(%rbp)
  30c901:	48 8d 4d b8          	lea    -0x48(%rbp),%rcx
  30c905:	c5 f8 11 01          	vmovups %xmm0,(%rcx)
  30c909:	48 c7 41 10 00 00 00 	movq   $0x0,0x10(%rcx)
  30c910:	00 
  30c911:	48 8b 8d 58 f9 ff ff 	mov    -0x6a8(%rbp),%rcx
  30c918:	48 89 8d f8 f9 ff ff 	mov    %rcx,-0x608(%rbp)
  30c91f:	44 88 ad 00 fa ff ff 	mov    %r13b,-0x600(%rbp)
  30c926:	8b 8d 08 fa ff ff    	mov    -0x5f8(%rbp),%ecx
  30c92c:	8b 95 0b fa ff ff    	mov    -0x5f5(%rbp),%edx
  30c932:	89 8d 01 fa ff ff    	mov    %ecx,-0x5ff(%rbp)
  30c938:	89 95 04 fa ff ff    	mov    %edx,-0x5fc(%rbp)
  30c93e:	c5 fa 6f 85 f8 f9 ff 	vmovdqu -0x608(%rbp),%xmm0
  30c945:	ff 
  30c946:	c5 fa 7f 44 24 08    	vmovdqu %xmm0,0x8(%rsp)
  30c94c:	0f b6 8d 74 f9 ff ff 	movzbl -0x68c(%rbp),%ecx
  30c953:	89 4c 24 18          	mov    %ecx,0x18(%rsp)
  30c957:	48 8d 8d a0 fa ff ff 	lea    -0x560(%rbp),%rcx
  30c95e:	48 89 0c 24          	mov    %rcx,(%rsp)
  30c962:	45 0f b6 c7          	movzbl %r15b,%r8d
  30c966:	4c 89 e7             	mov    %r12,%rdi
  30c969:	48 8b b5 a0 f9 ff ff 	mov    -0x660(%rbp),%rsi
  30c970:	48 89 da             	mov    %rbx,%rdx
  30c973:	48 89 c1             	mov    %rax,%rcx
  30c976:	41 b9 02 00 00 00    	mov    $0x2,%r9d
  30c97c:	c5 f8 77             	vzeroupper
  30c97f:	e8 cc ce 03 00       	call   349850 <cnetmod::quic::quic_recovery_controller@cnetmod.protocol.quic::on_packet_sent(unsigned long, unsigned long, std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >, bool, cnetmod::quic::pn_space@cnetmod.protocol.quic, cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic, std::__1::optional<unsigned long>, bool)>
  30c984:	48 8d bd a0 fa ff ff 	lea    -0x560(%rbp),%rdi
  30c98b:	e8 a0 3a 00 00       	call   310430 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30c990:	c5 f9 6f 85 d0 fd ff 	vmovdqa -0x230(%rbp),%xmm0
  30c997:	ff 
  30c998:	c4 c1 7a 7f 06       	vmovdqu %xmm0,(%r14)
  30c99d:	48 8b 85 e0 fd ff ff 	mov    -0x220(%rbp),%rax
  30c9a4:	49 89 46 10          	mov    %rax,0x10(%r14)
  30c9a8:	41 c6 46 18 01       	movb   $0x1,0x18(%r14)
  30c9ad:	e9 82 06 00 00       	jmp    30d034 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e44>
  30c9b2:	48 8b 8d 78 f9 ff ff 	mov    -0x688(%rbp),%rcx
  30c9b9:	c5 fa 6f 01          	vmovdqu (%rcx),%xmm0
  30c9bd:	c4 c1 7a 7f 06       	vmovdqu %xmm0,(%r14)
  30c9c2:	48 8b 41 10          	mov    0x10(%rcx),%rax
  30c9c6:	49 89 46 10          	mov    %rax,0x10(%r14)
  30c9ca:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30c9ce:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30c9d2:	48 c7 41 10 00 00 00 	movq   $0x0,0x10(%rcx)
  30c9d9:	00 
  30c9da:	41 c6 46 18 01       	movb   $0x1,0x18(%r14)
  30c9df:	e9 50 06 00 00       	jmp    30d034 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e44>
  30c9e4:	4c 89 ef             	mov    %r13,%rdi
  30c9e7:	c5 f8 77             	vzeroupper
  30c9ea:	e8 31 e1 fa ff       	call   2bab20 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30c9ef:	4d 8b 34 24          	mov    (%r12),%r14
  30c9f3:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30c9fa:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30ca01:	c4 e2 f3 f6 cb       	mulx   %rbx,%rcx,%rcx
  30ca06:	48 c1 e9 07          	shr    $0x7,%rcx
  30ca0a:	48 8b 04 c8          	mov    (%rax,%rcx,8),%rax
  30ca0e:	48 69 c9 aa 00 00 00 	imul   $0xaa,%rcx,%rcx
  30ca15:	48 89 d6             	mov    %rdx,%rsi
  30ca18:	48 29 ce             	sub    %rcx,%rsi
  30ca1b:	48 8d 0c 76          	lea    (%rsi,%rsi,2),%rcx
  30ca1f:	48 8b 3c c8          	mov    (%rax,%rcx,8),%rdi
  30ca23:	48 85 ff             	test   %rdi,%rdi
  30ca26:	74 1b                	je     30ca43 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1853>
  30ca28:	48 8d 04 c8          	lea    (%rax,%rcx,8),%rax
  30ca2c:	48 89 78 08          	mov    %rdi,0x8(%rax)
  30ca30:	48 8b 70 10          	mov    0x10(%rax),%rsi
  30ca34:	48 29 fe             	sub    %rdi,%rsi
  30ca37:	e8 64 df dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30ca3c:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30ca43:	49 ff 8e 68 08 00 00 	decq   0x868(%r14)
  30ca4a:	48 ff c2             	inc    %rdx
  30ca4d:	49 89 96 60 08 00 00 	mov    %rdx,0x860(%r14)
  30ca54:	48 81 fa 54 01 00 00 	cmp    $0x154,%rdx
  30ca5b:	72 27                	jb     30ca84 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1894>
  30ca5d:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30ca64:	48 8b 38             	mov    (%rax),%rdi
  30ca67:	be f0 0f 00 00       	mov    $0xff0,%esi
  30ca6c:	e8 2f df dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30ca71:	49 83 86 48 08 00 00 	addq   $0x8,0x848(%r14)
  30ca78:	08 
  30ca79:	49 81 86 60 08 00 00 	addq   $0xffffffffffffff56,0x860(%r14)
  30ca80:	56 ff ff ff 
  30ca84:	49 8b 04 24          	mov    (%r12),%rax
  30ca88:	41 b7 01             	mov    $0x1,%r15b
  30ca8b:	48 83 b8 68 08 00 00 	cmpq   $0x0,0x868(%rax)
  30ca92:	00 
  30ca93:	0f 84 a2 04 00 00    	je     30cf3b <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1d4b>
  30ca99:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30caa0:	eb 19                	jmp    30cabb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x18cb>
  30caa2:	49 8b 04 24          	mov    (%r12),%rax
  30caa6:	48 83 b8 68 08 00 00 	cmpq   $0x0,0x868(%rax)
  30caad:	00 
  30caae:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30cab5:	0f 84 15 f8 ff ff    	je     30c2d0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10e0>
  30cabb:	48 8b 88 48 08 00 00 	mov    0x848(%rax),%rcx
  30cac2:	48 8b 90 60 08 00 00 	mov    0x860(%rax),%rdx
  30cac9:	c4 e2 fb f6 c3       	mulx   %rbx,%rax,%rax
  30cace:	48 c1 e8 07          	shr    $0x7,%rax
  30cad2:	48 8b 0c c1          	mov    (%rcx,%rax,8),%rcx
  30cad6:	48 69 c0 aa 00 00 00 	imul   $0xaa,%rax,%rax
  30cadd:	48 29 c2             	sub    %rax,%rdx
  30cae0:	49 8b 75 08          	mov    0x8(%r13),%rsi
  30cae4:	48 89 f0             	mov    %rsi,%rax
  30cae7:	49 2b 45 00          	sub    0x0(%r13),%rax
  30caeb:	48 8d 3c 52          	lea    (%rdx,%rdx,2),%rdi
  30caef:	48 8b 14 f9          	mov    (%rcx,%rdi,8),%rdx
  30caf3:	48 8b 4c f9 08       	mov    0x8(%rcx,%rdi,8),%rcx
  30caf8:	49 89 c8             	mov    %rcx,%r8
  30cafb:	49 29 d0             	sub    %rdx,%r8
  30cafe:	4c 01 c0             	add    %r8,%rax
  30cb01:	48 3b 85 98 f9 ff ff 	cmp    -0x668(%rbp),%rax
  30cb08:	0f 87 c2 f7 ff ff    	ja     30c2d0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10e0>
  30cb0e:	4c 89 ef             	mov    %r13,%rdi
  30cb11:	e8 0a e0 fa ff       	call   2bab20 <std::__1::__wrap_iter<std::byte*> std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__insert_with_size[abi:nqe220108]<std::__1::_ClassicAlgPolicy, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*> >(std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, std::__1::__wrap_iter<std::byte const*>, long)>
  30cb16:	4d 8b 34 24          	mov    (%r12),%r14
  30cb1a:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30cb21:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30cb28:	c4 e2 f3 f6 cb       	mulx   %rbx,%rcx,%rcx
  30cb2d:	48 c1 e9 07          	shr    $0x7,%rcx
  30cb31:	48 8b 04 c8          	mov    (%rax,%rcx,8),%rax
  30cb35:	48 69 c9 aa 00 00 00 	imul   $0xaa,%rcx,%rcx
  30cb3c:	48 89 d6             	mov    %rdx,%rsi
  30cb3f:	48 29 ce             	sub    %rcx,%rsi
  30cb42:	48 8d 0c 76          	lea    (%rsi,%rsi,2),%rcx
  30cb46:	48 8b 3c c8          	mov    (%rax,%rcx,8),%rdi
  30cb4a:	48 85 ff             	test   %rdi,%rdi
  30cb4d:	74 1b                	je     30cb6a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x197a>
  30cb4f:	48 8d 04 c8          	lea    (%rax,%rcx,8),%rax
  30cb53:	48 89 78 08          	mov    %rdi,0x8(%rax)
  30cb57:	48 8b 70 10          	mov    0x10(%rax),%rsi
  30cb5b:	48 29 fe             	sub    %rdi,%rsi
  30cb5e:	e8 3d de dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30cb63:	49 8b 96 60 08 00 00 	mov    0x860(%r14),%rdx
  30cb6a:	49 ff 8e 68 08 00 00 	decq   0x868(%r14)
  30cb71:	48 ff c2             	inc    %rdx
  30cb74:	49 89 96 60 08 00 00 	mov    %rdx,0x860(%r14)
  30cb7b:	48 81 fa 54 01 00 00 	cmp    $0x154,%rdx
  30cb82:	0f 82 1a ff ff ff    	jb     30caa2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x18b2>
  30cb88:	49 8b 86 48 08 00 00 	mov    0x848(%r14),%rax
  30cb8f:	48 8b 38             	mov    (%rax),%rdi
  30cb92:	be f0 0f 00 00       	mov    $0xff0,%esi
  30cb97:	e8 04 de dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30cb9c:	49 83 86 48 08 00 00 	addq   $0x8,0x848(%r14)
  30cba3:	08 
  30cba4:	49 81 86 60 08 00 00 	addq   $0xffffffffffffff56,0x860(%r14)
  30cbab:	56 ff ff ff 
  30cbaf:	e9 ee fe ff ff       	jmp    30caa2 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x18b2>
  30cbb4:	8b 9d 40 fd ff ff    	mov    -0x2c0(%rbp),%ebx
  30cbba:	85 db                	test   %ebx,%ebx
  30cbbc:	0f 84 a5 03 00 00    	je     30cf67 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1d77>
  30cbc2:	8b 8d 44 fd ff ff    	mov    -0x2bc(%rbp),%ecx
  30cbc8:	48 8b 85 48 fd ff ff 	mov    -0x2b8(%rbp),%rax
  30cbcf:	e9 9d 03 00 00       	jmp    30cf71 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1d81>
  30cbd4:	84 c0                	test   %al,%al
  30cbd6:	0f 84 e8 03 00 00    	je     30cfc4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1dd4>
  30cbdc:	4c 8b 75 c0          	mov    -0x40(%rbp),%r14
  30cbe0:	4c 2b 75 b8          	sub    -0x48(%rbp),%r14
  30cbe4:	49 c1 fe 03          	sar    $0x3,%r14
  30cbe8:	48 b8 ef ee ee ee ee 	movabs $0xeeeeeeeeeeeeeeef,%rax
  30cbef:	ee ee ee 
  30cbf2:	4c 0f af f0          	imul   %rax,%r14
  30cbf6:	49 ff c6             	inc    %r14
  30cbf9:	0f 84 c5 03 00 00    	je     30cfc4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1dd4>
  30cbff:	4c 8d bd f0 fb ff ff 	lea    -0x410(%rbp),%r15
  30cc06:	4c 8b ad 10 fa ff ff 	mov    -0x5f0(%rbp),%r13
  30cc0d:	e9 dd 00 00 00       	jmp    30ccef <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1aff>
  30cc12:	48 8d 4b 90          	lea    -0x70(%rbx),%rcx
  30cc16:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30cc1a:	c5 fa 7f 40 88       	vmovdqu %xmm0,-0x78(%rax)
  30cc1f:	48 8d 53 98          	lea    -0x68(%rbx),%rdx
  30cc23:	48 c7 40 98 00 00 00 	movq   $0x0,-0x68(%rax)
  30cc2a:	00 
  30cc2b:	49 8b 34 24          	mov    (%r12),%rsi
  30cc2f:	48 89 70 88          	mov    %rsi,-0x78(%rax)
  30cc33:	48 8d 73 a0          	lea    -0x60(%rbx),%rsi
  30cc37:	48 8d 7b a8          	lea    -0x58(%rbx),%rdi
  30cc3b:	48 83 c3 b0          	add    $0xffffffffffffffb0,%rbx
  30cc3f:	4d 85 f6             	test   %r14,%r14
  30cc42:	4c 8d 85 48 ff ff ff 	lea    -0xb8(%rbp),%r8
  30cc49:	49 0f 44 c8          	cmove  %r8,%rcx
  30cc4d:	48 8b 09             	mov    (%rcx),%rcx
  30cc50:	48 89 48 90          	mov    %rcx,-0x70(%rax)
  30cc54:	48 8d 8d 50 ff ff ff 	lea    -0xb0(%rbp),%rcx
  30cc5b:	48 0f 44 d1          	cmove  %rcx,%rdx
  30cc5f:	48 8b 0a             	mov    (%rdx),%rcx
  30cc62:	48 89 48 98          	mov    %rcx,-0x68(%rax)
  30cc66:	c4 c1 7a 7f 04 24    	vmovdqu %xmm0,(%r12)
  30cc6c:	49 c7 44 24 10 00 00 	movq   $0x0,0x10(%r12)
  30cc73:	00 00 
  30cc75:	48 8d 8d 58 ff ff ff 	lea    -0xa8(%rbp),%rcx
  30cc7c:	48 0f 44 f1          	cmove  %rcx,%rsi
  30cc80:	48 8b 0e             	mov    (%rsi),%rcx
  30cc83:	48 89 48 a0          	mov    %rcx,-0x60(%rax)
  30cc87:	48 8d 8d 60 ff ff ff 	lea    -0xa0(%rbp),%rcx
  30cc8e:	48 0f 44 f9          	cmove  %rcx,%rdi
  30cc92:	48 8b 0f             	mov    (%rdi),%rcx
  30cc95:	48 89 48 a8          	mov    %rcx,-0x58(%rax)
  30cc99:	c5 fa 7f 06          	vmovdqu %xmm0,(%rsi)
  30cc9d:	48 8d 8d 68 ff ff ff 	lea    -0x98(%rbp),%rcx
  30cca4:	48 0f 44 d9          	cmove  %rcx,%rbx
  30cca8:	c5 fc 10 03          	vmovups (%rbx),%ymm0
  30ccac:	c5 fc 10 4b 20       	vmovups 0x20(%rbx),%ymm1
  30ccb1:	c5 fc 10 53 2b       	vmovups 0x2b(%rbx),%ymm2
  30ccb6:	c5 fc 11 50 db       	vmovups %ymm2,-0x25(%rax)
  30ccbb:	c5 fc 11 48 d0       	vmovups %ymm1,-0x30(%rax)
  30ccc0:	c5 fc 11 40 b0       	vmovups %ymm0,-0x50(%rax)
  30ccc5:	c5 f9 6f 05 e3 c2 dc 	vmovdqa -0x233d1d(%rip),%xmm0        # d8fb0 <typeinfo name for cnetmod::io_uring_context@cnetmod.io.platform.io_uring+0xbc2>
  30cccc:	ff 
  30cccd:	c4 c1 79 d4 85 e8 06 	vpaddq 0x6e8(%r13),%xmm0,%xmm0
  30ccd4:	00 00 
  30ccd6:	c4 c1 7a 7f 85 e8 06 	vmovdqu %xmm0,0x6e8(%r13)
  30ccdd:	00 00 
  30ccdf:	4c 8b ad 10 fa ff ff 	mov    -0x5f0(%rbp),%r13
  30cce6:	4d 85 f6             	test   %r14,%r14
  30cce9:	0f 84 d5 02 00 00    	je     30cfc4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1dd4>
  30ccef:	49 ff ce             	dec    %r14
  30ccf2:	48 8b 45 b8          	mov    -0x48(%rbp),%rax
  30ccf6:	49 6b ce 78          	imul   $0x78,%r14,%rcx
  30ccfa:	48 8d 1c 08          	lea    (%rax,%rcx,1),%rbx
  30ccfe:	4d 85 f6             	test   %r14,%r14
  30cd01:	4c 8d 64 08 88       	lea    -0x78(%rax,%rcx,1),%r12
  30cd06:	48 8d 85 40 ff ff ff 	lea    -0xc0(%rbp),%rax
  30cd0d:	4c 0f 44 e0          	cmove  %rax,%r12
  30cd11:	80 bd 28 fa ff ff 01 	cmpb   $0x1,-0x5d8(%rbp)
  30cd18:	75 0c                	jne    30cd26 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b36>
  30cd1a:	48 8d 85 18 fa ff ff 	lea    -0x5e8(%rbp),%rax
  30cd21:	4d 39 f5             	cmp    %r14,%r13
  30cd24:	74 3a                	je     30cd60 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b70>
  30cd26:	48 8b 85 b0 f9 ff ff 	mov    -0x650(%rbp),%rax
  30cd2d:	48 8b 8d b8 f9 ff ff 	mov    -0x648(%rbp),%rcx
  30cd34:	48 39 c8             	cmp    %rcx,%rax
  30cd37:	74 1a                	je     30cd53 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b63>
  30cd39:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
  30cd40:	4c 39 30             	cmp    %r14,(%rax)
  30cd43:	74 0e                	je     30cd53 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b63>
  30cd45:	48 83 c0 18          	add    $0x18,%rax
  30cd49:	48 39 c8             	cmp    %rcx,%rax
  30cd4c:	75 f2                	jne    30cd40 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1b50>
  30cd4e:	e9 55 01 00 00       	jmp    30cea8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1cb8>
  30cd53:	48 39 c8             	cmp    %rcx,%rax
  30cd56:	0f 84 4c 01 00 00    	je     30cea8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1cb8>
  30cd5c:	48 83 c0 08          	add    $0x8,%rax
  30cd60:	48 8d 4b 90          	lea    -0x70(%rbx),%rcx
  30cd64:	48 8b 95 a0 f9 ff ff 	mov    -0x660(%rbp),%rdx
  30cd6b:	48 8b 3a             	mov    (%rdx),%rdi
  30cd6e:	48 8b 10             	mov    (%rax),%rdx
  30cd71:	48 89 95 b8 fb ff ff 	mov    %rdx,-0x448(%rbp)
  30cd78:	0f b6 50 08          	movzbl 0x8(%rax),%edx
  30cd7c:	88 95 c0 fb ff ff    	mov    %dl,-0x440(%rbp)
  30cd82:	0f b6 40 09          	movzbl 0x9(%rax),%eax
  30cd86:	48 8d 53 98          	lea    -0x68(%rbx),%rdx
  30cd8a:	88 85 c1 fb ff ff    	mov    %al,-0x43f(%rbp)
  30cd90:	49 8b 04 24          	mov    (%r12),%rax
  30cd94:	48 89 85 c8 fb ff ff 	mov    %rax,-0x438(%rbp)
  30cd9b:	48 8d 43 a0          	lea    -0x60(%rbx),%rax
  30cd9f:	48 8d 73 a8          	lea    -0x58(%rbx),%rsi
  30cda3:	48 83 c3 b0          	add    $0xffffffffffffffb0,%rbx
  30cda7:	4d 85 f6             	test   %r14,%r14
  30cdaa:	4c 8d 85 48 ff ff ff 	lea    -0xb8(%rbp),%r8
  30cdb1:	49 0f 44 c8          	cmove  %r8,%rcx
  30cdb5:	48 8b 09             	mov    (%rcx),%rcx
  30cdb8:	48 89 8d d0 fb ff ff 	mov    %rcx,-0x430(%rbp)
  30cdbf:	48 8d 8d 50 ff ff ff 	lea    -0xb0(%rbp),%rcx
  30cdc6:	48 0f 44 d1          	cmove  %rcx,%rdx
  30cdca:	48 8b 0a             	mov    (%rdx),%rcx
  30cdcd:	48 89 8d d8 fb ff ff 	mov    %rcx,-0x428(%rbp)
  30cdd4:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30cdd8:	c4 c1 7a 7f 04 24    	vmovdqu %xmm0,(%r12)
  30cdde:	49 c7 44 24 10 00 00 	movq   $0x0,0x10(%r12)
  30cde5:	00 00 
  30cde7:	48 8d 8d 58 ff ff ff 	lea    -0xa8(%rbp),%rcx
  30cdee:	48 0f 44 c1          	cmove  %rcx,%rax
  30cdf2:	48 8b 08             	mov    (%rax),%rcx
  30cdf5:	48 89 8d e0 fb ff ff 	mov    %rcx,-0x420(%rbp)
  30cdfc:	48 8d 8d 60 ff ff ff 	lea    -0xa0(%rbp),%rcx
  30ce03:	48 0f 44 f1          	cmove  %rcx,%rsi
  30ce07:	48 8b 0e             	mov    (%rsi),%rcx
  30ce0a:	48 89 8d e8 fb ff ff 	mov    %rcx,-0x418(%rbp)
  30ce11:	c5 fa 7f 00          	vmovdqu %xmm0,(%rax)
  30ce15:	48 8d 85 68 ff ff ff 	lea    -0x98(%rbp),%rax
  30ce1c:	48 0f 44 d8          	cmove  %rax,%rbx
  30ce20:	c5 fe 6f 03          	vmovdqu (%rbx),%ymm0
  30ce24:	c5 fc 10 4b 20       	vmovups 0x20(%rbx),%ymm1
  30ce29:	c5 fc 10 53 2b       	vmovups 0x2b(%rbx),%ymm2
  30ce2e:	c4 c1 7c 11 57 2b    	vmovups %ymm2,0x2b(%r15)
  30ce34:	c4 c1 7c 11 4f 20    	vmovups %ymm1,0x20(%r15)
  30ce3a:	c4 c1 7e 7f 07       	vmovdqu %ymm0,(%r15)
  30ce3f:	48 8d b5 b8 fb ff ff 	lea    -0x448(%rbp),%rsi
  30ce46:	c5 f8 77             	vzeroupper
  30ce49:	e8 72 2c ff ff       	call   2ffac0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::requeue_prioritized_stream_frame(cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline)>
  30ce4e:	48 8b 9d e8 fb ff ff 	mov    -0x418(%rbp),%rbx
  30ce55:	48 85 db             	test   %rbx,%rbx
  30ce58:	74 23                	je     30ce7d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c8d>
  30ce5a:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30ce61:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30ce67:	48 85 c0             	test   %rax,%rax
  30ce6a:	75 11                	jne    30ce7d <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1c8d>
  30ce6c:	48 8b 03             	mov    (%rbx),%rax
  30ce6f:	48 89 df             	mov    %rbx,%rdi
  30ce72:	ff 50 10             	call   *0x10(%rax)
  30ce75:	48 89 df             	mov    %rbx,%rdi
  30ce78:	e8 83 d9 dd ff       	call   ea800 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30ce7d:	48 8b bd c8 fb ff ff 	mov    -0x438(%rbp),%rdi
  30ce84:	48 85 ff             	test   %rdi,%rdi
  30ce87:	0f 84 59 fe ff ff    	je     30cce6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1af6>
  30ce8d:	48 89 bd d0 fb ff ff 	mov    %rdi,-0x430(%rbp)
  30ce94:	48 8b b5 d8 fb ff ff 	mov    -0x428(%rbp),%rsi
  30ce9b:	48 29 fe             	sub    %rdi,%rsi
  30ce9e:	e8 fd da dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30cea3:	e9 3e fe ff ff       	jmp    30cce6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1af6>
  30cea8:	48 8b 85 a0 f9 ff ff 	mov    -0x660(%rbp),%rax
  30ceaf:	4c 8b 28             	mov    (%rax),%r13
  30ceb2:	49 8b 95 e8 06 00 00 	mov    0x6e8(%r13),%rdx
  30ceb9:	48 85 d2             	test   %rdx,%rdx
  30cebc:	75 16                	jne    30ced4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1ce4>
  30cebe:	49 8d bd c8 06 00 00 	lea    0x6c8(%r13),%rdi
  30cec5:	c5 f8 77             	vzeroupper
  30cec8:	e8 a3 c7 00 00       	call   319670 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::__add_front_capacity()>
  30cecd:	49 8b 95 e8 06 00 00 	mov    0x6e8(%r13),%rdx
  30ced4:	49 8b 8d d0 06 00 00 	mov    0x6d0(%r13),%rcx
  30cedb:	48 b8 f1 f0 f0 f0 f0 	movabs $0xf0f0f0f0f0f0f0f1,%rax
  30cee2:	f0 f0 f0 
  30cee5:	c4 e2 fb f6 c0       	mulx   %rax,%rax,%rax
  30ceea:	48 89 c6             	mov    %rax,%rsi
  30ceed:	48 c1 ee 05          	shr    $0x5,%rsi
  30cef1:	48 bf e0 ff ff ff ff 	movabs $0x1fffffffffffffe0,%rdi
  30cef8:	ff ff 1f 
  30cefb:	48 21 f8             	and    %rdi,%rax
  30cefe:	48 8d 04 70          	lea    (%rax,%rsi,2),%rax
  30cf02:	48 29 c2             	sub    %rax,%rdx
  30cf05:	48 8b 3c f1          	mov    (%rcx,%rsi,8),%rdi
  30cf09:	48 6b c2 78          	imul   $0x78,%rdx,%rax
  30cf0d:	48 01 f8             	add    %rdi,%rax
  30cf10:	49 3b 8d d8 06 00 00 	cmp    0x6d8(%r13),%rcx
  30cf17:	ba 00 00 00 00       	mov    $0x0,%edx
  30cf1c:	48 0f 44 c2          	cmove  %rdx,%rax
  30cf20:	48 39 f8             	cmp    %rdi,%rax
  30cf23:	0f 85 e9 fc ff ff    	jne    30cc12 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a22>
  30cf29:	48 8b 44 f1 f8       	mov    -0x8(%rcx,%rsi,8),%rax
  30cf2e:	b9 f0 0f 00 00       	mov    $0xff0,%ecx
  30cf33:	48 01 c8             	add    %rcx,%rax
  30cf36:	e9 d7 fc ff ff       	jmp    30cc12 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1a22>
  30cf3b:	4c 8b b5 90 f9 ff ff 	mov    -0x670(%rbp),%r14
  30cf42:	e9 89 f3 ff ff       	jmp    30c2d0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x10e0>
  30cf47:	8b 9d 40 fd ff ff    	mov    -0x2c0(%rbp),%ebx
  30cf4d:	85 db                	test   %ebx,%ebx
  30cf4f:	0f 84 a3 00 00 00    	je     30cff8 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e08>
  30cf55:	8b 8d 44 fd ff ff    	mov    -0x2bc(%rbp),%ecx
  30cf5b:	48 8b 85 48 fd ff ff 	mov    -0x2b8(%rbp),%rax
  30cf62:	e9 9b 00 00 00       	jmp    30d002 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e12>
  30cf67:	bb 05 00 00 00       	mov    $0x5,%ebx
  30cf6c:	e8 4f d8 dd ff       	call   ea7c0 <_ZNSt3__116generic_categoryEv$plt>
  30cf71:	48 8b 95 90 f9 ff ff 	mov    -0x670(%rbp),%rdx
  30cf78:	89 1a                	mov    %ebx,(%rdx)
  30cf7a:	89 4a 04             	mov    %ecx,0x4(%rdx)
  30cf7d:	48 89 42 08          	mov    %rax,0x8(%rdx)
  30cf81:	c6 42 18 00          	movb   $0x0,0x18(%rdx)
  30cf85:	e9 88 00 00 00       	jmp    30d012 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e22>
  30cf8a:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
  30cf91:	48 3b 85 48 ff ff ff 	cmp    -0xb8(%rbp),%rax
  30cf98:	75 07                	jne    30cfa1 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1db1>
  30cf9a:	48 83 7d 88 00       	cmpq   $0x0,-0x78(%rbp)
  30cf9f:	74 23                	je     30cfc4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1dd4>
  30cfa1:	4c 8b 75 c0          	mov    -0x40(%rbp),%r14
  30cfa5:	4c 2b 75 b8          	sub    -0x48(%rbp),%r14
  30cfa9:	49 c1 fe 03          	sar    $0x3,%r14
  30cfad:	48 b8 ef ee ee ee ee 	movabs $0xeeeeeeeeeeeeeeef,%rax
  30cfb4:	ee ee ee 
  30cfb7:	4c 0f af f0          	imul   %rax,%r14
  30cfbb:	49 ff c6             	inc    %r14
  30cfbe:	0f 85 f3 00 00 00    	jne    30d0b7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1ec7>
  30cfc4:	48 8b 95 78 f9 ff ff 	mov    -0x688(%rbp),%rdx
  30cfcb:	c5 fa 6f 02          	vmovdqu (%rdx),%xmm0
  30cfcf:	48 8b 8d 90 f9 ff ff 	mov    -0x670(%rbp),%rcx
  30cfd6:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30cfda:	48 8b 42 10          	mov    0x10(%rdx),%rax
  30cfde:	48 89 41 10          	mov    %rax,0x10(%rcx)
  30cfe2:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30cfe6:	c5 fa 7f 02          	vmovdqu %xmm0,(%rdx)
  30cfea:	48 c7 42 10 00 00 00 	movq   $0x0,0x10(%rdx)
  30cff1:	00 
  30cff2:	c6 41 18 01          	movb   $0x1,0x18(%rcx)
  30cff6:	eb 3c                	jmp    30d034 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e44>
  30cff8:	bb 05 00 00 00       	mov    $0x5,%ebx
  30cffd:	e8 be d7 dd ff       	call   ea7c0 <_ZNSt3__116generic_categoryEv$plt>
  30d002:	41 89 1e             	mov    %ebx,(%r14)
  30d005:	41 89 4e 04          	mov    %ecx,0x4(%r14)
  30d009:	49 89 46 08          	mov    %rax,0x8(%r14)
  30d00d:	41 c6 46 18 00       	movb   $0x0,0x18(%r14)
  30d012:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30d019:	48 85 ff             	test   %rdi,%rdi
  30d01c:	74 16                	je     30d034 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e44>
  30d01e:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30d025:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30d02c:	48 29 fe             	sub    %rdi,%rsi
  30d02f:	e8 6c d9 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d034:	48 8b bd b0 f9 ff ff 	mov    -0x650(%rbp),%rdi
  30d03b:	48 85 ff             	test   %rdi,%rdi
  30d03e:	74 19                	je     30d059 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e69>
  30d040:	48 89 bd b8 f9 ff ff 	mov    %rdi,-0x648(%rbp)
  30d047:	48 8b b5 c0 f9 ff ff 	mov    -0x640(%rbp),%rsi
  30d04e:	48 29 fe             	sub    %rdi,%rsi
  30d051:	c5 f8 77             	vzeroupper
  30d054:	e8 47 d9 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d059:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30d060:	c5 f8 77             	vzeroupper
  30d063:	e8 c8 33 00 00       	call   310430 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30d068:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30d06f:	75 1b                	jne    30d08c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e9c>
  30d071:	48 8b bd 50 fa ff ff 	mov    -0x5b0(%rbp),%rdi
  30d078:	48 85 ff             	test   %rdi,%rdi
  30d07b:	74 0f                	je     30d08c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1e9c>
  30d07d:	48 8b b5 60 fa ff ff 	mov    -0x5a0(%rbp),%rsi
  30d084:	48 29 fe             	sub    %rdi,%rsi
  30d087:	e8 14 d9 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d08c:	48 8b bd d0 f9 ff ff 	mov    -0x630(%rbp),%rdi
  30d093:	48 85 ff             	test   %rdi,%rdi
  30d096:	0f 84 d2 e2 ff ff    	je     30b36e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x17e>
  30d09c:	48 89 bd d8 f9 ff ff 	mov    %rdi,-0x628(%rbp)
  30d0a3:	48 8b b5 e0 f9 ff ff 	mov    -0x620(%rbp),%rsi
  30d0aa:	48 29 fe             	sub    %rdi,%rsi
  30d0ad:	e8 ee d8 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d0b2:	e9 b7 e2 ff ff       	jmp    30b36e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x17e>
  30d0b7:	44 0f b6 a5 28 fa ff 	movzbl -0x5d8(%rbp),%r12d
  30d0be:	ff 
  30d0bf:	4c 8b ad 10 fa ff ff 	mov    -0x5f0(%rbp),%r13
  30d0c6:	4c 8d bd 68 fb ff ff 	lea    -0x498(%rbp),%r15
  30d0cd:	eb 20                	jmp    30d0ef <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1eff>
  30d0cf:	48 8b 85 a0 f9 ff ff 	mov    -0x660(%rbp),%rax
  30d0d6:	48 8b 38             	mov    (%rax),%rdi
  30d0d9:	b8 c8 06 00 00       	mov    $0x6c8,%eax
  30d0de:	48 01 c7             	add    %rax,%rdi
  30d0e1:	e8 7a 2b ff ff       	call   2ffc60 <std::__1::deque<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline, std::__1::allocator<cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline> >::push_front(cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline&&)>
  30d0e6:	4d 85 f6             	test   %r14,%r14
  30d0e9:	0f 84 d5 fe ff ff    	je     30cfc4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1dd4>
  30d0ef:	49 ff ce             	dec    %r14
  30d0f2:	48 8b 45 b8          	mov    -0x48(%rbp),%rax
  30d0f6:	49 6b d6 78          	imul   $0x78,%r14,%rdx
  30d0fa:	4d 85 f6             	test   %r14,%r14
  30d0fd:	48 8d 74 10 88       	lea    -0x78(%rax,%rdx,1),%rsi
  30d102:	48 8d 8d 40 ff ff ff 	lea    -0xc0(%rbp),%rcx
  30d109:	48 0f 44 f1          	cmove  %rcx,%rsi
  30d10d:	45 84 e4             	test   %r12b,%r12b
  30d110:	74 0c                	je     30d11e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f2e>
  30d112:	48 8d 8d 18 fa ff ff 	lea    -0x5e8(%rbp),%rcx
  30d119:	4d 39 f5             	cmp    %r14,%r13
  30d11c:	74 2c                	je     30d14a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f5a>
  30d11e:	48 8b 8d b0 f9 ff ff 	mov    -0x650(%rbp),%rcx
  30d125:	48 8b bd b8 f9 ff ff 	mov    -0x648(%rbp),%rdi
  30d12c:	48 39 f9             	cmp    %rdi,%rcx
  30d12f:	74 10                	je     30d141 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f51>
  30d131:	4c 39 31             	cmp    %r14,(%rcx)
  30d134:	74 0b                	je     30d141 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f51>
  30d136:	48 83 c1 18          	add    $0x18,%rcx
  30d13a:	48 39 f9             	cmp    %rdi,%rcx
  30d13d:	75 f2                	jne    30d131 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1f41>
  30d13f:	eb 8e                	jmp    30d0cf <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1edf>
  30d141:	48 39 f9             	cmp    %rdi,%rcx
  30d144:	74 89                	je     30d0cf <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1edf>
  30d146:	48 83 c1 08          	add    $0x8,%rcx
  30d14a:	48 01 d0             	add    %rdx,%rax
  30d14d:	48 8d 50 90          	lea    -0x70(%rax),%rdx
  30d151:	48 8b bd a0 f9 ff ff 	mov    -0x660(%rbp),%rdi
  30d158:	48 8b 3f             	mov    (%rdi),%rdi
  30d15b:	4c 8b 01             	mov    (%rcx),%r8
  30d15e:	4c 89 85 30 fb ff ff 	mov    %r8,-0x4d0(%rbp)
  30d165:	44 0f b6 41 08       	movzbl 0x8(%rcx),%r8d
  30d16a:	44 88 85 38 fb ff ff 	mov    %r8b,-0x4c8(%rbp)
  30d171:	0f b6 49 09          	movzbl 0x9(%rcx),%ecx
  30d175:	4c 8d 40 98          	lea    -0x68(%rax),%r8
  30d179:	88 8d 39 fb ff ff    	mov    %cl,-0x4c7(%rbp)
  30d17f:	48 8b 0e             	mov    (%rsi),%rcx
  30d182:	48 89 8d 40 fb ff ff 	mov    %rcx,-0x4c0(%rbp)
  30d189:	48 8d 48 a0          	lea    -0x60(%rax),%rcx
  30d18d:	4c 8d 48 a8          	lea    -0x58(%rax),%r9
  30d191:	48 83 c0 b0          	add    $0xffffffffffffffb0,%rax
  30d195:	4d 85 f6             	test   %r14,%r14
  30d198:	4c 8d 95 48 ff ff ff 	lea    -0xb8(%rbp),%r10
  30d19f:	49 0f 44 d2          	cmove  %r10,%rdx
  30d1a3:	48 8b 12             	mov    (%rdx),%rdx
  30d1a6:	48 89 95 48 fb ff ff 	mov    %rdx,-0x4b8(%rbp)
  30d1ad:	48 8d 95 50 ff ff ff 	lea    -0xb0(%rbp),%rdx
  30d1b4:	4c 0f 44 c2          	cmove  %rdx,%r8
  30d1b8:	49 8b 10             	mov    (%r8),%rdx
  30d1bb:	48 89 95 50 fb ff ff 	mov    %rdx,-0x4b0(%rbp)
  30d1c2:	c5 f9 ef c0          	vpxor  %xmm0,%xmm0,%xmm0
  30d1c6:	c5 fa 7f 06          	vmovdqu %xmm0,(%rsi)
  30d1ca:	48 c7 46 10 00 00 00 	movq   $0x0,0x10(%rsi)
  30d1d1:	00 
  30d1d2:	48 8d 95 58 ff ff ff 	lea    -0xa8(%rbp),%rdx
  30d1d9:	48 0f 44 ca          	cmove  %rdx,%rcx
  30d1dd:	48 8b 11             	mov    (%rcx),%rdx
  30d1e0:	48 89 95 58 fb ff ff 	mov    %rdx,-0x4a8(%rbp)
  30d1e7:	48 8d 95 60 ff ff ff 	lea    -0xa0(%rbp),%rdx
  30d1ee:	4c 0f 44 ca          	cmove  %rdx,%r9
  30d1f2:	49 8b 11             	mov    (%r9),%rdx
  30d1f5:	48 89 95 60 fb ff ff 	mov    %rdx,-0x4a0(%rbp)
  30d1fc:	c5 fa 7f 01          	vmovdqu %xmm0,(%rcx)
  30d200:	48 8d 8d 68 ff ff ff 	lea    -0x98(%rbp),%rcx
  30d207:	48 0f 44 c1          	cmove  %rcx,%rax
  30d20b:	c5 fe 6f 00          	vmovdqu (%rax),%ymm0
  30d20f:	c5 fc 10 48 20       	vmovups 0x20(%rax),%ymm1
  30d214:	c5 fc 10 50 2b       	vmovups 0x2b(%rax),%ymm2
  30d219:	c4 c1 7c 11 57 2b    	vmovups %ymm2,0x2b(%r15)
  30d21f:	c4 c1 7c 11 4f 20    	vmovups %ymm1,0x20(%r15)
  30d225:	c4 c1 7e 7f 07       	vmovdqu %ymm0,(%r15)
  30d22a:	48 8d b5 30 fb ff ff 	lea    -0x4d0(%rbp),%rsi
  30d231:	c5 f8 77             	vzeroupper
  30d234:	e8 87 28 ff ff       	call   2ffac0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::requeue_prioritized_stream_frame(cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline)>
  30d239:	48 8b 9d 60 fb ff ff 	mov    -0x4a0(%rbp),%rbx
  30d240:	48 85 db             	test   %rbx,%rbx
  30d243:	74 23                	je     30d268 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2078>
  30d245:	48 c7 c0 ff ff ff ff 	mov    $0xffffffffffffffff,%rax
  30d24c:	f0 48 0f c1 43 08    	lock xadd %rax,0x8(%rbx)
  30d252:	48 85 c0             	test   %rax,%rax
  30d255:	75 11                	jne    30d268 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2078>
  30d257:	48 8b 03             	mov    (%rbx),%rax
  30d25a:	48 89 df             	mov    %rbx,%rdi
  30d25d:	ff 50 10             	call   *0x10(%rax)
  30d260:	48 89 df             	mov    %rbx,%rdi
  30d263:	e8 98 d5 dd ff       	call   ea800 <_ZNSt3__119__shared_weak_count14__release_weakEv$plt>
  30d268:	48 8b bd 40 fb ff ff 	mov    -0x4c0(%rbp),%rdi
  30d26f:	48 85 ff             	test   %rdi,%rdi
  30d272:	0f 84 6e fe ff ff    	je     30d0e6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1ef6>
  30d278:	48 89 bd 48 fb ff ff 	mov    %rdi,-0x4b8(%rbp)
  30d27f:	48 8b b5 50 fb ff ff 	mov    -0x4b0(%rbp),%rsi
  30d286:	48 29 fe             	sub    %rdi,%rsi
  30d289:	e8 12 d7 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d28e:	e9 53 fe ff ff       	jmp    30d0e6 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1ef6>
  30d293:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d29a:	c5 f8 77             	vzeroupper
  30d29d:	e8 1e 9b fa ff       	call   2b6dc0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d2a2:	48 8b 9d 90 fa ff ff 	mov    -0x570(%rbp),%rbx
  30d2a9:	4c 8b b5 68 f9 ff ff 	mov    -0x698(%rbp),%r14
  30d2b0:	e9 18 f3 ff ff       	jmp    30c5cd <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x13dd>
  30d2b5:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d2bc:	e8 ff 9a fa ff       	call   2b6dc0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d2c1:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d2c8:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d2cf:	e9 71 f3 ff ff       	jmp    30c645 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1455>
  30d2d4:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d2db:	e8 e0 9a fa ff       	call   2b6dc0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d2e0:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d2e7:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d2ee:	e9 9b f3 ff ff       	jmp    30c68e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x149e>
  30d2f3:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d2fa:	e8 c1 9a fa ff       	call   2b6dc0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d2ff:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d306:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d30d:	e9 c5 f3 ff ff       	jmp    30c6d7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x14e7>
  30d312:	48 8d bd 40 fd ff ff 	lea    -0x2c0(%rbp),%rdi
  30d319:	e8 a2 9a fa ff       	call   2b6dc0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::emplace_back<std::byte>(std::byte&&)::{lambda()#2}::operator()() const>
  30d31e:	4c 8b b5 90 fa ff ff 	mov    -0x570(%rbp),%r14
  30d325:	48 8b 9d 48 f9 ff ff 	mov    -0x6b8(%rbp),%rbx
  30d32c:	48 8b 85 88 f9 ff ff 	mov    -0x678(%rbp),%rax
  30d333:	4c 8b 8d a0 f9 ff ff 	mov    -0x660(%rbp),%r9
  30d33a:	e9 ea f3 ff ff       	jmp    30c729 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x1539>
  30d33f:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30d346:	00 00 
  30d348:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30d34c:	0f 85 7c 02 00 00    	jne    30d5ce <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23de>
  30d352:	e8 59 9a fa ff       	call   2b6db0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__throw_length_error[abi:nqe220108]()>
  30d357:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30d35e:	00 00 
  30d360:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30d364:	0f 85 64 02 00 00    	jne    30d5ce <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23de>
  30d36a:	c5 f8 77             	vzeroupper
  30d36d:	e8 3e 9a fa ff       	call   2b6db0 <std::__1::vector<std::byte, std::__1::allocator<std::byte> >::__throw_length_error[abi:nqe220108]()>
  30d372:	e9 0e 01 00 00       	jmp    30d485 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2295>
  30d377:	e9 09 01 00 00       	jmp    30d485 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2295>
  30d37c:	49 89 c6             	mov    %rax,%r14
  30d37f:	48 8d bd a0 fa ff ff 	lea    -0x560(%rbp),%rdi
  30d386:	e8 a5 30 00 00       	call   310430 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30d38b:	e9 f8 00 00 00       	jmp    30d488 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2298>
  30d390:	49 89 c6             	mov    %rax,%r14
  30d393:	48 8d bd 50 fe ff ff 	lea    -0x1b0(%rbp),%rdi
  30d39a:	e8 b1 26 ff ff       	call   2ffa50 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30d39f:	eb 03                	jmp    30d3a4 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x21b4>
  30d3a1:	49 89 c6             	mov    %rax,%r14
  30d3a4:	48 8b bd 40 fd ff ff 	mov    -0x2c0(%rbp),%rdi
  30d3ab:	48 85 ff             	test   %rdi,%rdi
  30d3ae:	74 1b                	je     30d3cb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x21db>
  30d3b0:	48 89 bd 48 fd ff ff 	mov    %rdi,-0x2b8(%rbp)
  30d3b7:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30d3be:	48 29 fe             	sub    %rdi,%rsi
  30d3c1:	e8 da d5 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d3c6:	eb 03                	jmp    30d3cb <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x21db>
  30d3c8:	49 89 c6             	mov    %rax,%r14
  30d3cb:	4d 85 ed             	test   %r13,%r13
  30d3ce:	0f 84 6f 01 00 00    	je     30d543 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2353>
  30d3d4:	4d 29 ef             	sub    %r13,%r15
  30d3d7:	4c 89 ef             	mov    %r13,%rdi
  30d3da:	4c 89 fe             	mov    %r15,%rsi
  30d3dd:	e8 be d5 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d3e2:	e9 5c 01 00 00       	jmp    30d543 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2353>
  30d3e7:	e9 30 01 00 00       	jmp    30d51c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x232c>
  30d3ec:	e9 2b 01 00 00       	jmp    30d51c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x232c>
  30d3f1:	e9 26 01 00 00       	jmp    30d51c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x232c>
  30d3f6:	49 89 c6             	mov    %rax,%r14
  30d3f9:	e9 97 01 00 00       	jmp    30d595 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23a5>
  30d3fe:	e9 82 00 00 00       	jmp    30d485 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2295>
  30d403:	49 89 c6             	mov    %rax,%r14
  30d406:	48 8b bd 68 ff ff ff 	mov    -0x98(%rbp),%rdi
  30d40d:	48 85 ff             	test   %rdi,%rdi
  30d410:	0f 84 64 01 00 00    	je     30d57a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x238a>
  30d416:	48 8b b5 78 ff ff ff 	mov    -0x88(%rbp),%rsi
  30d41d:	48 29 fe             	sub    %rdi,%rsi
  30d420:	e8 7b d5 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d425:	e9 50 01 00 00       	jmp    30d57a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x238a>
  30d42a:	49 89 c6             	mov    %rax,%r14
  30d42d:	e9 48 01 00 00       	jmp    30d57a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x238a>
  30d432:	49 89 c6             	mov    %rax,%r14
  30d435:	e9 40 01 00 00       	jmp    30d57a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x238a>
  30d43a:	e9 dd 00 00 00       	jmp    30d51c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x232c>
  30d43f:	49 89 c6             	mov    %rax,%r14
  30d442:	48 8d bd c8 fe ff ff 	lea    -0x138(%rbp),%rdi
  30d449:	e9 f0 00 00 00       	jmp    30d53e <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x234e>
  30d44e:	49 89 c6             	mov    %rax,%r14
  30d451:	48 85 db             	test   %rbx,%rbx
  30d454:	0f 84 e9 00 00 00    	je     30d543 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2353>
  30d45a:	49 29 dd             	sub    %rbx,%r13
  30d45d:	48 89 df             	mov    %rbx,%rdi
  30d460:	4c 89 ee             	mov    %r13,%rsi
  30d463:	e8 38 d5 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d468:	e9 d6 00 00 00       	jmp    30d543 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2353>
  30d46d:	49 89 c6             	mov    %rax,%r14
  30d470:	48 8d bd 30 fb ff ff 	lea    -0x4d0(%rbp),%rdi
  30d477:	e8 74 27 ff ff       	call   2ffbf0 <cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline::~scheduled_quic_frame()>
  30d47c:	e9 c2 00 00 00       	jmp    30d543 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2353>
  30d481:	eb 02                	jmp    30d485 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2295>
  30d483:	eb 00                	jmp    30d485 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2295>
  30d485:	49 89 c6             	mov    %rax,%r14
  30d488:	48 8b bd d0 fd ff ff 	mov    -0x230(%rbp),%rdi
  30d48f:	48 85 ff             	test   %rdi,%rdi
  30d492:	0f 84 ab 00 00 00    	je     30d543 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2353>
  30d498:	48 89 bd d8 fd ff ff 	mov    %rdi,-0x228(%rbp)
  30d49f:	48 8b b5 e0 fd ff ff 	mov    -0x220(%rbp),%rsi
  30d4a6:	48 29 fe             	sub    %rdi,%rsi
  30d4a9:	e8 f2 d4 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d4ae:	e9 90 00 00 00       	jmp    30d543 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2353>
  30d4b3:	49 89 c6             	mov    %rax,%r14
  30d4b6:	be 01 00 00 00       	mov    $0x1,%esi
  30d4bb:	4c 89 ef             	mov    %r13,%rdi
  30d4be:	e8 dd d4 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d4c3:	eb 7e                	jmp    30d543 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2353>
  30d4c5:	49 89 c6             	mov    %rax,%r14
  30d4c8:	4d 85 ed             	test   %r13,%r13
  30d4cb:	0f 84 a9 00 00 00    	je     30d57a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x238a>
  30d4d1:	48 8b b5 50 fd ff ff 	mov    -0x2b0(%rbp),%rsi
  30d4d8:	4c 29 ee             	sub    %r13,%rsi
  30d4db:	4c 89 ef             	mov    %r13,%rdi
  30d4de:	e8 bd d4 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d4e3:	e9 92 00 00 00       	jmp    30d57a <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x238a>
  30d4e8:	eb 32                	jmp    30d51c <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x232c>
  30d4ea:	49 89 c6             	mov    %rax,%r14
  30d4ed:	48 8d bd b8 fb ff ff 	lea    -0x448(%rbp),%rdi
  30d4f4:	e8 f7 26 ff ff       	call   2ffbf0 <cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline::~scheduled_quic_frame()>
  30d4f9:	eb 48                	jmp    30d543 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2353>
  30d4fb:	49 89 c6             	mov    %rax,%r14
  30d4fe:	e9 92 00 00 00       	jmp    30d595 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23a5>
  30d503:	49 89 c6             	mov    %rax,%r14
  30d506:	e9 8a 00 00 00       	jmp    30d595 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23a5>
  30d50b:	49 89 c6             	mov    %rax,%r14
  30d50e:	48 8d bd b8 fc ff ff 	lea    -0x348(%rbp),%rdi
  30d515:	e8 d6 26 ff ff       	call   2ffbf0 <cnetmod::quic::scheduled_quic_frame@cnetmod.protocol.quic.send_pipeline::~scheduled_quic_frame()>
  30d51a:	eb 1b                	jmp    30d537 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2347>
  30d51c:	49 89 c6             	mov    %rax,%r14
  30d51f:	eb 22                	jmp    30d543 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2353>
  30d521:	eb 11                	jmp    30d534 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2344>
  30d523:	49 89 c6             	mov    %rax,%r14
  30d526:	48 8d bd 40 fc ff ff 	lea    -0x3c0(%rbp),%rdi
  30d52d:	e8 1e 25 ff ff       	call   2ffa50 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30d532:	eb 03                	jmp    30d537 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2347>
  30d534:	49 89 c6             	mov    %rax,%r14
  30d537:	48 8d bd d0 fd ff ff 	lea    -0x230(%rbp),%rdi
  30d53e:	e8 0d 25 ff ff       	call   2ffa50 <cnetmod::quic::quic_send_frame@cnetmod.protocol.quic.send_pipeline::~quic_send_frame()>
  30d543:	48 8b bd b0 f9 ff ff 	mov    -0x650(%rbp),%rdi
  30d54a:	48 85 ff             	test   %rdi,%rdi
  30d54d:	74 16                	je     30d565 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x2375>
  30d54f:	48 89 bd b8 f9 ff ff 	mov    %rdi,-0x648(%rbp)
  30d556:	48 8b b5 c0 f9 ff ff 	mov    -0x640(%rbp),%rsi
  30d55d:	48 29 fe             	sub    %rdi,%rsi
  30d560:	e8 3b d4 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d565:	48 8d bd 40 ff ff ff 	lea    -0xc0(%rbp),%rdi
  30d56c:	e8 bf 2e 00 00       	call   310430 <cnetmod::quic::retransmittable_frame_storage@cnetmod.protocol.quic::~retransmittable_frame_storage()>
  30d571:	80 bd 88 fa ff ff 01 	cmpb   $0x1,-0x578(%rbp)
  30d578:	75 1b                	jne    30d595 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23a5>
  30d57a:	48 8b bd 50 fa ff ff 	mov    -0x5b0(%rbp),%rdi
  30d581:	48 85 ff             	test   %rdi,%rdi
  30d584:	74 0f                	je     30d595 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23a5>
  30d586:	48 8b b5 60 fa ff ff 	mov    -0x5a0(%rbp),%rsi
  30d58d:	48 29 fe             	sub    %rdi,%rsi
  30d590:	e8 0b d4 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d595:	48 8b bd d0 f9 ff ff 	mov    -0x630(%rbp),%rdi
  30d59c:	48 85 ff             	test   %rdi,%rdi
  30d59f:	74 16                	je     30d5b7 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23c7>
  30d5a1:	48 89 bd d8 f9 ff ff 	mov    %rdi,-0x628(%rbp)
  30d5a8:	48 8b b5 e0 f9 ff ff 	mov    -0x620(%rbp),%rsi
  30d5af:	48 29 fe             	sub    %rdi,%rsi
  30d5b2:	e8 e9 d3 dd ff       	call   ea9a0 <_ZdlPvm$plt>
  30d5b7:	64 48 8b 04 25 28 00 	mov    %fs:0x28,%rax
  30d5be:	00 00 
  30d5c0:	48 3b 45 d0          	cmp    -0x30(%rbp),%rax
  30d5c4:	75 08                	jne    30d5ce <cnetmod::quic::quic_connection@cnetmod.protocol.quic::pack_one_rtt_packet(bool, std::__1::vector<std::byte, std::__1::allocator<std::byte> >, std::__1::vector<std::byte, std::__1::allocator<std::byte> >*)+0x23de>
  30d5c6:	4c 89 f7             	mov    %r14,%rdi
  30d5c9:	e8 a2 d4 dd ff       	call   eaa70 <_Unwind_Resume$plt>
  30d5ce:	c5 f8 77             	vzeroupper
  30d5d1:	e8 3a d5 dd ff       	call   eab10 <__stack_chk_fail$plt>
  30d5d6:	cc                   	int3
  30d5d7:	cc                   	int3
  30d5d8:	cc                   	int3
  30d5d9:	cc                   	int3
  30d5da:	cc                   	int3
  30d5db:	cc                   	int3
  30d5dc:	cc                   	int3
  30d5dd:	cc                   	int3
  30d5de:	cc                   	int3
  30d5df:	cc                   	int3
