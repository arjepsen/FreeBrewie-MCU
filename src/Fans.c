#include "Fans.h"

#include "Board.h"

#include <stdbool.h>
#include <stdint.h>


// ======================================================================================
//  CONSTANTS
// ======================================================================================

/*
    Carry over the old raw thresholds for now.
    These can be adjusted later once the board-temperature scaling is finalized.
*/
#define FANS_DEFAULT_THERMAL_ON_THRESHOLD_ADC       350U
#define FANS_DEFAULT_THERMAL_OFF_THRESHOLD_ADC      320U


// ======================================================================================
//  TYPES
// ======================================================================================

typedef struct
{
    uint16_t board_temp_adc;
    uint16_t thermal_on_threshold_adc;
    uint16_t thermal_off_threshold_adc;
    uint8_t manual_override;
    uint8_t machine_request;
    uint8_t thermal_request;
    uint8_t board_temp_valid;
    uint8_t outputs_on;
} fans_runtime_t;


// ======================================================================================
//  VARIABLES
// ======================================================================================

static fans_runtime_t fans_runtime =
{
    .board_temp_adc = 0U,
    .thermal_on_threshold_adc = FANS_DEFAULT_THERMAL_ON_THRESHOLD_ADC,
    .thermal_off_threshold_adc = FANS_DEFAULT_THERMAL_OFF_THRESHOLD_ADC,
    .manual_override = 0U,
    .machine_request = 0U,
    .thermal_request = 0U,
    .board_temp_valid = 0U,
    .outputs_on = 0U
};


// ======================================================================================
//  PROTOTYPES
// ======================================================================================

static void fans_apply_outputs(bool enabled);


// ======================================================================================
//  API
// ======================================================================================

/**************************************************************************************************
 * @brief Initialize both fan outputs and reset the fan policy state.
 *
 * The current machine understanding is that there are two physical fans, but they are used as one
 * logical cooling function. This module therefore drives both outputs together while still keeping
 * the hardware mapping explicit in Board.h.
 **************************************************************************************************/
void fans_init()
{
    GPIO_OUTPUT(VENT_FAN_EN_DDR, VENT_FAN_EN_BIT);
    GPIO_OUTPUT(POWER_FAN_EN_DDR, POWER_FAN_EN_BIT);

    fans_runtime.board_temp_adc = 0U;
    fans_runtime.thermal_on_threshold_adc = FANS_DEFAULT_THERMAL_ON_THRESHOLD_ADC;
    fans_runtime.thermal_off_threshold_adc = FANS_DEFAULT_THERMAL_OFF_THRESHOLD_ADC;
    fans_runtime.manual_override = 0U;
    fans_runtime.machine_request = 0U;
    fans_runtime.thermal_request = 0U;
    fans_runtime.board_temp_valid = 0U;
    fans_runtime.outputs_on = 0U;

    fans_apply_outputs(false);
}


/**************************************************************************************************
 * @brief Run the fan policy once.
 *
 * Call this from the main loop. Other code should feed input state into this module with the
 * setters below; the final decision and GPIO writes stay here.
 **************************************************************************************************/
void fans_update()
{
    bool outputs_on;

    if (fans_runtime.board_temp_valid == 0U)
    {
        fans_runtime.thermal_request = 0U;
    }
    else if (fans_runtime.thermal_request == 0U)
    {
        if (fans_runtime.board_temp_adc >= fans_runtime.thermal_on_threshold_adc)
        {
            fans_runtime.thermal_request = 1U;
        }
    }
    else if (fans_runtime.board_temp_adc <= fans_runtime.thermal_off_threshold_adc)
    {
        fans_runtime.thermal_request = 0U;
    }

    outputs_on = (fans_runtime.manual_override != 0U) ||
                 (fans_runtime.machine_request != 0U) ||
                 (fans_runtime.thermal_request != 0U);

    fans_apply_outputs(outputs_on);
}


/**************************************************************************************************
 * @brief Set or clear the manual override request.
 **************************************************************************************************/
void fans_set_manual_override(bool enabled)
{
    fans_runtime.manual_override = enabled ? 1U : 0U;
}


/**************************************************************************************************
 * @brief Toggle the manual override request.
 **************************************************************************************************/
void fans_toggle_manual_override()
{
    fans_runtime.manual_override ^= 1U;
}


/**************************************************************************************************
 * @brief Report whether manual override is active.
 **************************************************************************************************/
bool fans_get_manual_override()
{
    return (fans_runtime.manual_override != 0U);
}


/**************************************************************************************************
 * @brief Set or clear the machine-activity request.
 *
 * The intent is that higher-level logic can say "the machine is active" without needing to know
 * the detailed fan policy. This keeps the long-term main loop simple.
 **************************************************************************************************/
