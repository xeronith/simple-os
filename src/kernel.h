#ifndef KERNEL_H
#define KERNEL_H

#include <stddef.h>
#include <stdint.h>

#define KERNEL_NAME    "simple-os"
#define KERNEL_VERSION "0.1"

void exception_report(unsigned long kind, unsigned long esr, unsigned long far,
                      unsigned long elr, unsigned long spsr);
void irq_handler(void);

#endif
