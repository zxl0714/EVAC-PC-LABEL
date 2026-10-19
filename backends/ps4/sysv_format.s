.text
.macro NativeCapture name, kind
.globl \name
.def \name; .scl 2; .type 32; .endef
.seh_proc \name
\name:
    # 32-byte Windows shadow space, 176-byte SysV register save area,
    # eight bytes of alignment. Original stack varargs begin at rsp+224.
    subq $216, %rsp
    .seh_stackalloc 216
    .seh_endprologue
    movq %rdi, 32(%rsp)
    movq %rsi, 40(%rsp)
    movq %rdx, 48(%rsp)
    movq %rcx, 56(%rsp)
    movq %r8, 64(%rsp)
    movq %r9, 72(%rsp)
    movdqu %xmm0, 80(%rsp)
    movdqu %xmm1, 96(%rsp)
    movdqu %xmm2, 112(%rsp)
    movdqu %xmm3, 128(%rsp)
    movdqu %xmm4, 144(%rsp)
    movdqu %xmm5, 160(%rsp)
    movdqu %xmm6, 176(%rsp)
    movdqu %xmm7, 192(%rsp)
    leaq 32(%rsp), %rcx
    leaq 224(%rsp), %rdx
    movq 216(%rsp), %r8
    movl $\kind, %r9d
    call NativeFormatCapture
    addq $216, %rsp
    retq
.seh_endproc
.endm
NativeCapture NativeSysvSnprintf, 0
NativeCapture NativeSysvPrintf, 1
NativeCapture NativeSysvSprintf, 2
NativeCapture NativeSysvSwprintf, 3
NativeCapture NativeSysvSscanf, 4
NativeCapture NativeSysvSprintfS, 5
