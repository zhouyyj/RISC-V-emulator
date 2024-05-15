.section .text
.global _start

_start:
    la sp, __stack_top
    ebreak
    call main

_exit:
    mv a1, a0
    li a0, 0
    ecall
