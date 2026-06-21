#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <stdbool.h>
#include <stdint.h>

void temperature_init();

bool temperature_read_boil_c_x10(int16_t *temp_c_x10);
bool temperature_read_mash_c_x10(int16_t *temp_c_x10);

#endif  /* TEMPERATURE_H */