#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
unsigned long heap_used(void);
unsigned long heap_capacity(void);

#endif
