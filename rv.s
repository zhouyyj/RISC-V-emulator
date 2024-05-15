.section .text
.globl _start

_start:
    la sp, __stack_top
    li t1, 10
    li t2, 10
    beq t1, t2, _exit
    sw sp, 0(s1)
    lb t1, 0(s1) 
    lw t1, 0(s1) 
    lh t1, 0(s1) 
    lbu t1, 0(s1) 
    lhu t1, 0(s1) 
    slli t1, t1, 12

_exit:
    li a0, 100
    li a1, 243243243
    ecall
    li a0, 0
    ecall

