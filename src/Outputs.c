#include "Outputs.h"

#include "ADC.h"
#include "Supervisor.h"
#include "Fans.h"
#include "Faults.h"
#include "Heaters.h"
#include "Leds.h"
#include "Power.h"
#include "Pumps.h"
#include "Solenoids.h"
#include "Valves.h"

#include <stdbool.h>
#include <stdint.h>

#define OUTPUTS_SAFE_ALLOWED_FLAGS               (SUPERVISOR_OUTPUT_FLAG_MCU_LED1 | \
                                                  SUPERVISOR_OUTPUT_FLAG_PRECHARGE_5V | \
                                                  SUPERVISOR_OUTPUT_FLAG_PWR_5V | \
                                                  SUPERVISOR_OUTPUT_FLAG_PWR_12V | \
                                                  SUPERVISOR_OUTPUT_FLAG_POWER_LED | \
                                                  SUPERVISOR_OUTPUT_FLAG_DRAIN_LED | \
                                                  SUPERVISOR_OUTPUT_FLAG_FAN_ENABLE)

#define OUTPUTS_SOLENOID_FLAGS                   (SUPERVISOR_OUTPUT_FLAG_BREW_INLET | \
                                                  SUPERVISOR_OUTPUT_FLAG_COOLING_INLET)

static supervisor_output_request_t outputs_last_applied =
{
    .binary_flags = SUPERVISOR_OUTPUT_FLAG_NONE,
    .mode_flags = SUPERVISOR_OUTPUT_MODE_NONE,
    .mash_pump_speed = 0U,
    .boil_pump_speed = 0U,
    .valve_id = 0U,
    .valve_position = 0U,
    .heater_request = SUPERVISOR_HEATER_REQUEST_NONE
};

/*********************************************************************
 * @brief Initialize outputs.
 *********************************************************************/
void outputs_init()
{
    /* Bring all hardware-owning output modules up before the first apply pass. */
    adc_init();
    leds_init();
    power_init();
    solenoids_init();
    pumps_init();
    heaters_init();
    fans_init();
    valves_init();

    outputs_last_applied = (supervisor_output_request_t){
        SUPERVISOR_OUTPUT_FLAG_NONE,
        SUPERVISOR_OUTPUT_MODE_SAFE_ALL_OFF,
        0U,
        0U,
        0U,
        0U,
        SUPERVISOR_HEATER_REQUEST_NONE
    };

    MCU_LED1_OFF();
    MCU_LED2_OFF();
    POWER_LED_OFF();
    DRAIN_LED_OFF();

    solenoids_all_off();
    pumps_all_off();
    heaters_all_off();
    fans_set_machine_request(false);
}

/*********************************************************************
 * @brief Apply outputs.
 *********************************************************************/
