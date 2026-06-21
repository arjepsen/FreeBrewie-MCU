#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>
#include <stdbool.h>

extern volatile bool power_btn_down;
extern volatile bool drain_btn_down;
extern volatile bool power_btn_flag;
extern volatile bool drain_btn_flag;

void buttons_init();
void buttons_update();

#endif  /* BUTTONS_H */