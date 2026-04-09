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

// Page directory for 4 MiB pages (PSE).
static uint32_t page_directory[1024] __attribute__((aligned(4096)));

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

// Initialize paging with identity-mapped 4 MiB pages up to tracked_memory_top.
void init_paging(uint32_t mb_magic, uint32_t mb_info_addr) {
    init_frame_allocator(mb_magic, mb_info_addr);

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0;
    }

    uint32_t num_4m_pages = (tracked_memory_top + PAGE_4M_SIZE - 1u) / PAGE_4M_SIZE;
    if (num_4m_pages > 1024u) num_4m_pages = 1024u;

    for (uint32_t i = 0; i < num_4m_pages; i++) {
        uint32_t base = i * PAGE_4M_SIZE;
        // Present | RW | USER | 4MiB page (PS)
        page_directory[i] = base | 0x87u;
    }

    // Enable 4 MiB pages (CR4.PSE).
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x10u;
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    uint32_t pd_phys = (uint32_t)page_directory;
    asm volatile("mov %0, %%cr3" : : "r"(pd_phys));

    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

