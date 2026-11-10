
.section .text
.code64

.extern ring3_saved_kernel_rsp
.extern ring3_syscall_handler

.global ring3_enter
.type ring3_enter, @function

/*
 * void ring3_enter(uint64_t user_rsp, uint64_t user_rip);
 *
 * Save the Ring 0 callee-saved registers and stack.
 * TSS.RSP0 supplies the kernel stack on privilege transition.
 */
ring3_enter:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, ring3_saved_kernel_rsp(%rip)

    movq %rdi, %rax
    movq %rsi, %rdx

    /* IRETQ frame: SS, RSP, RFLAGS, CS, RIP */
    pushq $0x1B
    pushq %rax
    pushq $0x002
    pushq $0x23
    pushq %rdx
    iretq

.size ring3_enter, .-ring3_enter


.global syscall_entry
.type syscall_entry, @function

/*
 * DPL3 interrupt gate at vector 0x80.
 * The test syscall exits Ring 3 and resumes ring3_enter's caller.
 */
syscall_entry:
    cld

    /* Preserve the user's syscall number before calling C. */
    pushq %rax
    movq (%rsp), %rdi

    /* Align the TSS kernel stack for the SysV ABI. */
    andq $-16, %rsp
    call ring3_syscall_handler

    /*
     * Do not IRET to user mode. Restore the saved Ring 0
     * continuation and return from ring3_enter.
     */
    movq ring3_saved_kernel_rsp(%rip), %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

.size syscall_entry, .-syscall_entry


.section .rodata.userblob,"a",@progbits
.balign 16
.global user_blob_start
.global user_blob_end
.type user_blob_start, @object

/*
 * Position-independent Ring 3 test program.
 * EAX=1 requests the test exit syscall.
 */
user_blob_start:
    movl $1, %eax
    int $0x80
1:
    pause
    jmp 1b
user_blob_end:

.size user_blob_start, user_blob_end-user_blob_start


.section .bss
.balign 8
.global ring3_saved_kernel_rsp
.type ring3_saved_kernel_rsp, @object
ring3_saved_kernel_rsp:
    .quad 0

.section .note.GNU-stack,"",@progbits
