.section .text
.global gdt_load_and_reload_cs
.type gdt_load_and_reload_cs, @function

gdt_load_and_reload_cs:
    lgdt (%rdi)
    pushq $0x08
    leaq .Lreload_cs(%rip), %rax
    pushq %rax
    lretq

.Lreload_cs:
    ret
.size gdt_load_and_reload_cs, .-gdt_load_and_reload_cs

.section .note.GNU-stack,"",@progbits
