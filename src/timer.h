#ifndef TIMER_H
#define TIMER_H

extern volatile unsigned long timer_ticks;

void timer_init(void);
void timer_wait_ms(unsigned ms);
unsigned long timer_frequency(void);

#endif
