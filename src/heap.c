/*
 * heap.c - a bump allocator.
 *
 * There is no virtual memory yet, so the heap is just a fixed window of
 * physical RAM above the kernel. Allocation is O(1); freeing is not supported
 * (the toy kernel never needs it).
 */

#include "heap.h"
#include "lib.h"

#include <stdint.h>

#define HEAP_BASE 0x41000000UL
#define HEAP_SIZE (16UL * 1024UL * 1024UL)

static unsigned long heap_next;

void heap_init(void)
{
    heap_next = HEAP_BASE;
}

void *kmalloc(size_t size)
{
    if (size == 0) {
        size = 1;
    }

    unsigned long rounded = ((unsigned long)size + 15UL) & ~15UL;
    if (heap_next + rounded > HEAP_BASE + HEAP_SIZE) {
        return 0;
    }

    void *result = (void *)heap_next;
    heap_next += rounded;
    return result;
}

void *kzalloc(size_t size)
{
    void *result = kmalloc(size);
    if (result) {
        memset(result, 0, size);
    }
    return result;
}

unsigned long heap_used(void)
{
    return heap_next - HEAP_BASE;
}

unsigned long heap_capacity(void)
{
    return HEAP_SIZE;
}
