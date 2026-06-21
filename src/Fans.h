#ifndef FANS_H
#define FANS_H

#include <stdbool.h>
#include <stdint.h>


// ======================================================================================
//  TYPES
// ======================================================================================

typedef struct
{
    bool manual_override;
    bool machine_request;
    bool thermal_request;
    bool board_temp_valid;
    bool outputs_on;
    uint16_t board_temp_adc;
    uint16_t thermal_on_threshold_adc;
    uint16_t thermal_off_threshold_adc;
} fan_status_t;


// ======================================================================================
//  API
// ======================================================================================

void fans_init();
void fans_update();

void fans_set_manual_override(bool enabled);
void fans_toggle_manual_override();
bool fans_get_manual_override();

void fans_set_machine_request(bool enabled);
bool fans_get_machine_request();

void fans_set_board_temp_adc(uint16_t board_temp_adc);
void fans_clear_board_temp();

void fans_set_thermal_thresholds(uint16_t on_threshold_adc, uint16_t off_threshold_adc);

bool fans_get_thermal_request();
bool fans_are_on();
void fans_all_off();

void fans_get_status(fan_status_t *status);

#endif /* FANS_H */
