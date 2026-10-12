/*
 * User program loader and user-memory validation.
 *
 * The loading code is adapted from examples/user-mode-hello/kernel/main.c
 * (map_system_rom, find_module, load_user_module and their helpers).
 * Changes from the example: page tables, user pages and the user stack come
 * from alloc_zeroed_page() instead of large static arrays (faster boot), and
 * user_range_ok() is new for Assignment 2's ioctl pointer validation.
 */
#include "kernel/loader.h"

#include <stddef.h>

#include "minemu/mmu.h"
#include "minemu/platform.h"

/* Page directory installed by boot.S (physical 0x4001_0000). */
#define BOOTSTRAP_PAGE_DIRECTORY_PADDR UINT32_C(0x40010000)
#define MAX_USER_IMAGE_PAGES 16U
#define SYSTEM_ROM_TABLE_COUNT 4U

/*
 * Page tables, user program pages and the user stack are taken from free
 * physical RAM right after the kernel image instead of being static arrays.
 * (The example used ~90 KiB of static .bss for them; the firmware clears
 * .bss one byte at a time before the kernel starts, which delayed boot by
 * ~600k ticks and made the shell miss the test deadlines.)
 *
 * __minemu_und_stack_top is the last symbol of the kernel image in
 * kernel/linker/kernel.ld, so everything above it is unused RAM.
 */
extern char __minemu_und_stack_top[];
static uint32_t next_free_paddr;

static uint32_t *system_rom_tables[SYSTEM_ROM_TABLE_COUNT];
static uint32_t *user_page_table;
static uint8_t *user_image_pages[MAX_USER_IMAGE_PAGES];
static uint8_t *user_stack;
static uint32_t user_page_vaddrs[MAX_USER_IMAGE_PAGES];
static uint32_t user_page_flags[MAX_USER_IMAGE_PAGES];
static size_t user_page_count;

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t index = 0; index < length; ++index) {
        out[index] = in[index];
    }
}

static bool bytes_equal(const uint8_t *left, const char *right, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != (uint8_t)right[index]) {
            return false;
        }
    }
    return true;
}

static size_t string_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool image_range_valid(uint32_t offset, uint32_t length, uint32_t image_size) {
    return offset <= image_size && length <= image_size - offset;
}

static bool image_records_valid(uint32_t offset, uint32_t count, uint32_t image_size) {
    return offset <= image_size && count <= ((image_size - offset) >> 5);
}

/* Kernel virtual addresses in the high-half direct map <-> physical. */
static uint32_t kernel_vaddr_to_paddr(const void *address) {
    return (uint32_t)(uintptr_t)address - MINEMU_KERNEL_DIRECT_BASE + MINEMU_RAM_BASE;
}

static void *paddr_to_kernel_vaddr(uint32_t paddr) {
    return (void *)(uintptr_t)(paddr - MINEMU_RAM_BASE + MINEMU_KERNEL_DIRECT_BASE);
}

/* Return a zeroed 4 KiB page (kernel virtual address), or NULL if RAM is
 * exhausted. Pages are never freed: there is only one user program. */
static void *alloc_zeroed_page(void) {
    if (next_free_paddr == 0) {
        uint32_t kernel_end = kernel_vaddr_to_paddr(__minemu_und_stack_top);
        next_free_paddr = (kernel_end + MINEMU_PAGE_SIZE - 1U) & MINEMU_PTE_PAGE_MASK;
    }
    if (next_free_paddr - MINEMU_RAM_BASE >= MINEMU_RAM_SIZE) {
        return NULL;
    }
    uint32_t *page = paddr_to_kernel_vaddr(next_free_paddr);
    next_free_paddr += MINEMU_PAGE_SIZE;
    for (size_t index = 0; index < MINEMU_PAGE_SIZE / sizeof(uint32_t); ++index) {
        page[index] = 0; /* word-sized stores: 4x fewer than byte stores */
    }
    return page;
}

static volatile uint32_t *active_page_directory(void) {
    return (volatile uint32_t *)(uintptr_t)(
        MINEMU_KERNEL_DIRECT_BASE + BOOTSTRAP_PAGE_DIRECTORY_PADDR - MINEMU_RAM_BASE);
}

