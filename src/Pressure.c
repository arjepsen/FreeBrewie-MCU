#include "Pressure.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// ======================================================================================
//  PROTOTYPES
// ======================================================================================

static inline pressure_status_t pressure_decode_status(uint16_t pressure_word);


// ======================================================================================
//  MAIN
// ======================================================================================

/****************************************************************************************
 * @brief Initialize the pressure driver.
 *
 * The pressure sensor uses the shared TWI driver. No additional hardware configuration
 * is required here, but this function is kept as the public module entry point.
 *
 * @return None.
 ****************************************************************************************/
void pressure_init()
{
}


/****************************************************************************************
 * @brief Read one raw frame from the pressure sensor.
 *
 * Reads 4 bytes starting at register 0x00 and splits them into:
 * - raw 16-bit pressure word
 * - 2-bit status field
 * - 14-bit pressure count
 * - raw 16-bit temperature word
 *
 * No conversion to physical units is done here.
 *
 * @param reading Pointer receiving the decoded raw reading.
 *
 * @return TWI transport status.
 ****************************************************************************************/
twi_status_t pressure_read_raw(pressure_raw_reading_t *reading)
{
    if (reading == NULL)
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    uint8_t tx_data = PRESSURE_REGISTER_START;
    uint8_t rx_data[4];
    twi_status_t twi_status;

    twi_status = twi_write_read(PRESSURE_SENSOR_ADDRESS, &tx_data, 1U, rx_data, 4U);

    if (twi_status != TWI_STATUS_OK)
    {
        return twi_status;
    }

    const uint16_t pressure_word = ((uint16_t)rx_data[0] << 8) | (uint16_t)rx_data[1];

    reading->pressure_word = pressure_word;
    reading->pressure_count = pressure_word & 0x3FFFU;
    reading->temperature_word = ((uint16_t)rx_data[2] << 8) | (uint16_t)rx_data[3];
    reading->status = pressure_decode_status(pressure_word);
    reading->valid = (reading->status == PRESSURE_STATUS_OK);

    return TWI_STATUS_OK;
}


/****************************************************************************************
 * @brief Read one processed pressure-sensor sample.
 *
 * This helper keeps the low-level transport and framing inside the pressure module while
 * exposing a more directly useful application-facing reading.
 *
 * Current processing performed:
 * - decode status
 * - expose 14-bit pressure count
 * - expose raw temperature word
 * - convert temperature to tenths of degrees Celsius
 * - mark validity from the decoded status
 *
 * @param reading Pointer receiving the processed reading.
 *
 * @return TWI transport status.
 ****************************************************************************************/
twi_status_t pressure_read_processed(pressure_reading_t *reading)
{
    if (reading == NULL)
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    pressure_raw_reading_t raw_reading;

    twi_status_t twi_status = pressure_read_raw(&raw_reading);
    if (twi_status != TWI_STATUS_OK)
    {
        return twi_status;
    }

    reading->pressure_count = raw_reading.pressure_count;
    reading->temperature_word = raw_reading.temperature_word;
    reading->temperature_c_x10 = (int16_t)(((uint32_t)raw_reading.temperature_word * 10UL) >> 8) - 700;
    reading->status = raw_reading.status;
    reading->valid = raw_reading.valid;

    return TWI_STATUS_OK;
}


/****************************************************************************************
 * @brief Read only the decoded sensor status.
 *
 * @param status Pointer receiving the decoded status.
 *
 * @return TWI transport status.
 ****************************************************************************************/
twi_status_t pressure_get_status(pressure_status_t *status)
{
    if (status == NULL)
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    pressure_raw_reading_t reading;

    twi_status_t twi_status = pressure_read_raw(&reading);
    if (twi_status != TWI_STATUS_OK)
    {
        return twi_status;
    }

    *status = reading.status;
    return TWI_STATUS_OK;
}


