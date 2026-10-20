
/var/tmp/cnetmod-datagram-cc-51d443d2/baseline-fixed/arena_h3_server:     file format elf64-x86-64


Disassembly of section .text:

0000000000302cd0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)>:
  302cd0:	80 bf 80 00 00 00 01 	cmpb   $0x1,0x80(%rdi)
  302cd7:	0f 85 26 01 00 00    	jne    302e03 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x133>
  302cdd:	48 8b 8f 78 0e 00 00 	mov    0xe78(%rdi),%rcx
  302ce4:	48 8d 87 80 0e 00 00 	lea    0xe80(%rdi),%rax
  302ceb:	48 39 c1             	cmp    %rax,%rcx
  302cee:	0f 84 0f 01 00 00    	je     302e03 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x133>
  302cf4:	55                   	push   %rbp
  302cf5:	48 89 e5             	mov    %rsp,%rbp
  302cf8:	53                   	push   %rbx
  302cf9:	48 8b 97 58 03 00 00 	mov    0x358(%rdi),%rdx
  302d00:	4c 8b 47 78          	mov    0x78(%rdi),%r8
  302d04:	4c 8b 8f 88 00 00 00 	mov    0x88(%rdi),%r9
  302d0b:	4d 39 c1             	cmp    %r8,%r9
  302d0e:	4d 0f 42 c1          	cmovb  %r9,%r8
  302d12:	41 b9 b0 04 00 00    	mov    $0x4b0,%r9d
  302d18:	41 ba f7 ff 00 00    	mov    $0xfff7,%r10d
  302d1e:	eb 09                	jmp    302d29 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x59>
  302d20:	48 39 c1             	cmp    %rax,%rcx
  302d23:	0f 84 d8 00 00 00    	je     302e01 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x131>
  302d29:	4c 8b 59 28          	mov    0x28(%rcx),%r11
  302d2d:	41 80 bb 82 00 00 00 	cmpb   $0x1,0x82(%r11)
  302d34:	01 
  302d35:	0f 85 95 00 00 00    	jne    302dd0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x100>
  302d3b:	41 80 bb 83 00 00 00 	cmpb   $0x0,0x83(%r11)
  302d42:	00 
  302d43:	0f 85 87 00 00 00    	jne    302dd0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x100>
  302d49:	41 80 bb 84 00 00 00 	cmpb   $0x0,0x84(%r11)
  302d50:	00 
  302d51:	75 7d                	jne    302dd0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x100>
  302d53:	48 85 d2             	test   %rdx,%rdx
  302d56:	74 09                	je     302d61 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x91>
  302d58:	48 8b 9a 58 07 00 00 	mov    0x758(%rdx),%rbx
  302d5f:	eb 05                	jmp    302d66 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x96>
  302d61:	bb b0 04 00 00       	mov    $0x4b0,%ebx
  302d66:	4c 39 c3             	cmp    %r8,%rbx
  302d69:	49 0f 43 d8          	cmovae %r8,%rbx
  302d6d:	48 81 fb b1 04 00 00 	cmp    $0x4b1,%rbx
  302d74:	49 0f 42 d9          	cmovb  %r9,%rbx
  302d78:	48 81 fb f7 ff 00 00 	cmp    $0xfff7,%rbx
  302d7f:	49 0f 43 da          	cmovae %r10,%rbx
  302d83:	49 39 9b 58 1c 00 00 	cmp    %rbx,0x1c58(%r11)
  302d8a:	73 44                	jae    302dd0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x100>
  302d8c:	41 80 bb 68 1c 00 00 	cmpb   $0x0,0x1c68(%r11)
  302d93:	00 
  302d94:	75 3a                	jne    302dd0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x100>
  302d96:	41 80 bb 78 1c 00 00 	cmpb   $0x0,0x1c78(%r11)
  302d9d:	00 
  302d9e:	75 30                	jne    302dd0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x100>
  302da0:	48 69 9f 98 00 00 00 	imul   $0xf4240,0x98(%rdi),%rbx
  302da7:	40 42 0f 00 
  302dab:	41 80 bb 88 1c 00 00 	cmpb   $0x0,0x1c88(%r11)
  302db2:	00 
  302db3:	75 08                	jne    302dbd <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0xed>
  302db5:	41 c6 83 88 1c 00 00 	movb   $0x1,0x1c88(%r11)
  302dbc:	01 
  302dbd:	48 01 f3             	add    %rsi,%rbx
  302dc0:	49 89 9b 80 1c 00 00 	mov    %rbx,0x1c80(%r11)
  302dc7:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
  302dce:	00 00 
  302dd0:	4c 8b 59 08          	mov    0x8(%rcx),%r11
  302dd4:	4d 85 db             	test   %r11,%r11
  302dd7:	74 17                	je     302df0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x120>
  302dd9:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
  302de0:	4c 89 d9             	mov    %r11,%rcx
  302de3:	4d 8b 1b             	mov    (%r11),%r11
  302de6:	4d 85 db             	test   %r11,%r11
  302de9:	75 f5                	jne    302de0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x110>
  302deb:	e9 30 ff ff ff       	jmp    302d20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x50>
  302df0:	49 89 cb             	mov    %rcx,%r11
  302df3:	48 8b 49 10          	mov    0x10(%rcx),%rcx
  302df7:	4c 3b 19             	cmp    (%rcx),%r11
  302dfa:	75 f4                	jne    302df0 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x120>
  302dfc:	e9 1f ff ff ff       	jmp    302d20 <cnetmod::quic::quic_connection@cnetmod.protocol.quic::quic_connection_impl::arm_path_mtu_discovery(std::__1::chrono::time_point<std::__1::chrono::steady_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000000l> > >)+0x50>
  302e01:	5b                   	pop    %rbx
  302e02:	5d                   	pop    %rbp
  302e03:	c3                   	ret
  302e04:	cc                   	int3
  302e05:	cc                   	int3
  302e06:	cc                   	int3
  302e07:	cc                   	int3
  302e08:	cc                   	int3
  302e09:	cc                   	int3
  302e0a:	cc                   	int3
  302e0b:	cc                   	int3
  302e0c:	cc                   	int3
  302e0d:	cc                   	int3
  302e0e:	cc                   	int3
  302e0f:	cc                   	int3

0000000000302e10 <std::__1::unordered_map<cnetmod::quic::connection_id@cnetmod.protocol.quic, cnetmod::quic::quic_connection@cnetmod.protocol.quic*, std::__1::hash<cnetmod::quic::connection_id@cnetmod.protocol.quic>, std::__1::equal_to<cnetmod::quic::connection_id@cnetmod.protocol.quic>, std::__1::allocator<std::__1::pair<cnetmod::quic::connection_id@cnetmod.protocol.quic const, cnetmod::quic::quic_connection@cnetmod.protocol.quic*> > >::erase[abi:nqe220108](cnetmod::quic::connection_id@cnetmod.protocol.quic const&)>:
  302e10:	55                   	push   %rbp
  302e11:	48 89 e5             	mov    %rsp,%rbp
  302e14:	53                   	push   %rbx
  302e15:	48 83 ec 28          	sub    $0x28,%rsp
  302e19:	48 89 fb             	mov    %rdi,%rbx
  302e1c:	64                   	fs
  302e1d:	48                   	rex.W
  302e1e:	8b                   	.byte 0x8b
  302e1f:	04                   	.byte 0x4