bool loader_map_system_rom(const struct minemu_boot_info *boot_info) {
    if (boot_info->system_rom_base != MINEMU_SYSTEM_ROM_BASE || boot_info->image_size == 0 ||
        boot_info->image_size > MINEMU_SYSTEM_ROM_SIZE) {
        return false;
    }

    uint32_t page_count = (boot_info->image_size + MINEMU_PAGE_SIZE - 1U) / MINEMU_PAGE_SIZE;
    uint32_t table_count = (page_count + 1023U) / 1024U;
    if (table_count > SYSTEM_ROM_TABLE_COUNT) {
        return false;
    }

    volatile uint32_t *directory = active_page_directory();
    for (uint32_t table = 0; table < table_count; ++table) {
        if (system_rom_tables[table] == NULL) {
            system_rom_tables[table] = alloc_zeroed_page();
            if (system_rom_tables[table] == NULL) {
                return false;
            }
        }
        uint32_t remaining = page_count - table * 1024U;
        uint32_t entries = remaining < 1024U ? remaining : 1024U;
        for (uint32_t index = 0; index < entries; ++index) {
            uint32_t page = table * 1024U + index;
            /* Supervisor read-only: no USER, WRITABLE or EXECUTABLE bit. */
            system_rom_tables[table][index] =
                (boot_info->system_rom_base + page * MINEMU_PAGE_SIZE) |
                MINEMU_PTE_VALID | MINEMU_PTE_READABLE;
        }
        uint32_t directory_index = (boot_info->system_rom_base >> 22) + table;
        directory[directory_index] =
            kernel_vaddr_to_paddr(system_rom_tables[table]) | MINEMU_PDE_VALID;
    }
    minemu_mmu_tlbiall();
    return true;
}

static const struct minemu_module_record *find_module(const struct minemu_boot_info *boot_info,
                                                       const char *name, size_t name_length) {
    const uint8_t *image = (const uint8_t *)(uintptr_t)boot_info->system_rom_base;
    if (!image_records_valid(boot_info->module_table_offset, boot_info->module_count,
                             boot_info->image_size)) {
        return NULL;
    }

    const struct minemu_module_record *modules =
        (const struct minemu_module_record *)(image + boot_info->module_table_offset);
    for (uint32_t index = 0; index < boot_info->module_count; ++index) {
        const struct minemu_module_record *module = &modules[index];
        if (module->name_length == name_length &&
            image_range_valid(module->name_offset, module->name_length,
                              boot_info->image_size) &&
            bytes_equal(image + module->name_offset, name, name_length)) {
            return module;
        }
    }
    return NULL;
}

static uint32_t pte_flags_from_segment(uint32_t segment_flags) {
    uint32_t pte_flags = 0;
    if ((segment_flags & MINEMU_SEGMENT_READABLE) != 0) {
        pte_flags |= MINEMU_PTE_READABLE;
    }
    if ((segment_flags & MINEMU_SEGMENT_WRITABLE) != 0) {
        pte_flags |= MINEMU_PTE_WRITABLE;
    }
    if ((segment_flags & MINEMU_SEGMENT_EXECUTABLE) != 0) {
        pte_flags |= MINEMU_PTE_EXECUTABLE;
    }
    return pte_flags;
}

/* Find (or, with create, allocate) the backing page for a user page VA.
 * Two segments may share a page only if they want identical permissions. */
static int user_page_index(uint32_t page_vaddr, uint32_t flags, bool create) {
    for (size_t index = 0; index < user_page_count; ++index) {
        if (user_page_vaddrs[index] == page_vaddr) {
            if (create && user_page_flags[index] != flags) {
                return -1;
            }
            return (int)index;
        }
    }
    if (!create || user_page_count == MAX_USER_IMAGE_PAGES) {
        return -1;
    }
    uint8_t *page = alloc_zeroed_page();
    if (page == NULL) {
        return -1;
    }
    size_t index = user_page_count++;
    user_image_pages[index] = page;
    user_page_vaddrs[index] = page_vaddr;
    user_page_flags[index] = flags;
    return (int)index;
}

static bool prepare_user_range(uint32_t vaddr, uint32_t length, uint32_t flags) {
    uint32_t end = vaddr + length;
    uint32_t page = vaddr & MINEMU_PTE_PAGE_MASK;
    while (page < end) {
        if (user_page_index(page, flags, true) < 0) {
            return false;
        }
        page += MINEMU_PAGE_SIZE;
    }
    return true;
}

/* Copy `length` bytes from source to user VA vaddr, writing through the
 * kernel's view of the backing pages. */
static bool write_user_range(uint32_t vaddr, const uint8_t *source, uint32_t length) {
    while (length != 0) {
        uint32_t page_vaddr = vaddr & MINEMU_PTE_PAGE_MASK;
        int index = user_page_index(page_vaddr, 0, false);
        if (index < 0) {
            return false;
        }
        uint32_t offset = vaddr - page_vaddr;
        uint32_t chunk = MINEMU_PAGE_SIZE - offset;
        if (chunk > length) {
            chunk = length;
        }
        copy_bytes(&user_image_pages[index][offset], source, chunk);
        source += chunk;
        vaddr += chunk;
        length -= chunk;
    }
    return true;
}

