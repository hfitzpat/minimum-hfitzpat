/*
 * SVC (system call) trampoline. Adapted from
 * examples/user-mode-hello/kernel/svc.S.
 *
 * A user program runs `svc #0` with the syscall number in r7 and arguments
 * in r0-r3. The CPU then: saves CPSR into SPSR_svc, switches to SVC mode
 * (banked sp/lr, IRQs masked), sets lr_svc = address of the instruction
 * after the svc, and branches here through the vector table.
 *
 * We save the interrupted state in a struct minemu_trap_frame (88 bytes) on
 * the SVC stack, call minemu_svc_dispatch(frame) in C, and then restore the
 * (possibly modified) frame. The dispatcher returns a result by changing the
 * saved r0, so the user sees it in r0 after the svc.
 *
 * Trap frame layout:
 *   0  r0-r12      52 return_lr    56 spsr        60 exception_id
 *   64 fault_pc    68 dfsr         72 dfar        76 user_sp
 *   80 user_lr     84 reserved
 */
.syntax unified
.cpu cortex-a9
.arm

.global minemu_svc_trampoline
.type minemu_svc_trampoline, %function
.extern minemu_svc_dispatch

minemu_svc_trampoline:
    /* 1. allocate one trap frame on the SVC stack */
    sub sp, sp, #88
    /* 2. save r0-r12, the return address and the user's CPSR (from SPSR) */
    stmia sp, {r0-r12}
    str lr, [sp, #52]
    mrs r1, spsr
    str r1, [sp, #56]
    /* 3. exception_id = MINEMU_EXCEPTION_SVC (-3) */
    ldr r1, =-3
    str r1, [sp, #60]
    /* 4. fault_pc = address of the svc instruction itself (lr_svc - 4) */
    sub r1, lr, #4
    str r1, [sp, #64]
    /* abort status fields are meaningless for an SVC */
    mov r1, #0
    str r1, [sp, #68]
    str r1, [sp, #72]
    /* 5. save the user-mode banked sp/lr ('^' = user register bank) */
    add r1, sp, #76
    stmia r1, {sp, lr}^
    mov r1, #0
    str r1, [sp, #84]
    /* 6. call the C dispatcher: r0 = frame, returns the frame to resume */
    mov r0, sp
    bl minemu_svc_dispatch
    /* 7. restore everything from the returned frame (r0 now holds the result) */
    mov sp, r0
    ldr lr, [sp, #52]
    ldr r1, [sp, #56]
    msr spsr_cxsf, r1
    add r1, sp, #76
    ldmia r1, {sp, lr}^
    ldmia sp, {r0-r12}
    add sp, sp, #88
    /* 8. return: pc = lr (instruction after svc), cpsr = spsr (back to USR) */
    movs pc, lr
