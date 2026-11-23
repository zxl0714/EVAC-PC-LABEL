.text
# Entry stub moved below the SysV red zone and saved the original RAX.
# RAX points to the first decoded operation in this straight-line run.
.globl Ps4VectorBridge
.def Ps4VectorBridge; .scl 2; .type 32; .endef
Ps4VectorBridge:
    pushfq
    leaq -400(%rsp), %rsp
    movq %rcx, 8(%rsp)
    movq %rdx, 16(%rsp)
    movq %rbx, 24(%rsp)
    movq %rbp, 40(%rsp)
    movq %rsi, 48(%rsp)
    movq %rdi, 56(%rsp)
    movq %r8, 64(%rsp)
    movq %r9, 72(%rsp)
    movq %r10, 80(%rsp)
    movq %r11, 88(%rsp)
    movq %r12, 96(%rsp)
    movq %r13, 104(%rsp)
    movq %r14, 112(%rsp)
    movq %r15, 120(%rsp)
    leaq 544(%rsp), %rcx
    movq %rcx, 32(%rsp)
    movq 408(%rsp), %rcx
    movq %rcx, 0(%rsp)
    movq 400(%rsp), %rcx
    movq %rcx, 128(%rsp)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqu %xmm\n, 144+16*\n(%rsp)
    .endr
    movq %rsp, %rbx
    andq $-16, %rsp
    subq $32, %rsp
    movq %rbx, %rcx
    movq %rax, %rdx
    cld
    callq Ps4ExecuteVectorRun
    movq %rbx, %rsp
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqu 144+16*\n(%rsp), %xmm\n
    .endr
    pushq 128(%rsp)
    popfq
    movq 0(%rsp), %rax
    movq 8(%rsp), %rcx
    movq 16(%rsp), %rdx
    movq 24(%rsp), %rbx
    movq 40(%rsp), %rbp
    movq 48(%rsp), %rsi
    movq 56(%rsp), %rdi
    movq 64(%rsp), %r8
    movq 72(%rsp), %r9
    movq 80(%rsp), %r10
    movq 88(%rsp), %r11
    movq 96(%rsp), %r12
    movq 104(%rsp), %r13
    movq 112(%rsp), %r14
    movq 120(%rsp), %r15
    leaq 544(%rsp), %rsp
    # The per-run exit stub installs the possibly modified guest RSP and
    # jumps to a fixed guest address without consuming any guest register.
    jmpq *-408(%rsp)