static bool load_user_module(const struct minemu_boot_info *boot_info,
                             const struct minemu_module_record *module) {
    const uint8_t *image = (const uint8_t *)(uintptr_t)boot_info->system_rom_base;
    if (module->segment_count == 0 ||
        !image_records_valid(module->segment_table_offset, module->segment_count,
                             boot_info->image_size) ||
        (module->entry_vaddr & 3U) != 0) {
        return false;
    }

    const struct minemu_module_segment *segments =
        (const struct minemu_module_segment *)(image + module->segment_table_offset);
    bool entry_is_executable = false;
    user_page_table = alloc_zeroed_page();
    user_stack = alloc_zeroed_page();
    if (user_page_table == NULL || user_stack == NULL) {
        return false;
    }
    user_page_count = 0;

    for (uint32_t index = 0; index < module->segment_count; ++index) {
        const struct minemu_module_segment *segment = &segments[index];
        if (segment->memory_size == 0 || segment->file_size > segment->memory_size ||
            (segment->flags & ~(MINEMU_SEGMENT_READABLE | MINEMU_SEGMENT_WRITABLE |
                                MINEMU_SEGMENT_EXECUTABLE)) != 0 ||
            ((segment->flags & MINEMU_SEGMENT_EXECUTABLE) != 0 &&
             (segment->flags & MINEMU_SEGMENT_READABLE) == 0) ||
            segment->virtual_address < USER_IMAGE_BASE ||
            segment->virtual_address >= USER_STACK_BOTTOM ||
            segment->memory_size > USER_STACK_BOTTOM - segment->virtual_address ||
            !image_range_valid(segment->data_offset, segment->file_size,
                               boot_info->image_size)) {
            return false;
        }

        /* Allocate (already zeroed) pages for the whole segment, which takes
         * care of BSS, then copy the initialized bytes from the image. */
        uint32_t pte_flags = pte_flags_from_segment(segment->flags);
        if (!prepare_user_range(segment->virtual_address, segment->memory_size, pte_flags) ||
            !write_user_range(segment->virtual_address, image + segment->data_offset,
                              segment->file_size)) {
            return false;
        }

        if ((segment->flags & MINEMU_SEGMENT_EXECUTABLE) != 0 &&
            segment->file_size >= 4U && module->entry_vaddr >= segment->virtual_address &&
            module->entry_vaddr - segment->virtual_address <= segment->file_size - 4U) {
            entry_is_executable = true;
        }
    }
    if (!entry_is_executable) {
        return false;
    }

    for (size_t index = 0; index < user_page_count; ++index) {
        uint32_t table_index = (user_page_vaddrs[index] >> 12) & 1023U;
        user_page_table[table_index] = kernel_vaddr_to_paddr(user_image_pages[index]) |
                                       MINEMU_PTE_VALID | MINEMU_PTE_USER |
                                       user_page_flags[index];
    }
    /* User stack: readable and writable, never executable. */
    user_page_table[(USER_STACK_BOTTOM >> 12) & 1023U] =
        kernel_vaddr_to_paddr(user_stack) | MINEMU_PTE_VALID | MINEMU_PTE_USER |
        MINEMU_PTE_READABLE | MINEMU_PTE_WRITABLE;

    volatile uint32_t *directory = active_page_directory();
    directory[USER_IMAGE_BASE >> 22] =
        kernel_vaddr_to_paddr(user_page_table) | MINEMU_PDE_VALID;
    minemu_mmu_tlbiall();
    return true;
}

bool loader_load_module(const struct minemu_boot_info *boot_info,
                        const char *name, uint32_t *entry_vaddr) {
    const struct minemu_module_record *module =
        find_module(boot_info, name, string_length(name));
    if (module == NULL || !load_user_module(boot_info, module)) {
        return false;
    }
    *entry_vaddr = module->entry_vaddr;
    return true;
}

bool user_range_ok(uint32_t vaddr, uint32_t length, bool write) {
    if (length == 0) {
        return true;
    }
    if (vaddr > UINT32_MAX - (length - 1U)) {
        return false; /* range wraps past the top of the address space */
    }

    uint32_t required = MINEMU_PTE_VALID | MINEMU_PTE_USER |
                        (write ? MINEMU_PTE_WRITABLE : MINEMU_PTE_READABLE);
    uint32_t page = vaddr & MINEMU_PTE_PAGE_MASK;
    uint32_t last_page = (vaddr + (length - 1U)) & MINEMU_PTE_PAGE_MASK;
    volatile uint32_t *directory = active_page_directory();

    for (;;) {
        uint32_t pde = directory[page >> 22];
        if ((pde & MINEMU_PDE_VALID) == 0) {
            return false;
        }
        uint32_t table_paddr = pde & MINEMU_PDE_PAGE_MASK;
        if (table_paddr < MINEMU_RAM_BASE ||
            table_paddr - MINEMU_RAM_BASE >= MINEMU_RAM_SIZE) {
            return false;
        }
        const volatile uint32_t *table = (const volatile uint32_t *)(uintptr_t)(
            table_paddr - MINEMU_RAM_BASE + MINEMU_KERNEL_DIRECT_BASE);
        uint32_t pte = table[(page >> 12) & 1023U];
        if ((pte & required) != required) {
            return false;
        }
        if (page == last_page) {
            return true;
        }
        page += MINEMU_PAGE_SIZE;
    }
}