/****************************************************************************************
 * @brief Read only the 14-bit pressure count.
 *
 * @param pressure_count Pointer receiving the pressure count.
 *
 * @return TWI transport status.
 ****************************************************************************************/
twi_status_t pressure_get_count(uint16_t *pressure_count)
{
    if (pressure_count == NULL)
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    pressure_raw_reading_t reading;

    twi_status_t twi_status = pressure_read_raw(&reading);
    if (twi_status != TWI_STATUS_OK)
    {
        return twi_status;
    }

    *pressure_count = reading.pressure_count;
    return TWI_STATUS_OK;
}


/****************************************************************************************
 * @brief Read only the raw temperature word.
 *
 * @param temperature_word Pointer receiving the raw temperature word.
 *
 * @return TWI transport status.
 ****************************************************************************************/
twi_status_t pressure_get_temperature_raw(uint16_t *temperature_word)
{
    if (temperature_word == NULL)
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    pressure_raw_reading_t reading;

    twi_status_t twi_status = pressure_read_raw(&reading);
    if (twi_status != TWI_STATUS_OK)
    {
        return twi_status;
    }

    *temperature_word = reading.temperature_word;
    return TWI_STATUS_OK;
}


/****************************************************************************************
 * @brief Read both the pressure count and raw temperature word.
 *
 * @param pressure_count Pointer receiving the pressure count.
 * @param temperature_word Pointer receiving the raw temperature word.
 *
 * @return TWI transport status.
 ****************************************************************************************/
twi_status_t pressure_get_count_and_temperature_raw(uint16_t *pressure_count,
                                                    uint16_t *temperature_word)
{
    if ((pressure_count == NULL) || (temperature_word == NULL))
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    pressure_raw_reading_t reading;

    twi_status_t twi_status = pressure_read_raw(&reading);
    if (twi_status != TWI_STATUS_OK)
    {
        return twi_status;
    }

    *pressure_count = reading.pressure_count;
    *temperature_word = reading.temperature_word;

    return TWI_STATUS_OK;
}


/*
 * Former helper kept here as historical reference:
 * pressure_status_is_valid() simply tested for PRESSURE_STATUS_OK.
 */


/****************************************************************************************
 * @brief Convert the raw temperature word to tenths of degrees Celsius.
 *
 * This follows the conversion behavior used in the older Brewie code path:
 *
 *     temperature_c = (temperature_word / 256.0) - 70.0
 *
 * To stay efficient and float-free on AVR, the conversion is implemented as:
 *
 *     temperature_c_x10 = ((temperature_word * 10) / 256) - 700
 *
 * @param temperature_word Raw 16-bit temperature word.
 *
 * @return Temperature in 0.1 degree Celsius units.
 ****************************************************************************************/
/*
 * Former helper kept here as historical reference:
 * pressure_temperature_raw_to_c_x10() now lives inline inside the processed read path.
 */


// ======================================================================================
//  PRIVATE FUNCTIONS
// ======================================================================================

/****************************************************************************************
 * @brief Decode the 2-bit sensor status from the raw pressure word.
 *
 * Raw pressure word format:
 * - bits 15:14 = status
 * - bits 13:0  = pressure count
 *
 * @param pressure_word Raw 16-bit pressure word.
 *
 * @return Decoded pressure status.
 ****************************************************************************************/
static pressure_status_t pressure_decode_status(uint16_t pressure_word)
{
    const uint8_t status_bits = (uint8_t)(pressure_word >> 8) >> 6;

    //switch ((pressure_word >> 14) & 0x03U)
    switch (status_bits)
    {
        case 0U:
            return PRESSURE_STATUS_OK;

        case 1U:
            return PRESSURE_STATUS_COMMAND_MODE;

        case 2U:
            return PRESSURE_STATUS_STALE_DATA;

        case 3U:
            return PRESSURE_STATUS_DIAGNOSTIC_FAULT;

        default:
            return PRESSURE_STATUS_DIAGNOSTIC_FAULT;
    }
}
