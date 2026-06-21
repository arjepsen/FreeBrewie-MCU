#include "Heaters.h"

#include "ADC.h"
#include "Board.h"

#include <avr/io.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <util/atomic.h>


// ======================================================================================
//  CONSTANTS
// ======================================================================================

#define HEATERS_ADC_OWNER                               ADC_OWNER_HEATERS


// ======================================================================================
//  TYPES
// ======================================================================================

typedef struct
{
    uint16_t min_sample;
    uint16_t max_sample;
    uint16_t avg_sample;
    uint16_t span_sample;
    uint16_t estimated_power_w;
    uint32_t sum_samples;
    uint16_t sample_count;
    uint8_t estimated_current_x10;
    uint8_t discard_windows;
    bool heater_state[2];
    // bool mash_on;
    // bool boil_on;
    bool load_present;
    bool measurement_requested;
    bool measurement_active;
    bool measurement_valid;
} heaters_runtime_t;


// ======================================================================================
//  PROTOTYPES
// ======================================================================================

static void heaters_measurement_reset();
static void heaters_measurement_finish();
static void heaters_adc_callback(uint16_t sample, void *context);
static inline void heaters_apply_outputs();


// ======================================================================================
//  VARIABLES
// ======================================================================================

static heaters_runtime_t heaters_runtime =
{
    .min_sample = 0U,
    .max_sample = 0U,
    .avg_sample = 0U,
    .span_sample = 0U,
    .estimated_power_w = 0U,
    .sum_samples = 0UL,
    .sample_count = 0U,
    .estimated_current_x10 = 0U,
    .discard_windows = 0U,
    .heater_state = {false, false},
    // .mash_on = false,
    // .boil_on = false,
    .load_present = false,
    .measurement_requested = false,
    .measurement_active = false,
    .measurement_valid = false
};


// ======================================================================================
//  MAIN
// ======================================================================================

/**************************************************************************************************
 * @brief Initialize heater outputs and runtime state.
 **************************************************************************************************/
void heaters_init()
{
    GPIO_OUTPUT(MASH_HTR2_CTRL_DDR, MASH_HTR2_CTRL_BIT);
    GPIO_OUTPUT(BOIL_HTR1_CTRL_DDR, BOIL_HTR1_CTRL_BIT);

    heaters_runtime.min_sample = 0U;
    heaters_runtime.max_sample = 0U;
    heaters_runtime.avg_sample = 0U;
    heaters_runtime.span_sample = 0U;
    heaters_runtime.estimated_power_w = 0U;
    heaters_runtime.sum_samples = 0UL;
    heaters_runtime.sample_count = 0U;
    heaters_runtime.estimated_current_x10 = 0U;
    heaters_runtime.discard_windows = 0U;
    heaters_runtime.heater_state[HEATER_ID_MASH] = false;
    heaters_runtime.heater_state[HEATER_ID_BOIL] = false;
    // heaters_runtime.mash_on = false;
    // heaters_runtime.boil_on = false;
    heaters_runtime.load_present = false;
    heaters_runtime.measurement_requested = false;
    heaters_runtime.measurement_active = false;
    heaters_runtime.measurement_valid = false;

    heaters_apply_outputs();
}


/**************************************************************************************************
 * @brief Enable or disable one heater.
 *
 * Enabling one heater clears the other heater command.
 * The first completed AC_MEAS window after a state change is discarded because it may straddle the
 * transition.
 *
 * @param heater_id Selected heater.
 * @param enabled   True to enable, false to disable.
 *
 * @return True on success, false on invalid heater_id.
 **************************************************************************************************/
bool heaters_set(heater_id_t heater_id, bool enabled)
{
    if (heater_id >= HEATER_ID_COUNT)
        return false;

    heater_id_t other_heater_id = (heater_id == HEATER_ID_MASH) ? HEATER_ID_BOIL : HEATER_ID_MASH;
    bool *other_heater = &heaters_runtime.heater_state[other_heater_id];

    // Check if we're changing any state.
    bool state_change = 
        (heaters_runtime.heater_state[heater_id] != enabled) ||
        (enabled && *other_heater);

    // Set the specified heaters new state.
    heaters_runtime.heater_state[heater_id] = enabled;

    // Disable the other heater if the specified was enabled.
    *other_heater = enabled ? false : *other_heater;

    if (state_change)
    {
        heaters_runtime.discard_windows = 1U;
        heaters_runtime.measurement_valid = false;  
    }
    
    heaters_apply_outputs();
    return true;
}


