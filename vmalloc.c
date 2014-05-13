#include "vmalloc.h"

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include <errno.h>
#include <sys/mman.h>

#define _likely_(x)   __builtin_expect(!!(x), 1)
#define _unlikely_(x) __builtin_expect(!!(x), 0)
#define _malloc_      __attribute__((malloc))

static const size_t threshold = 1 << 20;
static const size_t chunk_size = 4096 * 1024;

struct arena {
    size_t class;
    uint64_t map;
    uint8_t data[];
};

struct arena *arenas[17] = { NULL };

static size_t sizeclass(size_t size)
{
    size_t pow2 = 1 << (32 - __builtin_clz(size - 1));
    return pow2 < 16 ? 16 : pow2;
}

static size_t sizeclass_to_index(size_t class)
{
    return __builtin_ctz(class) - 4;
}

static inline bool map_check(uint64_t map, uint8_t bit)
{
    return map & 1 << bit;
}

static inline uint64_t map_set(uint64_t map, uint8_t bit)
{
    return map | 1 << bit;
}

static inline uint64_t map_unset(uint64_t map, uint8_t bit)
{
    return map & ~(1 << bit);
}

static inline _malloc_ void *mmap_memmory(size_t size)
{
    uint8_t *memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (_unlikely_(memory == MAP_FAILED))
        errno = ENOMEM;
    return memory;
}

static struct arena *new_arena(size_t class)
{
    struct arena *arena = mmap_memmory(chunk_size);

    if (_likely_(arena)) {
        arena->class = class;
        arena->map = 0L;
    }

    return arena;
}

static _malloc_ void *allocate_small(size_t size)
{
    struct arena *arena;
    size_t class = sizeclass(size);
    size_t i, idx = sizeclass_to_index(class);

    if (_unlikely_(!arenas[idx])) {
        printf("   new arena, %zu\n", sizeclass(size));
        arenas[idx] = new_arena(sizeclass(size));
    }

    arena = arenas[idx];

    /* TODO: only be used once pool if full? */
    for (i = 0; i < sizeof(arena->map) * 8 && map_check(arena->map, i); ++i);

    printf("+ allocating %zu in slot %zu\n", sizeclass(size), i);
    arena->map = map_set(arena->map, i);
    return &arena->data[arena->class * i];
}

void *allocate(size_t size)
{
    if (_unlikely_(size == 0))
        return NULL;
    if (_likely_(size <= threshold))
        return allocate_small(size);

    printf("+ large allocation of %zu\n", size);

    size_t realsize = size + sizeof(size_t);
    size_t *memory = mmap_memmory(realsize);
    *memory = realsize;
    return memory + 1;
}

static void deallocate_from_arena(struct arena *arena, uintptr_t ptr_addr)
{
    ptrdiff_t diff = ptr_addr - (uintptr_t)arena->data;

    if (diff >= 0 && diff % arena->class == 0) {
        printf("- deallocating %zu in slot %zu\n", arena->class, diff / arena->class);
        arena->map = map_unset(arena->map, diff / arena->class);
    }
}

static void deallocate_large(void *ptr)
{
    size_t *memory = (size_t *)ptr - 1;
    printf("- large deallocation of %zu\n", *memory - sizeof(size_t));
    munmap(memory, *memory);
}

void deallocate(void *ptr)
{
    uintptr_t ptr_addr = (uintptr_t)ptr;
    size_t i;

    printf("- deallocating\n");
    for (i = 0; i < sizeof(arenas) / sizeof(arenas[0]); ++i) {
        if (arenas[i] == NULL)
            continue;

        uintptr_t begin = (uintptr_t)arenas[i]->data;
        uintptr_t end = begin + chunk_size - sizeof(struct arena);

        printf("   0x%zX <= 0x%zX < 0x%zX\n", begin, ptr_addr, end);

        if (ptr_addr >= begin && ptr_addr < end) {
            deallocate_from_arena(arenas[i], ptr_addr);
            return;
        }
    }

    deallocate_large(ptr);
}

void deallocate_sized(void *ptr, size_t size)
{
    if (_likely_(size <= threshold)) {
        size_t class = sizeclass(size);
        size_t idx = sizeclass_to_index(class);

        deallocate_from_arena(arenas[idx], (uintptr_t)ptr);
    } else {
        deallocate_large(ptr);
    }

}
