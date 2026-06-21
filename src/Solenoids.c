#include "Solenoids.h"

#include "ADC.h"
#include "Board.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ======================================================================================
//  CONSTANTS
// ======================================================================================

#define SOLENOIDS_ADC_OWNER ADC_OWNER_SOLENOIDS

// ======================================================================================
//  TYPES
// ======================================================================================

typedef struct
{
    volatile uint8_t *port;
    volatile uint8_t *ddr;
    uint8_t bit;
    uint8_t adc_channel;
} solenoid_hw_t;

typedef struct
{
    uint16_t last_current_adc;
    uint8_t enabled;
} solenoid_runtime_t;

// ======================================================================================
//  PROTOTYPES
// ======================================================================================



// ======================================================================================
//  VARIABLES
// ======================================================================================

static const solenoid_hw_t solenoids_hw[SOLENOID_ID_COUNT] =
    {
        [SOLENOID_ID_BREW_INLET] =
            {
                .port = &BREW_INLET_PORT,
                .ddr = &BREW_INLET_DDR,
                .bit = BREW_INLET_BIT,
                .adc_channel = BREW_INLET_CURRENT_SENSE_ADC_CHANNEL},

        [SOLENOID_ID_COOLING_INLET] =
            {
                .port = &COOLING_INLET_PORT,
                .ddr = &COOLING_INLET_DDR,
                .bit = COOLING_INLET_BIT,
                .adc_channel = COOLING_INLET_CURRENT_SENSE_ADC_CHANNEL}};

static solenoid_runtime_t solenoids_runtime[SOLENOID_ID_COUNT];

// ======================================================================================
//  MAIN API
// ======================================================================================

/**************************************************************************************************
 * @brief Initialize the two inlet solenoid outputs.
 *
 * Both solenoids are configured as outputs and driven low so the machine starts with both
 * inlet paths de-energized.
 **************************************************************************************************/
void solenoids_init()
{
    uint8_t index;

    for (index = 0U; index < (uint8_t)SOLENOID_ID_COUNT; index++)
    {
        GPIO_OUTPUT(*solenoids_hw[index].ddr, solenoids_hw[index].bit);
        GPIO_LOW(*solenoids_hw[index].port, solenoids_hw[index].bit);

        solenoids_runtime[index].enabled = 0U;
        solenoids_runtime[index].last_current_adc = 0U;
    }
}

/**************************************************************************************************
 * @brief Open or Close one solenoid.
 *
 * @param solenoid_id Solenoid to control.
 * @param open        true to energize the solenoid, false to de-energize it.
 *
 * @return true if the solenoid id was valid, otherwise false.
 **************************************************************************************************/
bool solenoids_set(solenoid_id_t solenoid_id, bool open)
{
    if (solenoid_id >= SOLENOID_ID_COUNT)
        return false;

    if (open)
    {
        GPIO_HIGH(*solenoids_hw[solenoid_id].port, solenoids_hw[solenoid_id].bit);
        solenoids_runtime[solenoid_id].enabled = 1U;
    }
    else
    {
        GPIO_LOW(*solenoids_hw[solenoid_id].port, solenoids_hw[solenoid_id].bit);
        solenoids_runtime[solenoid_id].enabled = 0U;
    }

    return true;
}

/**************************************************************************************************
 * @brief Read the commanded on/off state of one solenoid.
 *
 * @param solenoid_id Solenoid to query.
 * @param enabled     Output pointer for the current commanded state.
 *
 * @return true on success, otherwise false.
 **************************************************************************************************/
bool solenoids_get(solenoid_id_t solenoid_id, bool *enabled)
{
    if ((solenoid_id >= SOLENOID_ID_COUNT) || (enabled == NULL))
    {
        return false;
    }

    *enabled = (solenoids_runtime[solenoid_id].enabled != 0U);
    return true;
}

/**************************************************************************************************
 * @brief Turn both solenoids off.
 **************************************************************************************************/
void solenoids_all_off()
{

    GPIO_LOW(*solenoids_hw[SOLENOID_ID_BREW_INLET].port, solenoids_hw[SOLENOID_ID_BREW_INLET].bit);
    solenoids_runtime[SOLENOID_ID_BREW_INLET].enabled = 0U;

    GPIO_LOW(*solenoids_hw[SOLENOID_ID_COOLING_INLET].port, solenoids_hw[SOLENOID_ID_COOLING_INLET].bit);
    solenoids_runtime[SOLENOID_ID_COOLING_INLET].enabled = 0U;
}

/**************************************************************************************************
 * @brief Take one blocking ADC sample from the selected solenoid current-sense channel.
 *
 * The sampled value is stored internally as the latest current reading for later status queries.
 *
 * @param solenoid_id Solenoid whose current-sense channel should be sampled.
 * @param current_adc Output pointer for the ADC sample.
 *
 * @return true on success, otherwise false.
 **************************************************************************************************/
bool solenoids_sample_current(solenoid_id_t solenoid_id, uint16_t *current_adc)
{
    uint16_t sample;

    if ((solenoid_id >= SOLENOID_ID_COUNT) || (current_adc == NULL))
    {
        return false;
    }

    if (!adc_acquire(SOLENOIDS_ADC_OWNER))
    {
        return false;
    }

    if (!adc_read_blocking(SOLENOIDS_ADC_OWNER, solenoids_hw[solenoid_id].adc_channel, &sample))
    {
        adc_release(SOLENOIDS_ADC_OWNER);
        return false;
    }

    adc_release(SOLENOIDS_ADC_OWNER);

    solenoids_runtime[solenoid_id].last_current_adc = sample;
    *current_adc = sample;
    return true;
}

/**************************************************************************************************
 * @brief Get a compact status snapshot for both solenoids.
 *
 * @param status Output pointer for the returned status.
 *
 * @return true on success, otherwise false.
 **************************************************************************************************/
bool solenoids_get_status(solenoids_status_t *status)
{
    if (status == NULL)
    {
        return false;
    }

    status->brew_inlet_on = (solenoids_runtime[SOLENOID_ID_BREW_INLET].enabled != 0U);
    status->cooling_inlet_on = (solenoids_runtime[SOLENOID_ID_COOLING_INLET].enabled != 0U);

    status->brew_inlet_current_adc = solenoids_runtime[SOLENOID_ID_BREW_INLET].last_current_adc;
    status->cooling_inlet_current_adc = solenoids_runtime[SOLENOID_ID_COOLING_INLET].last_current_adc;

    return true;
}