/**************************************************************************************************
 * @brief Turn both heaters off immediately.
 **************************************************************************************************/
void heaters_all_off()
{
    if (heaters_runtime.heater_state[HEATER_ID_MASH] || 
        heaters_runtime.heater_state[HEATER_ID_BOIL])
    {
        heaters_runtime.discard_windows = 1U;
        heaters_runtime.measurement_valid = false;
    }

    heaters_runtime.heater_state[HEATER_ID_MASH] = false;
    heaters_runtime.heater_state[HEATER_ID_BOIL] = false;
    heaters_apply_outputs();
}


/**************************************************************************************************
 * @brief Request one contiguous AC_MEAS window.
 *
 * The window is sampled back-to-back through the ADC callback path, so it is not paced by the main
 * loop. This keeps timing stable without a new timer ISR.
 *
 * @return True when a new measurement request was accepted.
 **************************************************************************************************/
bool heaters_request_measurement()
{
    if (heaters_runtime.measurement_requested || heaters_runtime.measurement_active)
    {
        return false;
    }

    heaters_runtime.measurement_requested = true;
    heaters_runtime.measurement_valid = false;
    return true;
}


/**************************************************************************************************
 * @brief Advance heater background work.
 *
 * Starts a requested AC_MEAS window when the ADC becomes available. Once started, the whole window
 * is collected by chained ADC callbacks without depending on main-loop timing.
 **************************************************************************************************/
void heaters_update()
{
    if (!heaters_runtime.measurement_requested)
    {
        return;
    }

    if (!adc_acquire(HEATERS_ADC_OWNER))
    {
        return;
    }

    heaters_measurement_reset();
    heaters_runtime.measurement_requested = false;
    heaters_runtime.measurement_active = true;

    if (!adc_start_conversion(HEATERS_ADC_OWNER,
                              AC_MEAS_ADC_CHANNEL,
                              heaters_adc_callback,
                              NULL))
    {
        heaters_runtime.measurement_active = false;
        adc_release(HEATERS_ADC_OWNER);
    }
}


/**************************************************************************************************
 * @brief Copy the current heater status.
 *
 * @param status Output pointer.
 *
 * @return True on success, false when @p status is NULL.
 **************************************************************************************************/
bool heaters_get_status(heaters_status_t *status)
{
    if (status == NULL)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        status->min_sample = heaters_runtime.min_sample;
        status->max_sample = heaters_runtime.max_sample;
        status->avg_sample = heaters_runtime.avg_sample;
        status->span_sample = heaters_runtime.span_sample;
        status->estimated_power_w = heaters_runtime.estimated_power_w;
        status->estimated_current_x10 = heaters_runtime.estimated_current_x10;
        status->mash_on = heaters_runtime.heater_state[HEATER_ID_MASH];
        status->boil_on = heaters_runtime.heater_state[HEATER_ID_BOIL];
        status->load_present = heaters_runtime.load_present;
        status->measurement_active = heaters_runtime.measurement_active || heaters_runtime.measurement_requested;
        status->measurement_valid = heaters_runtime.measurement_valid;
    }

    return true;
}


// ======================================================================================
//  INTERNAL HELPERS
// ======================================================================================

/**************************************************************************************************
 * @brief Reset the working accumulator for one AC_MEAS window.
 **************************************************************************************************/
static void heaters_measurement_reset()
{
    heaters_runtime.min_sample = 0U;
    heaters_runtime.max_sample = 0U;
    heaters_runtime.avg_sample = 0U;
    heaters_runtime.span_sample = 0U;
    heaters_runtime.estimated_power_w = 0U;
    heaters_runtime.sum_samples = 0UL;
    heaters_runtime.sample_count = 0U;
    heaters_runtime.estimated_current_x10 = 0U;
    heaters_runtime.load_present = false;
}


