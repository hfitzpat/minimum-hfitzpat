/*
 * First entry into user mode. Adapted from
 * examples/user-mode-hello/kernel/user.S.
 *
 * void minemu_enter_user(uint32_t entry_vaddr, uint32_t stack_top);
 *   r0 = A32 entry virtual address of the user program
 *   r1 = 8-byte-aligned top of the user stack
 *
 * The trick: an exception return (`movs pc, lr`) copies SPSR into CPSR. By
 * putting a USR-mode CPSR value (0x10: USR mode, ARM state, IRQs enabled) in
 * SPSR_svc and the entry address in lr, the "return" lands in user mode at
 * the program's entry point. This never returns to the kernel; the kernel is
 * re-entered only through exceptions (SVC, IRQ, aborts).
 */
.syntax unified
.cpu cortex-a9
.arm

.global minemu_enter_user
.type minemu_enter_user, %function

minemu_enter_user:
    cpsid if
    mov r2, r0

    /* Privileged user-register transfer initializes USR's banked SP and LR. */
    sub sp, sp, #8
    str r1, [sp]
    mov r3, #0
    str r3, [sp, #4]
    mov r3, sp
    ldmia r3, {sp, lr}^
    add sp, sp, #8

    /* SPSR = USR mode with IRQs unmasked; lr = user entry point. */
    mov r3, #0x10
    msr spsr_cxsf, r3
    mov lr, r2
    /* Start the program with zeroed argument registers r0-r3. */
    mov r0, #0
    mov r1, #0
    mov r2, #0
    mov r3, #0
    movs pc, lr
