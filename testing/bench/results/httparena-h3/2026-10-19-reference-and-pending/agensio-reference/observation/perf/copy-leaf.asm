
/var/tmp/agensio-reference-observation-dd7463c36c79453ca4d8fd9099ae2cf9/perf/mapped-files/usr/lib/x86_64-linux-gnu/libc.so.6:     file format elf64-x86-64


Disassembly of section .text:

0000000000162b60 <_dl_mcount_wrapper@GLIBC_2.2.5+0xd10>:
  162b60:	00 01                	add    %al,(%rcx)
  162b62:	74 9c                	je     162b00 <_dl_mcount_wrapper@GLIBC_2.2.5+0xcb0>
  162b64:	83 f9 c0             	cmp    $0xffffffc0,%ecx
  162b67:	0f 87 7e fe ff ff    	ja     1629eb <_dl_mcount_wrapper@GLIBC_2.2.5+0xb9b>
  162b6d:	c5 fe 6f 4e 20       	vmovdqu 0x20(%rsi),%ymm1
  162b72:	48 29 fe             	sub    %rdi,%rsi
  162b75:	48 83 c7 3f          	add    $0x3f,%rdi
  162b79:	49 8d 0c 10          	lea    (%r8,%rdx,1),%rcx
  162b7d:	48 83 e7 c0          	and    $0xffffffffffffffc0,%rdi
  162b81:	48 01 fe             	add    %rdi,%rsi
  162b84:	48 29 f9             	sub    %rdi,%rcx
  162b87:	f3 a4                	rep movsb (%rsi),(%rdi)
  162b89:	c4 c1 7e 7f 00       	vmovdqu %ymm0,(%r8)
  162b8e:	c4 c1 7e 7f 48 20    	vmovdqu %ymm1,0x20(%r8)
  162b94:	c5 f8 77             	vzeroupper
  162b97:	c3                   	ret
  162b98:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
  162b9f:	00 
  162ba0:	4c 8b 1d 31 97 08 00 	mov    0x89731(%rip),%r11        # 1ec2d8 <_obstack@GLIBC_2.2.5+0x88>
  162ba7:	4c 39 da             	cmp    %r11,%rdx
  162baa:	0f 82 21 fe ff ff    	jb     1629d1 <_dl_mcount_wrapper@GLIBC_2.2.5+0xb81>
