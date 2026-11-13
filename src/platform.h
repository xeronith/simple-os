#ifndef PLATFORM_H
#define PLATFORM_H

const char *platform_machine_name(void);
void platform_reboot(void);
void platform_panic(void) __attribute__((noreturn));
void platform_halt(void) __attribute__((noreturn));

#endif
