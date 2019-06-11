/* Definition of multiboot header specifications.
www.gnu.org/software/grub/manual/multiboot/multiboot.htm*/

.equ MBOOT_PAGE_ALIGN, 0x1    /*Load kernel and modules on a page boundary*/
.equ MBOOT_MEM_INFO, 0x2    /* Provide your kernel with memory info*/
.equ MBOOT_VIDEO_INFO, 0x4    /* Provide your kernel with video info*/

.equ  MBOOT_HEADER_MAGIC, 0x1badb002 /* Multiboot Magic value*/
.equ  MBOOT_HEADER_FLAGS, (MBOOT_PAGE_ALIGN | MBOOT_MEM_INFO | MBOOT_VIDEO_INFO)
.equ  MBOOT_CHECKSUM,  - (MBOOT_HEADER_MAGIC + MBOOT_HEADER_FLAGS)
.equ  STACKSIZE, 0x4000

.code64

.text

.global _start
_start:
   jmp real_start 

   .align 4
mb_header:
  .long MBOOT_HEADER_MAGIC
  .long MBOOT_HEADER_FLAGS
  .long MBOOT_CHECKSUM
  .space 20, 0
  .long 0
  .long 1024
  .long 728
  .long 32

.global saved_ebp 
saved_ebp:
   .quad 0

.global error_code
error_code:
   .quad 0


.align 1  
real_start:

  cli                         /*Disable interrupts*/
  movq $(stack_b + STACKSIZE), %rsp
  push %rax
  callq main

hang:                      /*Enter an infinite loop, to stop the processor*/
  hlt
  jmp hang

.global return_from_os
return_from_os:
 pop %rax /*Return pointer on stack because of the call*/
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %r11
 pop %r10
 pop %r9
 pop %r8
 pop %rbp
 pop %rdi
 pop %rsi
 pop %rdx
 pop %rcx
 pop %rbx
 pop %rax
 jmp 1f
 1:
 iretq

.global handle_div_by_zero
handle_div_by_zero:
 cli;
 push %rax
 push %rbx
 push %rcx
 push %rdx
 push %rsi
 push %rdi 
 push %rbp
 push %r8
 push %r9
 push %r10
 push %r11
 push %r12
 push %r13
 push %r14
 push %r15
 mov  %rsp, %rdi
 callq do_div_by_zero
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %r11
 pop %r10
 pop %r9
 pop %r8
 pop %rbp
 pop %rdi
 pop %rsi
 pop %rdx
 pop %rcx
 pop %rbx
 pop %rax
 jmp 1f
 1:
 iretq


.global handle_page_fault
handle_page_fault:
 cli;
 pop error_code          /*error code*/
 push %rax
 push %rbx
 push %rcx
 push %rdx
 push %rsi
 push %rdi 
 push %rbp
 push %r8
 push %r9
 push %r10
 push %r11
 push %r12
 push %r13
 push %r14
 push %r15
 mov  %rsp, %rdi
 mov  $(error_code), %rax   /*error code address*/
 mov (%rax), %rsi
 callq do_page_fault
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %r11
 pop %r10
 pop %r9
 pop %r8
 pop %rbp
 pop %rdi
 pop %rsi
 pop %rdx
 pop %rcx
 pop %rbx
 pop %rax
 jmp 1f
 1:
 iretq


.global handle_syscall
handle_syscall:
 cli;
 push %rax
 push %rbx
 push %rcx
 push %rdx
 push %rsi
 push %rdi 
 push %rbp
 push %r8
 push %r9
 push %r10
 push %r11
 push %r12
 push %r13
 push %r14
 push %r15
 callq do_syscall
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %r11
 pop %r10
 pop %r9
 pop %r8
 pop %rbp
 pop %rdi
 pop %rsi
 pop %rdx
 pop %rcx
 pop %rbx      /*Dont pop rax*/
 add $8, %rsp
 jmp 1f
 1:
 iretq
 
.global handle_gp
handle_gp:
 mov $0x70000, %rsi    /*Canonical CR3 is @0x70000*/
 mov %rsi, %cr3
 pop %rdi
 mov (%rsp), %rsi  
 callq do_gp

 /*mov (%rsp), %rsi
 add $0x3, %rsi
 mov %rsi, (%rsp)
 jmp 1f
 1:
 iretq*/

.global handle_irq
handle_irq:
 cli;
 push %rax
 push %rbx
 push %rcx
 push %rdx
 push %rsi
 push %rdi 
 push %rbp
 push %r8
 push %r9
 push %r10
 push %r11
 push %r12
 push %r13
 push %r14
 push %r15
 mov  %rsp, %rdi
 callq do_irq
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %r11
 pop %r10
 pop %r9
 pop %r8
 pop %rbp
 pop %rdi
 pop %rsi
 pop %rdx
 pop %rcx
 pop %rbx
 pop %rax
 jmp 1f
 1:
 iretq

.global ret_from_user
ret_from_user:
  mov $0x70000, %rsi
  mov %rsi, %cr3
  mov $(saved_ebp), %rax
  mov (%rax), %rbp
  mov $0, %rax
  leaveq
  retq    

.data
.comm stack_b, STACKSIZE

.end
