/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * Clean setjmp/longjmp for Xtensa windowed ABI (no syscall)
 */

	.section .literal.espirate_setjmp, "ax"
	.align 4
.L_spill:
	.word __xtensa_libgcc_window_spill

	.section .text.espirate_setjmp, "ax"
	.align 4
	.global espirate_setjmp
	.type espirate_setjmp, @function
espirate_setjmp:
	entry	a1, 16
	l32r	a4, .L_spill
	callx4	a4
	addi	a5, a1, -16
	l32i.n	a3, a5, 0
	l32i.n	a4, a5, 4
	s32i.n	a3, a2, 0
	s32i.n	a4, a2, 4
	l32i.n	a3, a5, 8
	l32i.n	a4, a5, 12
	s32i.n	a3, a2, 8
	s32i.n	a4, a2, 12
	extui	a3, a0, 30, 2
	blti	a3, 2, .L_s1
	l32i.n	a7, a1, 4
	slli	a4, a3, 4
	sub	a5, a7, a4
	addi	a6, a2, 16
	addi	a7, a7, -16
.L_s0:
	l32i.n	a3, a5, 0
	l32i.n	a4, a5, 4
	s32i.n	a3, a6, 0
	s32i.n	a4, a6, 4
	l32i.n	a3, a5, 8
	l32i.n	a4, a5, 12
	s32i.n	a3, a6, 8
	s32i.n	a4, a6, 12
	addi	a5, a5, 16
	addi	a6, a6, 16
	blt	a5, a7, .L_s0
.L_s1:
	l32i.n	a3, a1, 0
	l32i.n	a4, a1, 4
	s32i.n	a3, a2, 48
	s32i.n	a4, a2, 52
	l32i.n	a3, a1, 8
	l32i.n	a4, a1, 12
	s32i.n	a3, a2, 56
	s32i.n	a4, a2, 60
	s32i	a0, a2, 64
	movi.n	a2, 0
	retw.n
	.size espirate_setjmp, . - espirate_setjmp

	.section .text.espirate_longjmp, "ax"
	.align 4
	.global espirate_longjmp
	.type espirate_longjmp, @function
espirate_longjmp:
	entry	a1, 16
	rsr.windowbase	a5
	movi.n	a4, 1
	ssl	a5
	sll	a4, a4
	wsr.windowstart	a4
	rsync
	l32i	a0, a2, 64
	addi	a7, a1, -16
	l32i.n	a4, a2, 0
	l32i.n	a5, a2, 4
	s32i.n	a4, a7, 0
	s32i.n	a5, a7, 4
	l32i.n	a4, a2, 8
	l32i.n	a5, a2, 12
	s32i.n	a4, a7, 8
	s32i.n	a5, a7, 12
	extui	a7, a0, 30, 2
	blti	a7, 2, .L_l1
	l32i.n	a8, a2, 52
	slli	a4, a7, 4
	sub	a6, a8, a4
	addi	a5, a2, 16
	addi	a8, a8, -16
.L_l0:
	l32i.n	a7, a5, 0
	l32i.n	a4, a5, 4
	s32i.n	a7, a6, 0
	s32i.n	a4, a6, 4
	l32i.n	a7, a5, 8
	l32i.n	a4, a5, 12
	s32i.n	a7, a6, 8
	s32i	a4, a6, 12
	addi	a5, a5, 16
	addi	a6, a6, 16
	blt	a6, a8, .L_l0
.L_l1:
	l32i.n	a7, a2, 4
	addi	a7, a7, -16
	l32i.n	a4, a2, 48
	l32i.n	a5, a2, 52
	s32i.n	a4, a7, 0
	s32i.n	a5, a7, 4
	l32i.n	a4, a2, 56
	l32i.n	a5, a2, 60
	s32i.n	a4, a7, 8
	s32i.n	a5, a7, 12
	movi.n	a2, 1
	movnez	a2, a3, a3
	retw.n
	.size espirate_longjmp, . - espirate_longjmp
