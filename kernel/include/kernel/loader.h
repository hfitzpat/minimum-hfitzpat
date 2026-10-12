#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <stdbool.h>
#include <stdint.h>

#include "minemu/boot.h"

/*
 * User address space layout (single user program for now):
 *
 *   [0x0040_0000, 0x007f_f000)  program segments from the packaged module
 *   [0x007f_f000, 0x0080_0000)  one 4 KiB user stack page (RW, not executable)
 *
 * All of it lives under page-directory entry 1 (VA bits 31..22 == 1).
 */
#define USER_IMAGE_BASE UINT32_C(0x00400000)
#define USER_STACK_BOTTOM UINT32_C(0x007ff000)
#define USER_STACK_TOP UINT32_C(0x00800000)

/* Map the packaged System ROM image read-only so the kernel can read the
 * module table and segment data. Returns false if boot_info is unusable. */
bool loader_map_system_rom(const struct minemu_boot_info *boot_info);

/* Find the module called `name`, copy its segments into user pages, zero its
 * BSS, create the user stack, and install the user page table. On success
 * stores the module's entry address in *entry_vaddr. */
bool loader_load_module(const struct minemu_boot_info *boot_info,
                        const char *name, uint32_t *entry_vaddr);

/* Switch to USR mode at entry_vaddr with sp = stack_top (user_entry.S). */
void minemu_enter_user(uint32_t entry_vaddr, uint32_t stack_top)
    __attribute__((noreturn));

/*
 * Check that every page touched by [vaddr, vaddr + length) is mapped for
 * user-mode access: the PTE must be VALID and USER, plus WRITABLE when
 * `write` is true, or READABLE otherwise. Kernel memory and MMIO pages have
 * no USER bit, so a user cannot trick the kernel into touching them.
 * A zero-length range is trivially valid. Wrap-around ranges are rejected.
 */
bool user_range_ok(uint32_t vaddr, uint32_t length, bool write);

#endif
