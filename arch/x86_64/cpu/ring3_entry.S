.section .text
.code64

.extern ring3_saved_kernel_rsp
.extern ring3_syscall_handler

.global ring3_enter
.type ring3_enter, @function

/* void ring3_enter(uint64_t user_rsp, uint64_t user_rip); */
ring3_enter:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, ring3_saved_kernel_rsp(%rip)

    /* IRETQ frame: SS, RSP, RFLAGS, CS, RIP. */
    pushq $0x1B
    pushq %rdi
    pushq $0x202
    pushq $0x23
    pushq %rsi
    iretq
.size ring3_enter, .-ring3_enter

/*
 * int 0x80 from CPL3.  Build the same trap-frame layout as isr_common:
 * 15 GPRs, vector, error code, then the CPU's five-word privilege frame.
 * A normal syscall writes its result to saved RAX and IRETQs back to CPL3.
 * A process-exit syscall switches to the kernel CR3 and leaves through the
 * saved ring3_enter continuation instead.
 */
.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    cld
    pushq $0                  /* error_code */
    pushq $128                /* vector */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %r12           /* callee-saved frame pointer */
    movq %rsp, %rdi
    andq $-16, %rsp
    call ring3_syscall_handler
    movq %r12, %rsp

    movabsq $0x8000000000000000, %rdx
    cmpq %rdx, %rax
    je ring3_resume_kernel

    movq %rax, 112(%rsp)     /* saved RAX receives syscall result */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp            /* discard vector and synthetic error */
    iretq
.size syscall_entry, .-syscall_entry

/* Shared non-returning path after a CPL3 task has exited or faulted. */
.global ring3_resume_kernel
.type ring3_resume_kernel, @function
ring3_resume_kernel:
    movq ring3_saved_kernel_rsp(%rip), %rsp
    movq $0, ring3_saved_kernel_rsp(%rip)
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size ring3_resume_kernel, .-ring3_resume_kernel

.section .rodata.userblob,"a",@progbits
.balign 16
.global user_blob_start
.global user_blob_end
.type user_blob_start, @object
/*
 * ABI smoke program: syscall 2 echoes RDI in RAX and returns to CPL3;
 * syscall 1 exits the process. A failed echo exits with status 1.
 */
user_blob_start:
    movl $2, %eax
    movl $42, %edi
    int $0x80
    cmpl $42, %eax
    jne 1f
    xorl %edi, %edi
    jmp 2f
1:
    movl $1, %edi
2:
    movl $1, %eax
    int $0x80
3:
    pause
    jmp 3b
user_blob_end:
.size user_blob_start, user_blob_end-user_blob_start

.section .bss
.balign 8
.global ring3_saved_kernel_rsp
.type ring3_saved_kernel_rsp, @object
ring3_saved_kernel_rsp:
    .quad 0

.section .note.GNU-stack,"",@progbits