void outputs_apply()
{
    const supervisor_output_request_t *request = &supervisor_runtime.requested_outputs;
    uint16_t flags = request->binary_flags;
    uint16_t changed_flags;
    uint8_t mash_pump_speed = request->mash_pump_speed;
    uint8_t boil_pump_speed = request->boil_pump_speed;
    uint8_t valve_id = request->valve_id;
    uint8_t valve_position = request->valve_position;
    supervisor_heater_request_t heater_request = request->heater_request;
    bool safe_outputs = (((request->mode_flags & SUPERVISOR_OUTPUT_MODE_SAFE_ALL_OFF) != 0U) ||
                         faults_runtime.force_safe_outputs);

    /* Final safety clamp: even if upstream logic asked for more, only a small
     * explicitly safe subset may survive this stage. */
    if (safe_outputs)
    {
        flags &= OUTPUTS_SAFE_ALLOWED_FLAGS;
        mash_pump_speed = 0U;
        boil_pump_speed = 0U;
        valve_id = 0U;
        valve_position = 0U;
        heater_request = SUPERVISOR_HEATER_REQUEST_NONE;
    }

    /* Track binary output edges so modules are only poked when needed. */
    changed_flags = (uint16_t)(outputs_last_applied.binary_flags ^ flags);

    /* Apply the simple direct GPIO-style outputs first. */
    if ((flags & SUPERVISOR_OUTPUT_FLAG_PRECHARGE_5V) != 0U)
    {
        PWR_PRECHARGE_5V_ON();
    }
    else
    {
        PWR_PRECHARGE_5V_OFF();
    }

    if ((flags & SUPERVISOR_OUTPUT_FLAG_PWR_5V) != 0U)
    {
        PWR_5V_ON();
    }
    else
    {
        PWR_5V_OFF();
    }

    if ((flags & SUPERVISOR_OUTPUT_FLAG_PWR_12V) != 0U)
    {
        PWR_12V_ON();
    }
    else
    {
        PWR_12V_OFF();
    }

    if ((flags & SUPERVISOR_OUTPUT_FLAG_PWR_6V5_SERVO) != 0U)
    {
        PWR_6V5_SERVO_ON();
    }
    else
    {
        PWR_6V5_SERVO_OFF();
    }

    if ((flags & SUPERVISOR_OUTPUT_FLAG_MCU_LED1) != 0U)
    {
        MCU_LED1_ON();
    }
    else
    {
        MCU_LED1_OFF();
    }

    if ((flags & SUPERVISOR_OUTPUT_FLAG_POWER_LED) != 0U)
    {
        POWER_LED_ON();
    }
    else
    {
        POWER_LED_OFF();
    }

    if ((flags & SUPERVISOR_OUTPUT_FLAG_DRAIN_LED) != 0U)
    {
        DRAIN_LED_ON();
    }
    else
    {
        DRAIN_LED_OFF();
    }

    /* Solenoids are edge-driven: if no solenoid bits remain set, shut the pair off. */
    if ((flags & OUTPUTS_SOLENOID_FLAGS) == 0U)
    {
        if ((outputs_last_applied.binary_flags & OUTPUTS_SOLENOID_FLAGS) != 0U)
        {
            solenoids_all_off();
        }
    }
    else
    {
        if ((changed_flags & SUPERVISOR_OUTPUT_FLAG_BREW_INLET) != 0U)
        {
            (void)solenoids_set(SOLENOID_ID_BREW_INLET, ((flags & SUPERVISOR_OUTPUT_FLAG_BREW_INLET) != 0U));
        }

        if ((changed_flags & SUPERVISOR_OUTPUT_FLAG_COOLING_INLET) != 0U)
        {
            (void)solenoids_set(SOLENOID_ID_COOLING_INLET, ((flags & SUPERVISOR_OUTPUT_FLAG_COOLING_INLET) != 0U));
        }
    }

    /* Pumps only receive new commands on enable edges or speed changes. */
    if ((flags & SUPERVISOR_OUTPUT_FLAG_MASH_PUMP_ENABLE) != 0U)
    {
        if (((outputs_last_applied.binary_flags & SUPERVISOR_OUTPUT_FLAG_MASH_PUMP_ENABLE) == 0U) ||
            (outputs_last_applied.mash_pump_speed != mash_pump_speed))
        {
            (void)pumps_set_speed(PUMP_ID_MASH, mash_pump_speed);
            (void)pumps_start(PUMP_ID_MASH);
        }
    }
    else if ((outputs_last_applied.binary_flags & SUPERVISOR_OUTPUT_FLAG_MASH_PUMP_ENABLE) != 0U)
    {
        (void)pumps_stop(PUMP_ID_MASH);
    }

    if ((flags & SUPERVISOR_OUTPUT_FLAG_BOIL_PUMP_ENABLE) != 0U)
    {
        if (((outputs_last_applied.binary_flags & SUPERVISOR_OUTPUT_FLAG_BOIL_PUMP_ENABLE) == 0U) ||
            (outputs_last_applied.boil_pump_speed != boil_pump_speed))
        {
            (void)pumps_set_speed(PUMP_ID_BOIL, boil_pump_speed);
            (void)pumps_start(PUMP_ID_BOIL);
        }
    }
    else if ((outputs_last_applied.binary_flags & SUPERVISOR_OUTPUT_FLAG_BOIL_PUMP_ENABLE) != 0U)
    {
        (void)pumps_stop(PUMP_ID_BOIL);
    }

    /* Heaters are mutually mediated through the heater module itself. */
    if ((flags & SUPERVISOR_OUTPUT_FLAG_HEATER_ENABLE) == 0U)
    {
        if ((outputs_last_applied.binary_flags & SUPERVISOR_OUTPUT_FLAG_HEATER_ENABLE) != 0U)
        {
            heaters_all_off();
        }
    }
    else if (((outputs_last_applied.binary_flags & SUPERVISOR_OUTPUT_FLAG_HEATER_ENABLE) == 0U) ||
             (outputs_last_applied.heater_request != heater_request))
    {
        if (heater_request == SUPERVISOR_HEATER_REQUEST_MASH)
        {
            (void)heaters_set(HEATER_ID_MASH, true);
        }
        else if (heater_request == SUPERVISOR_HEATER_REQUEST_BOIL)
        {
            (void)heaters_set(HEATER_ID_BOIL, true);
        }
        else
        {
            heaters_all_off();
        }
    }

    /* Fans keep their own local policy; this just updates the machine request input. */
    if ((changed_flags & SUPERVISOR_OUTPUT_FLAG_FAN_ENABLE) != 0U)
    {
        fans_set_machine_request(((flags & SUPERVISOR_OUTPUT_FLAG_FAN_ENABLE) != 0U));
    }

    if ((valve_id != 0U) &&
        ((outputs_last_applied.valve_id != valve_id) ||
         (outputs_last_applied.valve_position != valve_position)) &&
        !valves_is_busy())
    {
        (void)valves_start_move_to_position((valve_id_t)valve_id,
                                            (valve_position_t)valve_position);
    }

    /* Store the post-gating state so the next pass can detect real changes. */
    outputs_last_applied.binary_flags = flags;
    outputs_last_applied.mode_flags = request->mode_flags;
    outputs_last_applied.mash_pump_speed = mash_pump_speed;
    outputs_last_applied.boil_pump_speed = boil_pump_speed;
    outputs_last_applied.valve_id = valve_id;
    outputs_last_applied.valve_position = valve_position;
    outputs_last_applied.heater_request = heater_request;
}
