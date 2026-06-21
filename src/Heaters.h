#ifndef HEATERS_H
#define HEATERS_H

#include <stdbool.h>
#include <stdint.h>


// ======================================================================================
//  CONSTANTS
// ======================================================================================

#define HEATERS_AC_SAMPLE_COUNT                         256U
#define HEATERS_AC_SAMPLE_COUNT_SHIFT                   8U
#define HEATERS_LOAD_PRESENT_SPAN_THRESHOLD             40U
#define HEATERS_AC_BASELINE_ADC_COUNTS                  8U
#define HEATERS_EST_CURRENT_SCALE_NUMERATOR             1U
#define HEATERS_EST_CURRENT_SCALE_SHIFT                 3U
#define HEATERS_NOMINAL_MAINS_VOLTAGE                   230U


// ======================================================================================
//  TYPES
// ======================================================================================

typedef enum
{
    //HEATER_ID_NONE = 0,
    HEATER_ID_MASH,
    HEATER_ID_BOIL,
    HEATER_ID_COUNT
} heater_id_t;

typedef struct
{
    uint16_t min_sample;
    uint16_t max_sample;
    uint16_t avg_sample;
    uint16_t span_sample;
    uint16_t estimated_power_w;
    uint8_t estimated_current_x10;
    bool mash_on;
    bool boil_on;
    bool load_present;
    bool measurement_active;
    bool measurement_valid;
} heaters_status_t;


// ======================================================================================
//  API
// ======================================================================================

void heaters_init();
bool heaters_set(heater_id_t heater_id, bool enabled);
void heaters_all_off();

bool heaters_request_measurement();
void heaters_update();
bool heaters_get_status(heaters_status_t *status);

#endif /* HEATERS_H */
