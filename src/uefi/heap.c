/*
 * heap.c - a small static bump allocator for the UEFI application.
 *
 * A fixed 1 MiB array avoids depending on Boot Services allocation and works
 * identically on firmware and in QEMU.
 */

#include "heap.h"
#include "lib.h"

#define HEAP_SIZE (1024UL * 1024UL)

static unsigned char heap_area[HEAP_SIZE] __attribute__((aligned(16)));
static unsigned long heap_next;

void heap_init(void)
{
    heap_next = (unsigned long)heap_area;
}

void *kmalloc(size_t size)
{
    if (size == 0) {
        size = 1;
    }

    unsigned long rounded = ((unsigned long)size + 15UL) & ~15UL;
    if (heap_next + rounded > (unsigned long)heap_area + HEAP_SIZE) {
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
    return heap_next - (unsigned long)heap_area;
}

unsigned long heap_capacity(void)
{
    return HEAP_SIZE;
}
