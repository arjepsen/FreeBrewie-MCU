#ifndef PRESSURE_H
#define PRESSURE_H

#include "TWI.h"

#include <stdbool.h>
#include <stdint.h>


// ======================================================================================
//  MACROS
// ======================================================================================

#define PRESSURE_SENSOR_ADDRESS                  0x19U
#define PRESSURE_REGISTER_START                  0x00U


// ======================================================================================
//  TYPES
// ======================================================================================

typedef enum
{
    PRESSURE_STATUS_OK = 0,
    PRESSURE_STATUS_COMMAND_MODE,
    PRESSURE_STATUS_STALE_DATA,
    PRESSURE_STATUS_DIAGNOSTIC_FAULT
} pressure_status_t;

typedef struct
{
    uint16_t pressure_word;
    uint16_t pressure_count;
    uint16_t temperature_word;
    pressure_status_t status;
    bool valid;
} pressure_raw_reading_t;

typedef struct
{
    uint16_t pressure_count;
    uint16_t temperature_word;
    int16_t temperature_c_x10;
    pressure_status_t status;
    bool valid;
} pressure_reading_t;


// ======================================================================================
//  PROTOTYPES
// ======================================================================================

void pressure_init();

twi_status_t pressure_read_raw(pressure_raw_reading_t *reading);
twi_status_t pressure_read_processed(pressure_reading_t *reading);

twi_status_t pressure_get_status(pressure_status_t *status);
twi_status_t pressure_get_count(uint16_t *pressure_count);
twi_status_t pressure_get_temperature_raw(uint16_t *temperature_word);
twi_status_t pressure_get_count_and_temperature_raw(uint16_t *pressure_count,
                                                    uint16_t *temperature_word);

bool pressure_status_is_valid(pressure_status_t status);
int16_t pressure_temperature_raw_to_c_x10(uint16_t temperature_word);

#endif