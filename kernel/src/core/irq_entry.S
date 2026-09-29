/*
 * IRQ trampoline. Adapted from examples/irq-context-switch/irq.S.
 *
 * On IRQ entry the CPU has already: saved CPSR into SPSR_irq, switched to IRQ
 * mode (banked sp/lr), masked IRQs, set lr_irq = interrupted PC + 4, and
 * branched here via the vector table. Everything else is our job.
 *
 * Trap frame layout (struct minemu_trap_frame, 88 bytes):
 *   0  r0-r12      52 return_lr    56 spsr        60 exception_id
 *   64 fault_pc    68 dfsr         72 dfar        76 user_sp
 *   80 user_lr     84 reserved
 */
.syntax unified
.cpu cortex-a9
.arm

.global minemu_irq_trampoline
.type minemu_irq_trampoline, %function
.extern minemu_irq_dispatch

minemu_irq_trampoline:
    /* 1. allocate one trap frame on the IRQ stack */
    sub sp, sp, #88
    /* 2. save r0-r12, lr and spsr at their defined offsets */
    stmia sp, {r0-r12}
    str lr, [sp, #52]
    mrs r1, spsr
    str r1, [sp, #56]
    /* 3. read the interrupt controller CLAIM register -> exception_id */
    ldr r1, =0x10000000            /* MINEMU_INTERRUPT_BASE */
    ldr r1, [r1, #8]               /* claim */
    str r1, [sp, #60]
    /* 4. record the interrupted instruction's address (lr_irq - 4) */
    sub r1, lr, #4
    str r1, [sp, #64]
    /* abort status fields are meaningless for an IRQ */
    mov r1, #0
    str r1, [sp, #68]
    str r1, [sp, #72]
    /* 5. save the interrupted mode's banked sp/lr ('^' = user/system bank) */
    add r1, sp, #76
    stmia r1, {sp, lr}^
    mov r1, #0
    str r1, [sp, #84]
    /* 6. call the C dispatcher: r0 = frame, returns frame to resume */
    mov r0, sp
    bl minemu_irq_dispatch
    /* 7. restore context from the frame the dispatcher returned */
    mov sp, r0
    ldr lr, [sp, #52]
    ldr r1, [sp, #56]
    msr spsr_cxsf, r1
    add r1, sp, #76
    ldmia r1, {sp, lr}^
    ldmia sp, {r0-r12}
    add sp, sp, #88
    /* 8. return: pc = lr - 4, cpsr = spsr (restores mode and IRQ mask) */
    subs pc, lr, #4
