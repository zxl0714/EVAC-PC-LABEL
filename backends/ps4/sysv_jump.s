.text
# Guest libc jump-buffer ABI: RIP 0, RBX 8, entry RSP 16, RBP 24,
# R12..R15 32..56, x87 control 64, MXCSR 68, signal mask 72..87.
# This runtime has no guest signal producer/provider. setjmp records that
# empty guest mask; longjmp refuses a nonempty mask rather than ignoring it.
.globl NativeSysvSetjmp
.def NativeSysvSetjmp; .scl 2; .type 32; .endef
NativeSysvSetjmp:
    movq $0, 72(%rdi)
    movq $0, 80(%rdi)
    movq %r15, 56(%rdi)
    movq %r14, 48(%rdi)
    movq %r13, 40(%rdi)
    movq %r12, 32(%rdi)
    movq %rbp, 24(%rdi)
    movq %rsp, 16(%rdi)
    movq %rbx, 8(%rdi)
    movq (%rsp), %rax
    movq %rax, 0(%rdi)
    fnstcw 64(%rdi)
    stmxcsr 68(%rdi)
    xorl %eax, %eax
    retq

.globl NativeSysvLongjmp
.def NativeSysvLongjmp; .scl 2; .type 32; .endef
.seh_proc NativeSysvLongjmp
NativeSysvLongjmp:
    subq $40, %rsp
    .seh_stackalloc 40
    .seh_endprologue
    movq 72(%rdi), %rax
    orq 80(%rdi), %rax
    jnz .Lunsupported_mask
    # Restore control bits while preserving currently raised SSE exceptions.
    stmxcsr 32(%rsp)
    movl 32(%rsp), %ecx
    andl $63, %ecx
    movl 68(%rdi), %edx
    andl $-64, %edx
    orl %edx, %ecx
    movl %ecx, 32(%rsp)
    ldmxcsr 32(%rsp)
    fldcw 64(%rdi)
    movq 0(%rdi), %r10
    movq 16(%rdi), %r11
    movq 8(%rdi), %rbx
    movq 24(%rdi), %rbp
    movq 32(%rdi), %r12
    movq 40(%rdi), %r13
    movq 48(%rdi), %r14
    movq 56(%rdi), %r15
    movl %esi, %eax
    testl %eax, %eax
    jnz .Ljump_value
    incl %eax
.Ljump_value:
    leaq 8(%r11), %rsp
    jmpq *%r10
.Lunsupported_mask:
    movq 40(%rsp), %rdx
    movl $1, %ecx
    call NativeJumpUnsupported
    ud2
.seh_endproc
