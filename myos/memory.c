// Basic paging and physical frame allocator for Smiggles OS
// Inspired by Linux's virtual memory concepts:
// - paged virtual memory over physical frames
// - simple physical page allocator for kernel use

#include "kernel.h"

#define PAGE_SIZE          4096
#define FIRST_MANAGED_PHYS 0x00100000           // Start managing at 1 MiB
#define FALLBACK_MEMORY_TOP (16 * 1024 * 1024)

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289u
#define MB2_TAG_TYPE_END 0u
#define MB2_TAG_TYPE_MMAP 6u

#define MAX_TRACKED_FRAMES (1024u * 1024u)      // Covers up to 4 GiB
#define MAX_BITMAP_WORDS   (MAX_TRACKED_FRAMES / 32u)

#define PAGE_4M_SIZE       (4u * 1024u * 1024u)

#define PAGE_FLAG_PRESENT  0x001u
#define PAGE_FLAG_RW       0x002u
#define PAGE_FLAG_USER     0x004u
#define PAGE_ADDR_MASK     0xFFFFF000u

struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_info {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed));

struct mb2_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

#define FIRST_MANAGED_FRAME   (FIRST_MANAGED_PHYS / PAGE_SIZE)

// Frame allocation bitmap: 1 = used, 0 = free
static uint32_t frame_bitmap[MAX_BITMAP_WORDS];
static uint32_t total_frames;
static uint32_t bitmap_words;
static uint32_t tracked_memory_top;

// Kernel master page directory and tracked mapped PDE count.
static uint32_t* kernel_page_directory;
static uint32_t mapped_pde_count;

extern uint8_t __bss_end;

static inline void set_frame(uint32_t frame) {
    if (frame >= total_frames) return;
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    if (idx >= bitmap_words) return;
    frame_bitmap[idx] |= (1U << bit);
}

static inline void clear_frame(uint32_t frame) {
    if (frame >= total_frames) return;
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    if (idx >= bitmap_words) return;
    frame_bitmap[idx] &= ~(1U << bit);
}

static inline int frame_is_set(uint32_t frame) {
    if (frame >= total_frames) return 1;
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    if (idx >= bitmap_words) return 1;
    return (frame_bitmap[idx] & (1U << bit)) != 0;
}

