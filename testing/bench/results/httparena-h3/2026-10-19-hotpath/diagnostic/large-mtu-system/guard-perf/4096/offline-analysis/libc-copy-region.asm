
/usr/lib/libc.so.6:     file format elf64-x86-64


Disassembly of section .text:

0000000000176d00 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1320>:
  176d00:	c5 fd d7 c9          	vpmovmskb %ymm1,%ecx
  176d04:	85 c9                	test   %ecx,%ecx
  176d06:	75 2c                	jne    176d34 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1354>
  176d08:	c5 fd d7 ca          	vpmovmskb %ymm2,%ecx
  176d0c:	85 c9                	test   %ecx,%ecx
  176d0e:	75 18                	jne    176d28 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1348>
  176d10:	c5 fd d7 cb          	vpmovmskb %ymm3,%ecx
  176d14:	48 c1 e1 20          	shl    $0x20,%rcx
  176d18:	48 09 f1             	or     %rsi,%rcx
  176d1b:	48 0f bd c9          	bsr    %rcx,%rcx
  176d1f:	48 8d 44 01 81       	lea    -0x7f(%rcx,%rax,1),%rax
  176d24:	c5 f8 77             	vzeroupper
  176d27:	c3                   	ret
  176d28:	f3 48 0f bd c9       	lzcnt  %rcx,%rcx
  176d2d:	48 29 c8             	sub    %rcx,%rax
  176d30:	c5 f8 77             	vzeroupper
  176d33:	c3                   	ret
  176d34:	f3 0f bd c9          	lzcnt  %ecx,%ecx
  176d38:	48 29 c8             	sub    %rcx,%rax
  176d3b:	c5 f8 77             	vzeroupper
  176d3e:	c3                   	ret
  176d3f:	90                   	nop
  176d40:	f3 0f 1e fa          	endbr64
  176d44:	48 39 d1             	cmp    %rdx,%rcx
  176d47:	0f 82 53 5c fb ff    	jb     12c9a0 <__chk_fail@@GLIBC_2.3.4>
  176d4d:	0f 1f 00             	nopl   (%rax)
  176d50:	f3 0f 1e fa          	endbr64
  176d54:	48 c1 e2 02          	shl    $0x2,%rdx
  176d58:	c5 f9 6e c6          	vmovd  %esi,%xmm0
  176d5c:	48 89 f8             	mov    %rdi,%rax
  176d5f:	c4 e2 79 58 c0       	vpbroadcastd %xmm0,%xmm0
  176d64:	48 83 fa 20          	cmp    $0x20,%rdx
  176d68:	0f 82 77 01 00 00    	jb     176ee5 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1505>
  176d6e:	c4 e2 7d 58 c0       	vpbroadcastd %xmm0,%ymm0
  176d73:	eb 35                	jmp    176daa <_dl_mcount_wrapper@GLIBC_2.2.5+0x13ca>
  176d75:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
  176d7c:	00 00 00 00 
  176d80:	f3 0f 1e fa          	endbr64
  176d84:	48 39 d1             	cmp    %rdx,%rcx
  176d87:	0f 82 13 5c fb ff    	jb     12c9a0 <__chk_fail@@GLIBC_2.3.4>
  176d8d:	0f 1f 00             	nopl   (%rax)
  176d90:	f3 0f 1e fa          	endbr64
  176d94:	c5 f9 6e c6          	vmovd  %esi,%xmm0
  176d98:	48 89 f8             	mov    %rdi,%rax
  176d9b:	48 83 fa 20          	cmp    $0x20,%rdx
  176d9f:	0f 82 3b 01 00 00    	jb     176ee0 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1500>
  176da5:	c4 e2 7d 78 c0       	vpbroadcastb %xmm0,%ymm0
  176daa:	48 83 fa 40          	cmp    $0x40,%rdx
  176dae:	0f 87 95 00 00 00    	ja     176e49 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1469>
  176db4:	c5 fe 7f 44 17 e0    	vmovdqu %ymm0,-0x20(%rdi,%rdx,1)
  176dba:	c5 fe 7f 07          	vmovdqu %ymm0,(%rdi)
  176dbe:	c5 f8 77             	vzeroupper
  176dc1:	c3                   	ret
  176dc2:	0f 1f 00             	nopl   (%rax)
  176dc5:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
  176dcc:	00 00 00 00 
  176dd0:	f3 0f 1e fa          	endbr64
  176dd4:	48 39 d1             	cmp    %rdx,%rcx
  176dd7:	0f 82 c3 5b fb ff    	jb     12c9a0 <__chk_fail@@GLIBC_2.3.4>
  176ddd:	66 90                	xchg   %ax,%ax
  176ddf:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
  176de6:	00 00 00 00 
  176dea:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
  176df1:	00 00 00 00 
  176df5:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
  176dfc:	00 00 00 00 
  176e00:	f3 0f 1e fa          	endbr64
  176e04:	c5 f9 6e c6          	vmovd  %esi,%xmm0
  176e08:	48 89 f8             	mov    %rdi,%rax
  176e0b:	48 83 fa 20          	cmp    $0x20,%rdx
  176e0f:	0f 82 cb 00 00 00    	jb     176ee0 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1500>
  176e15:	c4 e2 7d 78 c0       	vpbroadcastb %xmm0,%ymm0
  176e1a:	48 83 fa 40          	cmp    $0x40,%rdx
  176e1e:	77 20                	ja     176e40 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1460>
  176e20:	c5 fe 7f 07          	vmovdqu %ymm0,(%rdi)
  176e24:	c5 fe 7f 44 17 e0    	vmovdqu %ymm0,-0x20(%rdi,%rdx,1)
  176e2a:	c5 f8 77             	vzeroupper
  176e2d:	c3                   	ret
  176e2e:	66 90                	xchg   %ax,%ax
  176e30:	c5 fe 7f 47 c0       	vmovdqu %ymm0,-0x40(%rdi)
  176e35:	c5 fe 7f 47 e0       	vmovdqu %ymm0,-0x20(%rdi)
  176e3a:	c5 f8 77             	vzeroupper
  176e3d:	c3                   	ret
  176e3e:	66 90                	xchg   %ax,%ax
  176e40:	48 3b 15 c9 23 0a 00 	cmp    0xa23c9(%rip),%rdx        # 219210 <obstack_exit_failure@@GLIBC_2.2.5+0x8>
  176e47:	77 77                	ja     176ec0 <_dl_mcount_wrapper@GLIBC_2.2.5+0x14e0>
  176e49:	c5 fe 7f 07          	vmovdqu %ymm0,(%rdi)
  176e4d:	c5 fe 7f 47 20       	vmovdqu %ymm0,0x20(%rdi)
  176e52:	48 01 d7             	add    %rdx,%rdi
  176e55:	48 81 fa 80 00 00 00 	cmp    $0x80,%rdx
  176e5c:	76 d2                	jbe    176e30 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1450>
  176e5e:	c5 fe 7f 40 40       	vmovdqu %ymm0,0x40(%rax)
  176e63:	c5 fe 7f 40 60       	vmovdqu %ymm0,0x60(%rax)
  176e68:	48 83 c7 80          	add    $0xffffffffffffff80,%rdi
  176e6c:	48 81 fa 00 01 00 00 	cmp    $0x100,%rdx
  176e73:	76 2b                	jbe    176ea0 <_dl_mcount_wrapper@GLIBC_2.2.5+0x14c0>
  176e75:	48 8d 90 80 00 00 00 	lea    0x80(%rax),%rdx
  176e7c:	48 83 e2 e0          	and    $0xffffffffffffffe0,%rdx
  176e80:	c5 fd 7f 02          	vmovdqa %ymm0,(%rdx)
  176e84:	c5 fd 7f 42 20       	vmovdqa %ymm0,0x20(%rdx)
  176e89:	c5 fd 7f 42 40       	vmovdqa %ymm0,0x40(%rdx)
  176e8e:	c5 fd 7f 42 60       	vmovdqa %ymm0,0x60(%rdx)
  176e93:	48 83 ea 80          	sub    $0xffffffffffffff80,%rdx
  176e97:	48 39 fa             	cmp    %rdi,%rdx
  176e9a:	72 e4                	jb     176e80 <_dl_mcount_wrapper@GLIBC_2.2.5+0x14a0>
  176e9c:	0f 1f 40 00          	nopl   0x0(%rax)
  176ea0:	c5 fe 7f 07          	vmovdqu %ymm0,(%rdi)
  176ea4:	c5 fe 7f 47 20       	vmovdqu %ymm0,0x20(%rdi)
  176ea9:	c5 fe 7f 47 40       	vmovdqu %ymm0,0x40(%rdi)
  176eae:	c5 fe 7f 47 60       	vmovdqu %ymm0,0x60(%rdi)
  176eb3:	c5 f8 77             	vzeroupper
  176eb6:	c3                   	ret
  176eb7:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
  176ebe:	00 00 
  176ec0:	48 3b 15 c9 93 0a 00 	cmp    0xa93c9(%rip),%rdx        # 220290 <_obstack@GLIBC_2.2.5+0x98>
  176ec7:	73 77                	jae    176f40 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1560>
  176ec9:	40 0f b6 c6          	movzbl %sil,%eax
  176ecd:	48 89 d1             	mov    %rdx,%rcx
  176ed0:	48 89 fa             	mov    %rdi,%rdx
  176ed3:	f3 aa                	rep stos %al,(%rdi)
  176ed5:	48 89 d0             	mov    %rdx,%rax
  176ed8:	c5 f8 77             	vzeroupper
  176edb:	c3                   	ret
  176edc:	0f 1f 40 00          	nopl   0x0(%rax)
  176ee0:	c4 e2 79 78 c0       	vpbroadcastb %xmm0,%xmm0
  176ee5:	83 fa 10             	cmp    $0x10,%edx
  176ee8:	7d 16                	jge    176f00 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1520>
  176eea:	83 fa 08             	cmp    $0x8,%edx
  176eed:	7d 21                	jge    176f10 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1530>
  176eef:	83 fa 04             	cmp    $0x4,%edx
  176ef2:	7d 2c                	jge    176f20 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1540>
  176ef4:	83 fa 01             	cmp    $0x1,%edx
  176ef7:	7f 37                	jg     176f30 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1550>
  176ef9:	7c 03                	jl     176efe <_dl_mcount_wrapper@GLIBC_2.2.5+0x151e>
  176efb:	40 88 37             	mov    %sil,(%rdi)
  176efe:	c3                   	ret
  176eff:	90                   	nop
  176f00:	c5 fa 7f 07          	vmovdqu %xmm0,(%rdi)
  176f04:	c5 fa 7f 44 17 f0    	vmovdqu %xmm0,-0x10(%rdi,%rdx,1)
  176f0a:	c3                   	ret
  176f0b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
  176f10:	c5 f9 d6 07          	vmovq  %xmm0,(%rdi)
  176f14:	c5 f9 d6 44 17 f8    	vmovq  %xmm0,-0x8(%rdi,%rdx,1)
  176f1a:	c3                   	ret
  176f1b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
  176f20:	c5 f9 7e 07          	vmovd  %xmm0,(%rdi)
  176f24:	c5 f9 7e 44 17 fc    	vmovd  %xmm0,-0x4(%rdi,%rdx,1)
  176f2a:	c3                   	ret
  176f2b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
  176f30:	40 88 37             	mov    %sil,(%rdi)
  176f33:	40 88 77 01          	mov    %sil,0x1(%rdi)
  176f37:	40 88 74 17 ff       	mov    %sil,-0x1(%rdi,%rdx,1)
  176f3c:	c3                   	ret
  176f3d:	0f 1f 00             	nopl   (%rax)
  176f40:	c5 fe 7f 07          	vmovdqu %ymm0,(%rdi)
  176f44:	48 8d 54 17 80       	lea    -0x80(%rdi,%rdx,1),%rdx
  176f49:	48 83 cf 1f          	or     $0x1f,%rdi
  176f4d:	48 ff c7             	inc    %rdi
  176f50:	c5 fd e7 07          	vmovntdq %ymm0,(%rdi)
  176f54:	c5 fd e7 47 20       	vmovntdq %ymm0,0x20(%rdi)
  176f59:	c5 fd e7 47 40       	vmovntdq %ymm0,0x40(%rdi)
  176f5e:	c5 fd e7 47 60       	vmovntdq %ymm0,0x60(%rdi)
  176f63:	48 83 ef 80          	sub    $0xffffffffffffff80,%rdi
  176f67:	48 39 d7             	cmp    %rdx,%rdi
  176f6a:	72 e4                	jb     176f50 <_dl_mcount_wrapper@GLIBC_2.2.5+0x1570>
  176f6c:	0f ae f8             	sfence
  176f6f:	c5 fe 7f 02          	vmovdqu %ymm0,(%rdx)
  176f73:	c5 fe 7f 42 20       	vmovdqu %ymm0,0x20(%rdx)
  176f78:	c5 fe 7f 42 40       	vmovdqu %ymm0,0x40(%rdx)
  176f7d:	c5 fe 7f 42 60       	vmovdqu %ymm0,0x60(%rdx)
  176f82:	c5 f8 77             	vzeroupper
  176f85:	c3                   	ret
  176f86:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
  176f8d:	00 00 00 
  176f90:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
  176f97:	00 00 00 
  176f9a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