void fans_set_machine_request(bool enabled)
{
    fans_runtime.machine_request = enabled ? 1U : 0U;
}


/**************************************************************************************************
 * @brief Report whether the machine-activity request is active.
 **************************************************************************************************/
bool fans_get_machine_request()
{
    return (fans_runtime.machine_request != 0U);
}


/**************************************************************************************************
 * @brief Feed a new raw ADC sample for the board-temperature signal.
 *
 * The thermal policy currently works directly on the raw ADC domain, matching the old code style.
 * That keeps this module usable now, even before the final engineering-unit scaling is decided.
 **************************************************************************************************/
void fans_set_board_temp_adc(uint16_t board_temp_adc)
{
    fans_runtime.board_temp_adc = board_temp_adc;
    fans_runtime.board_temp_valid = 1U;
}


/**************************************************************************************************
 * @brief Mark the board-temperature signal as unavailable.
 **************************************************************************************************/
void fans_clear_board_temp()
{
    fans_runtime.board_temp_valid = 0U;
    fans_runtime.thermal_request = 0U;
}


/**************************************************************************************************
 * @brief Set the raw ADC hysteresis thresholds used by the thermal fan request.
 *
 * @param on_threshold_adc  Threshold at or above which the thermal request turns on.
 * @param off_threshold_adc Threshold at or below which the thermal request turns off.
 *
 * If off_threshold_adc is above on_threshold_adc, it is clamped down to on_threshold_adc so the
 * hysteresis remains valid.
 **************************************************************************************************/
void fans_set_thermal_thresholds(uint16_t on_threshold_adc, uint16_t off_threshold_adc)
{
    if (off_threshold_adc > on_threshold_adc)
    {
        off_threshold_adc = on_threshold_adc;
    }

    fans_runtime.thermal_on_threshold_adc = on_threshold_adc;
    fans_runtime.thermal_off_threshold_adc = off_threshold_adc;
}


/**************************************************************************************************
 * @brief Report whether the thermal request is currently active.
 **************************************************************************************************/
bool fans_get_thermal_request()
{
    return (fans_runtime.thermal_request != 0U);
}


/**************************************************************************************************
 * @brief Report whether the fan outputs are currently on.
 **************************************************************************************************/
bool fans_are_on()
{
    return (fans_runtime.outputs_on != 0U);
}


/**************************************************************************************************
 * @brief Immediately clear all requests and switch both fan outputs off.
 *
 * This clears the current requests and drives the outputs low right away. A later fans_update()
 * may turn them back on again if new inputs request cooling.
 **************************************************************************************************/
void fans_all_off()
{
    fans_runtime.manual_override = 0U;
    fans_runtime.machine_request = 0U;
    fans_runtime.thermal_request = 0U;
    fans_apply_outputs(false);
}


/**************************************************************************************************
 * @brief Copy the current fan status into a caller-provided structure.
 *
 * @param status Output pointer. Must be valid.
 **************************************************************************************************/
void fans_get_status(fan_status_t *status)
{
    status->manual_override = (fans_runtime.manual_override != 0U);
    status->machine_request = (fans_runtime.machine_request != 0U);
    status->thermal_request = (fans_runtime.thermal_request != 0U);
    status->board_temp_valid = (fans_runtime.board_temp_valid != 0U);
    status->outputs_on = (fans_runtime.outputs_on != 0U);
    status->board_temp_adc = fans_runtime.board_temp_adc;
    status->thermal_on_threshold_adc = fans_runtime.thermal_on_threshold_adc;
    status->thermal_off_threshold_adc = fans_runtime.thermal_off_threshold_adc;
}


// ======================================================================================
//  INTERNAL HELPERS
// ======================================================================================

/**************************************************************************************************
 * @brief Drive both fan outputs to the same state.
 **************************************************************************************************/
static void fans_apply_outputs(bool enabled)
{
    if (enabled == (fans_runtime.outputs_on != 0U))
    {
        return;
    }

    if (enabled)
    {
        GPIO_HIGH(VENT_FAN_EN_PORT, VENT_FAN_EN_BIT);
        GPIO_HIGH(POWER_FAN_EN_PORT, POWER_FAN_EN_BIT);
        fans_runtime.outputs_on = 1U;
    }
    else
    {
        GPIO_LOW(VENT_FAN_EN_PORT, VENT_FAN_EN_BIT);
        GPIO_LOW(POWER_FAN_EN_PORT, POWER_FAN_EN_BIT);
        fans_runtime.outputs_on = 0U;
    }
}
