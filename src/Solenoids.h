#ifndef SOLENOIDS_H
#define SOLENOIDS_H

#include <stdbool.h>
#include <stdint.h>


// ======================================================================================
//  TYPES
// ======================================================================================

typedef enum
{
    SOLENOID_ID_BREW_INLET = 0,
    SOLENOID_ID_COOLING_INLET,
    SOLENOID_ID_COUNT
} solenoid_id_t;

typedef struct
{
    uint16_t brew_inlet_current_adc;
    uint16_t cooling_inlet_current_adc;
    bool brew_inlet_on;
    bool cooling_inlet_on;
} solenoids_status_t;


// ======================================================================================
//  API
// ======================================================================================

void solenoids_init();
bool solenoids_set(solenoid_id_t solenoid_id, bool enabled);
bool solenoids_get(solenoid_id_t solenoid_id, bool *enabled);
void solenoids_all_off();

bool solenoids_sample_current(solenoid_id_t solenoid_id, uint16_t *current_adc);
bool solenoids_get_status(solenoids_status_t *status);

#endif /* SOLENOIDS_H */