#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

extern volatile uint32_t runtime_time_ms;

void runtime_init();
void service_fast_tasks();
void service_timed_tasks();

#endif /* RUNTIME_H */
