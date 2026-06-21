#ifndef PUMPS_H
#define PUMPS_H

#include <stdbool.h>
#include <stdint.h>


// ======================================================================================
//  TYPES
// ======================================================================================

typedef enum
{
    PUMP_ID_NONE = 0,
    PUMP_ID_MASH,
    PUMP_ID_BOIL,
    PUMP_ID_COUNT
} pump_id_t;

typedef enum
{
    PUMP_DIAG_STATE_IDLE = 0,
    PUMP_DIAG_STATE_STARTING,
    PUMP_DIAG_STATE_RUNNING,
    PUMP_DIAG_STATE_MAYBE_DRY,
    PUMP_DIAG_STATE_DRY,
    PUMP_DIAG_STATE_MAYBE_CLOGGED,
    PUMP_DIAG_STATE_CLOGGED
} pump_diag_state_t;

typedef struct
{
    uint8_t commanded_speed;
    bool running;
    bool current_valid;
    uint16_t tach_window_pulses;
    uint16_t tach_total_pulses;
    uint16_t current_avg_adc;
    uint16_t current_peak_adc;
    uint8_t dry_counter;
    uint8_t clogged_counter;
    pump_diag_state_t state;
} pump_status_t;


// ======================================================================================
//  API
// ======================================================================================

void pumps_init();
void pumps_update();

bool pumps_set_speed(pump_id_t pump_id, uint8_t speed);
uint8_t pumps_get_speed(pump_id_t pump_id);

bool pumps_start(pump_id_t pump_id);
bool pumps_stop(pump_id_t pump_id);
void pumps_all_off();

bool pumps_is_running(pump_id_t pump_id);

uint16_t pumps_get_tach_pulses(pump_id_t pump_id);
void pumps_reset_tach_pulses(pump_id_t pump_id);
bool pumps_get_tach_level(pump_id_t pump_id);

uint16_t pumps_get_current_avg_adc(pump_id_t pump_id);
uint16_t pumps_get_current_peak_adc(pump_id_t pump_id);

pump_diag_state_t pumps_get_diag_state(pump_id_t pump_id);
bool pumps_is_maybe_dry(pump_id_t pump_id);
bool pumps_is_dry(pump_id_t pump_id);
bool pumps_is_maybe_clogged(pump_id_t pump_id);
bool pumps_is_clogged(pump_id_t pump_id);

bool pumps_get_status(pump_id_t pump_id, pump_status_t *status);

#endif /* PUMPS_H */
