#include "vmalloc.h"

#include <stddef.h>
#include <stdio.h>
#include <memory.h>
#include <stdbool.h>
#include <stdint.h>

#include <errno.h>
#include <sys/mman.h>

#define _likely_(x)   __builtin_expect(!!(x), 1)
#define _unlikely_(x) __builtin_expect(!!(x), 0)
#define _malloc_      __attribute__((malloc))

#define ARENA_COUNT   20
#define THRESHOLD     1 << ARENA_COUNT
#define MAP_SIZE      64
#define RESOLUTION    MAP_SIZE * 8

struct __attribute__((packed)) arena {
    size_t class;
    uint8_t map[MAP_SIZE];
    struct arena *next;
    uint8_t data[];
};

static struct arena *arenas[ARENA_COUNT] = { NULL };

static inline size_t sizeclass(size_t size)
{
    size_t pow2 = 1 << (32 - __builtin_clz(size - 1));
    return pow2 < 16 ? 16 : pow2;
}

static inline size_t chunk_size(size_t class)
{
    return sizeof(struct arena) + class * RESOLUTION;
}

static inline size_t sizeclass_to_index(size_t class)
{
    return __builtin_ctz(class) - 4;
}

static inline bool bit_check(uint8_t map[], uint64_t bit)
{
    return map[bit / 8] & (1 << (bit % 8));
}

static inline void bit_set(uint8_t map[], uint64_t bit)
{
    map[bit / 8] |= (1 << (bit % 8));
}

static inline void bit_unset(uint8_t map[], uint64_t bit)
{
    map[bit / 8] &= ~(1 << (bit % 8));
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
    struct arena *arena = mmap_memmory(chunk_size(class));

    if (_likely_(arena)) {
        arena->class = class;
        arena->next = NULL;
        memset(arena->map, 0, sizeof(arena->map));
    }

    return arena;
}

static _malloc_ void *allocate_small(size_t size)
{
    struct arena *arena;
    size_t class = sizeclass(size);
    size_t i, idx = sizeclass_to_index(class);

    if (_unlikely_(!arenas[idx])) {
        printf("   new arena, %zu\n", class);
        arenas[idx] = new_arena(class);
    }

    arena = arenas[idx];

    for (;;) {
        for (i = 0; i < RESOLUTION && bit_check(arena->map, i); ++i);

        if (_unlikely_(i == RESOLUTION)) {
            if (!arena->next) {
                printf("   new arena (next), %zu\n", class);
                arena->next = new_arena(class);
            }

            if (_unlikely_(!arena->next)) {
                errno = ENOMEM;
                return NULL;
            }

            arena = arena->next;
        } else {
            printf("+ allocating %zu in slot %zu\n", sizeclass(size), i);
            bit_set(arena->map, i);
            return &arena->data[arena->class * i];
        }
    }
}

void *allocate(size_t size)
{
    if (_unlikely_(size == 0))
        return NULL;
    if (_likely_(size <= THRESHOLD))
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
        bit_unset(arena->map, diff / arena->class);
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
    for (i = 0; i < ARENA_COUNT; ++i) {
        struct arena *arena = arenas[i];

        if (!arenas)
            continue;

        for (; arena; arena = arena->next) {
            uintptr_t begin = (uintptr_t)arena->data;
            uintptr_t end = begin + arena->class * RESOLUTION;

            printf("   0x%zX <= 0x%zX < 0x%zX\n", begin, ptr_addr, end);

            if (ptr_addr >= begin && ptr_addr < end) {
                deallocate_from_arena(arena, ptr_addr);
                return;
            }
        }
    }

    deallocate_large(ptr);
}

void deallocate_sized(void *ptr, size_t size)
{
    if (_likely_(size <= THRESHOLD)) {
        size_t class = sizeclass(size);
        size_t idx = sizeclass_to_index(class);

        deallocate_from_arena(arenas[idx], (uintptr_t)ptr);
    } else {
        deallocate_large(ptr);
    }
}