static uint32_t align_up_u32(uint32_t value, uint32_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static void zero_page_u32(uint32_t* page_u32) {
    for (int i = 0; i < 1024; i++) {
        page_u32[i] = 0;
    }
}

static uint32_t* alloc_zeroed_page(void) {
    uint32_t* page = (uint32_t*)alloc_page();
    if (!page) return 0;
    zero_page_u32(page);
    return page;
}

static void clear_usable_range(uint64_t start, uint64_t end) {
    if (end <= start) return;
    if (start >= (uint64_t)tracked_memory_top) return;
    if (end > (uint64_t)tracked_memory_top) end = tracked_memory_top;

    uint32_t first = (uint32_t)(start / PAGE_SIZE);
    uint32_t last = (uint32_t)((end - 1u) / PAGE_SIZE);
    if (last >= total_frames) last = total_frames - 1u;

    for (uint32_t f = first; f <= last; f++) {
        clear_frame(f);
    }
}

static void reserve_range(uint32_t start, uint32_t end) {
    if (end <= start) return;
    if (start >= tracked_memory_top) return;
    if (end > tracked_memory_top) end = tracked_memory_top;

    uint32_t first = start / PAGE_SIZE;
    uint32_t last = (end - 1u) / PAGE_SIZE;
    if (last >= total_frames) last = total_frames - 1u;

    for (uint32_t f = first; f <= last; f++) {
        set_frame(f);
    }
}

static int parse_multiboot2_mmap(uint32_t mb_magic, uint32_t mb_info_addr) {
    uint32_t max_usable_end = FALLBACK_MEMORY_TOP;
    int found_mmap = 0;

    if (mb_magic != MULTIBOOT2_BOOTLOADER_MAGIC || mb_info_addr == 0) {
        tracked_memory_top = FALLBACK_MEMORY_TOP;
        return 0;
    }

    const struct mb2_info* info = (const struct mb2_info*)(uintptr_t)mb_info_addr;
    uint32_t total_size = info->total_size;
    if (total_size < 16) {
        tracked_memory_top = FALLBACK_MEMORY_TOP;
        return 0;
    }

    const uint8_t* cursor = (const uint8_t*)info + 8;
    const uint8_t* end = (const uint8_t*)info + total_size;

    while (cursor + sizeof(struct mb2_tag) <= end) {
        const struct mb2_tag* tag = (const struct mb2_tag*)cursor;
        if (tag->size < sizeof(struct mb2_tag)) break;

        if (tag->type == MB2_TAG_TYPE_END) {
            break;
        }

        if (tag->type == MB2_TAG_TYPE_MMAP && tag->size >= sizeof(struct mb2_tag_mmap)) {
            const struct mb2_tag_mmap* mmap_tag = (const struct mb2_tag_mmap*)tag;
            const uint8_t* entry_ptr = (const uint8_t*)mmap_tag + sizeof(struct mb2_tag_mmap);
            const uint8_t* tag_end = (const uint8_t*)tag + tag->size;

            found_mmap = 1;
            while (entry_ptr + mmap_tag->entry_size <= tag_end &&
                   mmap_tag->entry_size >= sizeof(struct mb2_mmap_entry)) {
                const struct mb2_mmap_entry* ent = (const struct mb2_mmap_entry*)entry_ptr;
                if (ent->type == 1 && ent->length != 0) {
                    uint64_t usable_end64 = ent->base_addr + ent->length;
                    if (usable_end64 > 0xFFFFFFFFu) usable_end64 = 0xFFFFFFFFu;
                    if ((uint32_t)usable_end64 > max_usable_end) {
                        max_usable_end = (uint32_t)usable_end64;
                    }
                }
                entry_ptr += mmap_tag->entry_size;
            }
        }

        cursor += align_up_u32(tag->size, 8u);
    }

    tracked_memory_top = align_up_u32(max_usable_end, PAGE_SIZE);
    if (tracked_memory_top < FIRST_MANAGED_PHYS) {
        tracked_memory_top = FALLBACK_MEMORY_TOP;
    }

    if (tracked_memory_top > (MAX_TRACKED_FRAMES * PAGE_SIZE)) {
        tracked_memory_top = MAX_TRACKED_FRAMES * PAGE_SIZE;
    }

    if (!found_mmap) {
        tracked_memory_top = FALLBACK_MEMORY_TOP;
    }
    return found_mmap;
}

// Find the first free physical frame at or above FIRST_MANAGED_FRAME.
static int first_free_frame(void) {
    for (uint32_t frame = FIRST_MANAGED_FRAME; frame < total_frames; frame++) {
        if (!frame_is_set(frame)) {
            return (int)frame;
        }
    }
    return -1;
}

// Initialize the simple frame allocator.
static void init_frame_allocator(uint32_t mb_magic, uint32_t mb_info_addr) {
    int has_mmap = parse_multiboot2_mmap(mb_magic, mb_info_addr);
    total_frames = tracked_memory_top / PAGE_SIZE;
    if (total_frames == 0) {
        total_frames = FALLBACK_MEMORY_TOP / PAGE_SIZE;
        tracked_memory_top = FALLBACK_MEMORY_TOP;
    }

    bitmap_words = (total_frames + 31u) / 32u;
    if (bitmap_words > MAX_BITMAP_WORDS) {
        bitmap_words = MAX_BITMAP_WORDS;
        total_frames = MAX_TRACKED_FRAMES;
        tracked_memory_top = MAX_TRACKED_FRAMES * PAGE_SIZE;
    }

    for (uint32_t i = 0; i < bitmap_words; i++) {
        frame_bitmap[i] = 0xFFFFFFFFu;
    }

    if (has_mmap && mb_magic == MULTIBOOT2_BOOTLOADER_MAGIC && mb_info_addr != 0) {
        const struct mb2_info* info = (const struct mb2_info*)(uintptr_t)mb_info_addr;
        const uint8_t* cursor = (const uint8_t*)info + 8;
        const uint8_t* end = (const uint8_t*)info + info->total_size;

        while (cursor + sizeof(struct mb2_tag) <= end) {
            const struct mb2_tag* tag = (const struct mb2_tag*)cursor;
            if (tag->size < sizeof(struct mb2_tag)) break;
            if (tag->type == MB2_TAG_TYPE_END) break;

            if (tag->type == MB2_TAG_TYPE_MMAP && tag->size >= sizeof(struct mb2_tag_mmap)) {
                const struct mb2_tag_mmap* mmap_tag = (const struct mb2_tag_mmap*)tag;
                const uint8_t* entry_ptr = (const uint8_t*)mmap_tag + sizeof(struct mb2_tag_mmap);
                const uint8_t* tag_end = (const uint8_t*)tag + tag->size;

                while (entry_ptr + mmap_tag->entry_size <= tag_end &&
                       mmap_tag->entry_size >= sizeof(struct mb2_mmap_entry)) {
                    const struct mb2_mmap_entry* ent = (const struct mb2_mmap_entry*)entry_ptr;
                    if (ent->type == 1 && ent->length != 0) {
                        clear_usable_range(ent->base_addr, ent->base_addr + ent->length);
                    }
                    entry_ptr += mmap_tag->entry_size;
                }
            }

            cursor += align_up_u32(tag->size, 8u);
        }
    } else {
        clear_usable_range(FIRST_MANAGED_PHYS, tracked_memory_top);
    }

    // Keep BIOS area and low memory reserved permanently.
    reserve_range(0, FIRST_MANAGED_PHYS);

    // Keep kernel image, bss, and bootstrap stack reserved.
    reserve_range(0, align_up_u32((uint32_t)(uintptr_t)&__bss_end, PAGE_SIZE));

    // Keep Multiboot info payload reserved.
    if (mb_magic == MULTIBOOT2_BOOTLOADER_MAGIC && mb_info_addr != 0) {
        const struct mb2_info* info = (const struct mb2_info*)(uintptr_t)mb_info_addr;
        uint32_t mb_start = mb_info_addr;
        uint32_t mb_end = align_up_u32(mb_info_addr + info->total_size, PAGE_SIZE);
        reserve_range(mb_start, mb_end);
    }

    // Never allocate trailing bits in the final bitmap word.
    if ((total_frames & 31u) != 0) {
        uint32_t tail_start = total_frames & ~31u;
        for (uint32_t f = total_frames; f < tail_start + 32u; f++) {
            set_frame(f);
        }
    }
}

static void destroy_process_directory_internal(uint32_t* pd) {
    if (!pd) return;
    for (uint32_t i = 0; i < mapped_pde_count; i++) {
        if ((pd[i] & PAGE_FLAG_PRESENT) == 0) continue;
        uint32_t* pt = (uint32_t*)(uintptr_t)(pd[i] & PAGE_ADDR_MASK);
        free_page((void*)pt);
    }
    free_page((void*)pd);
}

static int mark_user_page(uint32_t* pd, uint32_t vaddr) {
    uint32_t pde_index = vaddr >> 22;
    if (pde_index >= mapped_pde_count) return -1;

    uint32_t pde = pd[pde_index];
    if ((pde & PAGE_FLAG_PRESENT) == 0) return -1;

    uint32_t* pt = (uint32_t*)(uintptr_t)(pde & PAGE_ADDR_MASK);
    uint32_t pte_index = (vaddr >> 12) & 0x3FFu;
    if ((pt[pte_index] & PAGE_FLAG_PRESENT) == 0) return -1;

    pt[pte_index] |= PAGE_FLAG_USER;
    pd[pde_index] |= PAGE_FLAG_USER;
    return 0;
}

// Public API: allocate one 4KiB physical page, returned as a kernel virtual
// address (identity-mapped).
void* alloc_page(void) {
    int frame = first_free_frame();
    if (frame < 0) {
        return 0;
    }
    set_frame((uint32_t)frame);
    uint32_t phys = (uint32_t)frame * PAGE_SIZE;
    return (void*)phys;
}

// Public API: free a previously allocated 4KiB page.
void free_page(void* addr) {
    uint32_t phys = (uint32_t)addr;
    if (phys < FIRST_MANAGED_PHYS || phys >= tracked_memory_top) {
        return;
    }
    uint32_t frame = phys / PAGE_SIZE;
    clear_frame(frame);
}

unsigned int paging_get_kernel_directory(void) {
    return (unsigned int)(uintptr_t)kernel_page_directory;
}

unsigned int paging_create_process_directory(unsigned int user_code_addr,
                                             unsigned int user_stack_base,
                                             unsigned int user_stack_size) {
    if (!kernel_page_directory || mapped_pde_count == 0) {
        return 0;
    }

    uint32_t* pd = alloc_zeroed_page();
    if (!pd) return 0;

    for (uint32_t i = 0; i < mapped_pde_count; i++) {
        if ((kernel_page_directory[i] & PAGE_FLAG_PRESENT) == 0) continue;

        uint32_t* src_pt = (uint32_t*)(uintptr_t)(kernel_page_directory[i] & PAGE_ADDR_MASK);
        uint32_t* dst_pt = alloc_zeroed_page();
        if (!dst_pt) {
            destroy_process_directory_internal(pd);
            return 0;
        }

        for (int j = 0; j < 1024; j++) {
            dst_pt[j] = src_pt[j] & ~PAGE_FLAG_USER;
        }

        pd[i] = ((unsigned int)(uintptr_t)dst_pt) | PAGE_FLAG_PRESENT | PAGE_FLAG_RW;
    }

    if (user_code_addr != 0u && mark_user_page(pd, user_code_addr) != 0) {
        destroy_process_directory_internal(pd);
        return 0;
    }

    if (user_stack_base != 0u && user_stack_size != 0u) {
        uint32_t start = user_stack_base & ~(PAGE_SIZE - 1u);
        uint32_t end = align_up_u32(user_stack_base + user_stack_size, PAGE_SIZE);
        for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
            if (mark_user_page(pd, addr) != 0) {
                destroy_process_directory_internal(pd);
                return 0;
            }
        }
    }

    return (unsigned int)(uintptr_t)pd;
}

