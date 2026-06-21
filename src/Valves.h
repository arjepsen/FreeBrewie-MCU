#ifndef VALVES_H
#define VALVES_H

#include "Board.h"

#include <stdbool.h>
#include <stdint.h>


// ======================================================================================
//  TYPES
// ======================================================================================

typedef enum
{
    VALVE_ID_NONE = 0,
    VALVE_ID_MASH_INLET = 1,
    VALVE_ID_BOIL_RETURN = 2,
    VALVE_ID_OUTLET = 3,
    VALVE_ID_COOLER = 4,
    VALVE_ID_VALVE_5 = 5,
    VALVE_ID_HOP4 = 6,
    VALVE_ID_HOP3 = 7,
    VALVE_ID_HOP2 = 8,
    VALVE_ID_HOP1 = 9,
    VALVE_ID_MASH_RETURN = 10,
    VALVE_ID_BOIL_INLET = 11,
    VALVE_ID_COUNT
} valve_id_t;

typedef enum
{
    VALVE_POSITION_OPEN = 0,
    VALVE_POSITION_CLOSE,
    VALVE_POSITION_CLOSE_HARD,
    VALVE_POSITION_SPARGE_OPEN,
    VALVE_POSITION_SPARGE_CLOSE,
    VALVE_POSITION_COUNT
} valve_position_t;

typedef enum
{
    VALVE_CURRENT_SENSE_NONE = 0,
    VALVE_CURRENT_SENSE_VALVES,
    VALVE_CURRENT_SENSE_BOIL_PUMP,
    VALVE_CURRENT_SENSE_MASH_PUMP
} valve_current_sense_t;

typedef struct
{
    uint16_t open_us;
    uint16_t close_us;
    uint16_t close_hard_us;
    uint16_t sparge_open_us;
    uint16_t sparge_close_us;
    uint16_t frame_us;
    uint16_t burst_ms;
    uint16_t power_settle_ms;
    uint8_t startup_frames_to_ignore;
    bool current_monitoring_enabled;
} valve_config_t;

typedef struct
{
    valve_id_t valve_id;
    valve_position_t target_position;
    bool success;
    uint16_t average_current_adc;
    uint16_t peak_current_adc;
} valve_move_result_t;


// ======================================================================================
//  API
// ======================================================================================

void valves_init();

bool valves_is_installed(valve_id_t valve_id);
bool valves_is_busy();
bool valves_start_move_to_position(valve_id_t valve_id, valve_position_t position);
bool valves_abort_move();

bool valves_has_completed_move();
bool valves_get_completed_move(valve_move_result_t *result);

valve_id_t valves_get_active_valve();
valve_position_t valves_get_active_target_position();
valve_position_t valves_get_last_position(valve_id_t valve_id);
valve_current_sense_t valves_get_current_sense_source(valve_id_t valve_id);

bool valves_set_position_pulse_us(valve_id_t valve_id, valve_position_t position, uint16_t pulse_us);
bool valves_set_frame_us(valve_id_t valve_id, uint16_t frame_us);
bool valves_set_burst_ms(valve_id_t valve_id, uint16_t burst_ms);
bool valves_set_power_settle_ms(valve_id_t valve_id, uint16_t power_settle_ms);
bool valves_set_startup_frames_to_ignore(valve_id_t valve_id, uint8_t frame_count);
bool valves_set_current_monitoring_enabled(valve_id_t valve_id, bool enabled);

const valve_config_t *valves_get_config(valve_id_t valve_id);

uint16_t valves_get_last_current_adc(valve_id_t valve_id);
uint16_t valves_get_peak_current_adc(valve_id_t valve_id);
uint16_t valves_read_current_adc(valve_id_t valve_id);

#endif /* VALVES_H */