/**************************************************************************************************
 * @brief Finalize one completed AC_MEAS window.
 *
 * Efficiency note:
 * - the average uses a right shift because the window length is 256 samples
 * - the remaining divide-by-10 for power happens only once per completed window, not per sample
 **************************************************************************************************/
static void heaters_measurement_finish()
{
    uint16_t signal_level;
    uint16_t scaled_current_x10;

    heaters_runtime.avg_sample = (uint16_t)(heaters_runtime.sum_samples >> HEATERS_AC_SAMPLE_COUNT_SHIFT);
    heaters_runtime.span_sample = (uint16_t)(heaters_runtime.max_sample - heaters_runtime.min_sample);
    heaters_runtime.load_present = (heaters_runtime.span_sample >= HEATERS_LOAD_PRESENT_SPAN_THRESHOLD);

    if (heaters_runtime.avg_sample > HEATERS_AC_BASELINE_ADC_COUNTS)
    {
        signal_level = (uint16_t)(heaters_runtime.avg_sample - HEATERS_AC_BASELINE_ADC_COUNTS);
    }
    else
    {
        signal_level = 0U;
    }

    scaled_current_x10 = (uint16_t)((uint16_t)(signal_level * HEATERS_EST_CURRENT_SCALE_NUMERATOR) >>
                                    HEATERS_EST_CURRENT_SCALE_SHIFT);

    if (scaled_current_x10 > 255U)
    {
        scaled_current_x10 = 255U;
    }

    heaters_runtime.estimated_current_x10 = (uint8_t)scaled_current_x10;
    heaters_runtime.estimated_power_w =
        (uint16_t)(((uint32_t)heaters_runtime.estimated_current_x10 * HEATERS_NOMINAL_MAINS_VOLTAGE + 5UL) / 10UL);

    if (heaters_runtime.discard_windows != 0U)
    {
        heaters_runtime.discard_windows--;
        heaters_runtime.measurement_valid = false;
    }
    else
    {
        heaters_runtime.measurement_valid = true;
    }
}


/**************************************************************************************************
 * @brief ADC completion callback for one AC_MEAS sample.
 *
 * Chains the next conversion immediately so one requested window is sampled contiguously.
 *
 * @param sample  ADC sample.
 * @param context Unused.
 **************************************************************************************************/
static void heaters_adc_callback(uint16_t sample, void *context)
{
    (void)context;

    if (heaters_runtime.sample_count == 0U)
    {
        heaters_runtime.min_sample = sample;
        heaters_runtime.max_sample = sample;
    }
    else
    {
        if (sample < heaters_runtime.min_sample)
        {
            heaters_runtime.min_sample = sample;
        }

        if (sample > heaters_runtime.max_sample)
        {
            heaters_runtime.max_sample = sample;
        }
    }

    heaters_runtime.sum_samples += sample;
    heaters_runtime.sample_count++;

    if (heaters_runtime.sample_count < HEATERS_AC_SAMPLE_COUNT)
    {
        if (adc_start_conversion(HEATERS_ADC_OWNER,
                                 AC_MEAS_ADC_CHANNEL,
                                 heaters_adc_callback,
                                 NULL))
        {
            return;
        }
    }

    heaters_runtime.measurement_active = false;
    heaters_measurement_finish();
    adc_release(HEATERS_ADC_OWNER);
}


/**************************************************************************************************
 * @brief Apply the current commanded heater states to the GPIO outputs.
 *
 * Outputs are active-high according to the traced board map.
 **************************************************************************************************/
static inline void heaters_apply_outputs()
{
    if (heaters_runtime.heater_state[HEATER_ID_MASH] && 
        heaters_runtime.heater_state[HEATER_ID_BOIL])
    {
        return;
    }

    PORTG = (PORTG & (uint8_t)~_BV(PG5)) | (heaters_runtime.heater_state[HEATER_ID_MASH] ? _BV(PG5) : 0U);
    PORTE = (PORTE & (uint8_t)~_BV(PE2)) | (heaters_runtime.heater_state[HEATER_ID_BOIL] ? _BV(PE2) : 0U);
}