void paging_destroy_process_directory(unsigned int page_directory) {
    uint32_t* pd = (uint32_t*)(uintptr_t)page_directory;
    if (!pd) return;
    if (pd == kernel_page_directory) return;
    destroy_process_directory_internal(pd);
}

void paging_switch_directory(unsigned int page_directory) {
    if (page_directory == 0u) return;
    asm volatile("mov %0, %%cr3" : : "r"(page_directory) : "memory");
}

// Initialize paging with identity-mapped 4 KiB pages up to tracked_memory_top.
void init_paging(uint32_t mb_magic, uint32_t mb_info_addr) {
    init_frame_allocator(mb_magic, mb_info_addr);

    kernel_page_directory = alloc_zeroed_page();
    if (!kernel_page_directory) {
        while (1) { }
    }

    mapped_pde_count = (tracked_memory_top + PAGE_4M_SIZE - 1u) / PAGE_4M_SIZE;
    if (mapped_pde_count > 1024u) mapped_pde_count = 1024u;

    for (uint32_t i = 0; i < mapped_pde_count; i++) {
        uint32_t* page_table = alloc_zeroed_page();
        if (!page_table) {
            while (1) { }
        }

        uint32_t base = i * PAGE_4M_SIZE;
        for (uint32_t j = 0; j < 1024; j++) {
            uint32_t phys = base + (j * PAGE_SIZE);
            if (phys >= tracked_memory_top) break;
            // Present | RW (supervisor-only)
            page_table[j] = phys | PAGE_FLAG_PRESENT | PAGE_FLAG_RW;
        }

        // Present | RW (supervisor-only)
        kernel_page_directory[i] = ((unsigned int)(uintptr_t)page_table) | PAGE_FLAG_PRESENT | PAGE_FLAG_RW;
    }

    // Ensure classic 4 KiB paging mode.
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~0x10u;
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    uint32_t pd_phys = (uint32_t)(uintptr_t)kernel_page_directory;
    asm volatile("mov %0, %%cr3" : : "r"(pd_phys));

    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

