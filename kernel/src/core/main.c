#include "kernel/kprintf.h"
#include "kernel/loader.h"
#include "kernel/uart.h"
#include "minemu/boot.h"
#include "minemu/irq.h"
#include "minemu/trap.h"
#include "minemu/trace.h"

/* Name of the module (from image/minimum.toml) that runs as the first and
 * only user program: the msh shell. */
static const char user_program_name[] = "minimum-user";

void minemu_kernel_main(const struct minemu_boot_info *boot_info) {
    if ((uintptr_t)boot_info != MINEMU_BOOT_INFO_VADDR ||
        boot_info->magic != MINEMU_BOOT_INFO_MAGIC ||
        boot_info->version != MINEMU_ABI_VERSION ||
        boot_info->size != sizeof(*boot_info) ||
        boot_info->system_rom_base != UINT32_C(0x08000000) ||
        boot_info->direct_map_vaddr != UINT32_C(0xc0000000) ||
        boot_info->direct_map_paddr != UINT32_C(0x40000000) ||
        boot_info->direct_map_size != UINT32_C(0x04000000)) {
        minemu_trace_event(UINT32_C(0xb007bad0));
        minemu_fail_stop();
    }

    /* UART0 driver + RX interrupt. IRQs stay masked on the CPU until we drop
     * into user mode, whose CPSR has the I bit clear (see user_entry.S). */
    uart_init();

    uint32_t entry_vaddr = 0;
    if (!loader_map_system_rom(boot_info) ||
        !loader_load_module(boot_info, user_program_name, &entry_vaddr)) {
        kprintf("kernel: could not load user program '%s'\n", user_program_name);
        minemu_trace_event(UINT32_C(0xb007bad1));
        minemu_fail_stop();
    }

    minemu_enter_user(entry_vaddr, USER_STACK_TOP);
}
