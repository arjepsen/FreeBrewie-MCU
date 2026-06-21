#include "Valves.h"

#include "ADC.h"
#include "Power.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <util/atomic.h>


// ======================================================================================
//  CONSTANTS
// ======================================================================================

#define VALVES_TIMER_PRESCALER_BITS              ((1U << CS41))
#define VALVES_TIMER_TICKS_PER_US                2U
#define VALVES_TIMER_TICKS_PER_MS                (1000UL * VALVES_TIMER_TICKS_PER_US)

#define VALVES_MIN_PULSE_US                      500U
#define VALVES_MAX_PULSE_US                      2500U
#define VALVES_MIN_FRAME_US                      5000U
#define VALVES_MAX_FRAME_US                      30000U
#define VALVES_MIN_BURST_MS                      20U
#define VALVES_MAX_BURST_MS                      5000U
#define VALVES_MAX_POWER_SETTLE_MS               ((uint16_t)(65535UL / VALVES_TIMER_TICKS_PER_MS))

#define VALVES_DEFAULT_OPEN_US                   740U
#define VALVES_DEFAULT_CLOSE_US                  1780U
#define VALVES_DEFAULT_CLOSE_HARD_US             1900U
#define VALVES_DEFAULT_SPARGE_OPEN_US            740U
#define VALVES_DEFAULT_SPARGE_CLOSE_US           1260U
#define VALVES_DEFAULT_FRAME_US                  20000U
#define VALVES_DEFAULT_BURST_MS                  680U
#define VALVES_DEFAULT_POWER_SETTLE_MS           20U
#define VALVES_DEFAULT_STARTUP_FRAMES_TO_IGNORE  2U

#define VALVES_CURRENT_SAMPLE_OFFSET_US          250U
#define VALVES_CURRENT_SAMPLE_MIN_PULSE_US       (VALVES_CURRENT_SAMPLE_OFFSET_US + 50U)

#define VALVES_ADC_INVALID_CHANNEL               0xFFU

#define VALVES_DEFAULT_CONFIG                                        \
    {                                                                \
        .open_us = VALVES_DEFAULT_OPEN_US,                           \
        .close_us = VALVES_DEFAULT_CLOSE_US,                         \
        .close_hard_us = VALVES_DEFAULT_CLOSE_HARD_US,               \
        .sparge_open_us = VALVES_DEFAULT_SPARGE_OPEN_US,             \
        .sparge_close_us = VALVES_DEFAULT_SPARGE_CLOSE_US,           \
        .frame_us = VALVES_DEFAULT_FRAME_US,                         \
        .burst_ms = VALVES_DEFAULT_BURST_MS,                         \
        .power_settle_ms = VALVES_DEFAULT_POWER_SETTLE_MS,           \
        .startup_frames_to_ignore = VALVES_DEFAULT_STARTUP_FRAMES_TO_IGNORE, \
        .current_monitoring_enabled = true                           \
    }


// ======================================================================================
//  TYPES
// ======================================================================================

typedef enum
{
    VALVES_MOVE_STATE_IDLE = 0,
    VALVES_MOVE_STATE_SETTLE,
    VALVES_MOVE_STATE_PULSE_HIGH,
    VALVES_MOVE_STATE_FRAME_GAP
} valves_move_state_t;

typedef struct
{
    volatile uint8_t *port;
    volatile uint8_t *ddr;
    uint8_t bit;
    bool installed;
    uint8_t current_sense;
    valve_config_t config;
    uint8_t last_position;
    uint16_t last_current_adc;
    uint16_t peak_current_adc;
} valve_runtime_t;

typedef struct
{
    uint8_t pending;
    uint8_t valve_id;
    uint8_t target_position;
    uint8_t success;
    uint32_t sample_sum;
    uint16_t sample_count;
    uint16_t peak_current_adc;
} valves_completed_move_t;

typedef struct
{
    uint8_t busy;
    uint8_t sample_pending;
    uint8_t sample_started;
    uint8_t state;
    uint8_t frame_index;
    uint8_t frame_count;
    uint8_t valve_id;
    uint8_t target_position;
    uint8_t active_bit;
    uint8_t current_channel;
    uint8_t current_monitoring_enabled;
    volatile uint8_t *active_port;
    uint16_t pulse_ticks;
    uint16_t frame_ticks;
    uint16_t sample_offset_ticks;
    uint32_t sample_sum;
    uint16_t stable_peak_adc;
    uint16_t stable_sample_count;
} valves_active_move_t;


// ======================================================================================
//  PROTOTYPES
// ======================================================================================

static void valves_timer4_init();
static void valves_timer4_schedule_a_from_now(uint16_t ticks_from_now);
static void valves_timer4_schedule_a_from_now_isr(uint16_t ticks_from_now);
static void valves_timer4_schedule_b_from_now_isr(uint16_t ticks_from_now);

static void valves_adc_callback(uint16_t sample, void *context);

static inline bool valves_is_valid_id(valve_id_t valve_id);
static inline bool valves_is_valid_position(valve_position_t position);
static bool valves_is_valid_pulse_us(uint16_t pulse_us);
static bool valves_is_valid_frame_us(uint16_t frame_us);
static bool valves_is_valid_burst_ms(uint16_t burst_ms);
static bool valves_is_valid_power_settle_ms(uint16_t power_settle_ms);

static uint16_t valves_get_position_pulse_us(const valve_config_t *config, valve_position_t position);
static uint8_t valves_get_adc_channel_for_source(uint8_t source);

static inline void valves_set_output_low_unchecked();
static inline void valves_set_output_high_unchecked();
static void valves_finish_move();
static void valves_reset_active_move();
static void valves_reset_completed_move();
static void valves_store_completed_move(valve_id_t valve_id,
                                        valve_position_t target_position,
                                        bool success,
                                        uint32_t sample_sum,
                                        uint16_t sample_count,
                                        uint16_t peak_current_adc);


// ======================================================================================
//  VARIABLES
// ======================================================================================

static valve_runtime_t valves_runtime[VALVE_ID_COUNT] =
{
    [VALVE_ID_NONE] =
    {
        .port = NULL,
        .ddr = NULL,
        .bit = 0U,
        .installed = false,
        .current_sense = VALVE_CURRENT_SENSE_NONE,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_MASH_INLET] =
    {
        .port = &MASH_IN_VALVE_PORT,
        .ddr = &MASH_IN_VALVE_DDR,
        .bit = MASH_IN_VALVE_BIT,
        .installed = true,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_BOIL_RETURN] =
    {
        .port = &BOIL_RTN_VALVE_PORT,
        .ddr = &BOIL_RTN_VALVE_DDR,
        .bit = BOIL_RTN_VALVE_BIT,
        .installed = true,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_OUTLET] =
    {
        .port = &OUTLET_VALVE_PORT,
        .ddr = &OUTLET_VALVE_DDR,
        .bit = OUTLET_VALVE_BIT,
        .installed = true,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_COOLER] =
    {
        .port = &COOL_VALVE_PORT,
        .ddr = &COOL_VALVE_DDR,
        .bit = COOL_VALVE_BIT,
        .installed = true,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_VALVE_5] =
    {
        .port = &VALVE_5_PORT,
        .ddr = &VALVE_5_DDR,
        .bit = VALVE_5_BIT,
        .installed = false,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_HOP4] =
    {
        .port = &HOP4_VALVE_PORT,
        .ddr = &HOP4_VALVE_DDR,
        .bit = HOP4_VALVE_BIT,
        .installed = true,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_HOP3] =
    {
        .port = &HOP3_VALVE_PORT,
        .ddr = &HOP3_VALVE_DDR,
        .bit = HOP3_VALVE_BIT,
        .installed = true,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_HOP2] =
    {
        .port = &HOP2_VALVE_PORT,
        .ddr = &HOP2_VALVE_DDR,
        .bit = HOP2_VALVE_BIT,
        .installed = true,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_HOP1] =
    {
        .port = &HOP1_VALVE_PORT,
        .ddr = &HOP1_VALVE_DDR,
        .bit = HOP1_VALVE_BIT,
        .installed = true,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_MASH_RETURN] =
    {
        .port = &MASH_RTN_VALVE_PORT,
        .ddr = &MASH_RTN_VALVE_DDR,
        .bit = MASH_RTN_VALVE_BIT,
        .installed = true,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    },
    [VALVE_ID_BOIL_INLET] =
    {
        .port = &BOIL_INLET_VALVE_PORT,
        .ddr = &BOIL_INLET_VALVE_DDR,
        .bit = BOIL_INLET_VALVE_BIT,
        .installed = true,
        .current_sense = VALVE_CURRENT_SENSE_VALVES,
        .config = VALVES_DEFAULT_CONFIG,
        .last_position = VALVE_POSITION_CLOSE,
        .last_current_adc = 0U,
        .peak_current_adc = 0U
    }
};

static volatile valves_active_move_t valves_active_move =
{
    .busy = 0U,
    .sample_pending = 0U,
    .sample_started = 0U,
    .state = VALVES_MOVE_STATE_IDLE,
    .frame_index = 0U,
    .frame_count = 0U,
    .valve_id = VALVE_ID_NONE,
    .target_position = VALVE_POSITION_CLOSE,
    .active_bit = 0U,
    .current_channel = VALVES_ADC_INVALID_CHANNEL,
    .current_monitoring_enabled = 0U,
    .active_port = NULL,
    .pulse_ticks = 0U,
    .frame_ticks = 0U,
    .sample_offset_ticks = 0U,
    .sample_sum = 0UL,
    .stable_peak_adc = 0U,
    .stable_sample_count = 0U
};

static volatile valves_completed_move_t valves_completed_move =
{
    .pending = 0U,
    .valve_id = VALVE_ID_NONE,
    .target_position = VALVE_POSITION_CLOSE,
    .success = 0U,
    .sample_sum = 0UL,
    .sample_count = 0U,
    .peak_current_adc = 0U
};


// ======================================================================================
//  MAIN API
// ======================================================================================

/**************************************************************************************************
 * @brief Initialize valve GPIOs and Timer4 timing.
 *
 * All valve outputs are configured low. The valve driver is non-blocking and uses Timer4 interrupts,
 * so global interrupts must be enabled after init.
 **************************************************************************************************/
void valves_init()
{
    uint8_t index;

    valves_timer4_init();

    for (index = 0U; index < (uint8_t)VALVE_ID_COUNT; index++)
    {
        if ((valves_runtime[index].ddr != NULL) && (valves_runtime[index].port != NULL))
        {
            GPIO_OUTPUT(*valves_runtime[index].ddr, valves_runtime[index].bit);
            GPIO_LOW(*valves_runtime[index].port, valves_runtime[index].bit);
        }

        valves_runtime[index].last_position = VALVE_POSITION_CLOSE;
        valves_runtime[index].last_current_adc = 0U;
        valves_runtime[index].peak_current_adc = 0U;
    }

    valves_reset_active_move();
    valves_reset_completed_move();
    PWR_6V5_SERVO_OFF();
}


/**************************************************************************************************
 * @brief Report whether a valve is physically installed.
 *
 * @param valve_id Valve identifier.
 *
 * @return True when installed.
 **************************************************************************************************/
bool valves_is_installed(valve_id_t valve_id)
{
    if (!valves_is_valid_id(valve_id))
    {
        return false;
    }

    return valves_runtime[valve_id].installed;
}


/**************************************************************************************************
 * @brief Report whether a valve move is currently active.
 *
 * @return True while the background move state machine owns the valve driver.
 **************************************************************************************************/
bool valves_is_busy()
{
    return (valves_active_move.busy != 0U);
}


/**************************************************************************************************
 * @brief Start a non-blocking valve move.
 *
 * Only one active move is allowed at a time. On success, Timer4 begins the settle/pulse/gap sequence
 * in the background and completion can later be read with valves_get_completed_move().
 *
 * @param valve_id Valve identifier.
 * @param position Target position.
 *
 * @return True when the move was accepted and started.
 **************************************************************************************************/
bool valves_start_move_to_position(valve_id_t valve_id, valve_position_t position)
{
    valve_runtime_t *valve;
    uint16_t pulse_us;
    uint16_t frame_us;
    uint16_t pulse_ticks;
    uint16_t frame_ticks;
    uint16_t sample_offset_ticks;
    uint32_t frame_count_32;
    uint32_t settle_ticks_32;
    bool accepted;

    if (!valves_is_valid_id(valve_id) || !valves_is_valid_position(position))
    {
        return false;
    }

    valve = &valves_runtime[valve_id];

    if (!valve->installed)
    {
        return false;
    }

    pulse_us = valves_get_position_pulse_us(&valve->config, position);
    frame_us = valve->config.frame_us;

    if (!valves_is_valid_pulse_us(pulse_us) || !valves_is_valid_frame_us(frame_us) ||
        !valves_is_valid_burst_ms(valve->config.burst_ms) ||
        !valves_is_valid_power_settle_ms(valve->config.power_settle_ms) ||
        (pulse_us >= frame_us))
    {
        return false;
    }

    frame_count_32 = (((uint32_t)valve->config.burst_ms * 1000UL) + (uint32_t)frame_us - 1UL) / (uint32_t)frame_us;
    if ((frame_count_32 == 0UL) || (frame_count_32 > 255UL))
    {
        return false;
    }

    settle_ticks_32 = (uint32_t)valve->config.power_settle_ms * VALVES_TIMER_TICKS_PER_MS;
    if (settle_ticks_32 > 65535UL)
    {
        return false;
    }

    pulse_ticks = (uint16_t)((uint32_t)pulse_us * VALVES_TIMER_TICKS_PER_US);
    frame_ticks = (uint16_t)((uint32_t)frame_us * VALVES_TIMER_TICKS_PER_US);
    sample_offset_ticks = (uint16_t)((uint32_t)VALVES_CURRENT_SAMPLE_OFFSET_US * VALVES_TIMER_TICKS_PER_US);
    accepted = false;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (valves_active_move.busy == 0U)
        {
            valves_reset_completed_move();

            valves_active_move.busy = 1U;
            valves_active_move.sample_pending = 0U;
            valves_active_move.sample_started = 0U;
            valves_active_move.state = VALVES_MOVE_STATE_SETTLE;
            valves_active_move.frame_index = 0U;
            valves_active_move.frame_count = (uint8_t)frame_count_32;
            valves_active_move.valve_id = (uint8_t)valve_id;
            valves_active_move.target_position = (uint8_t)position;
            valves_active_move.active_bit = valve->bit;
            valves_active_move.active_port = valve->port;
            valves_active_move.current_channel = valves_get_adc_channel_for_source(valve->current_sense);
            valves_active_move.current_monitoring_enabled = (uint8_t)(valve->config.current_monitoring_enabled ? 1U : 0U);
            valves_active_move.pulse_ticks = pulse_ticks;
            valves_active_move.frame_ticks = frame_ticks;
            valves_active_move.sample_offset_ticks = sample_offset_ticks;
            valves_active_move.sample_sum = 0UL;
            valves_active_move.stable_peak_adc = 0U;
            valves_active_move.stable_sample_count = 0U;
            accepted = true;
        }
    }

    if (!accepted)
    {
        return false;
    }

    if (!adc_acquire(ADC_OWNER_VALVES))
    {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            if ((valves_active_move.busy != 0U) && (valves_active_move.valve_id == (uint8_t)valve_id))
            {
                valves_reset_active_move();
            }
        }
        return false;
    }

    valve->last_current_adc = 0U;
    valve->peak_current_adc = 0U;

    PWR_6V5_SERVO_ON();
    valves_timer4_schedule_a_from_now((uint16_t)settle_ticks_32);

    return true;
}


/**************************************************************************************************
 * @brief Abort an active move.
 *
 * A latched completion result is stored with success = false.
 *
 * @return True when an active move was aborted.
 **************************************************************************************************/
bool valves_abort_move()
{
    bool aborted;
    valve_id_t valve_id;
    valve_position_t target_position;

    aborted = false;
    valve_id = VALVE_ID_NONE;
    target_position = VALVE_POSITION_CLOSE;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (valves_active_move.busy != 0U)
        {
            valve_id = (valve_id_t)valves_active_move.valve_id;
            target_position = (valve_position_t)valves_active_move.target_position;
            valves_set_output_low_unchecked();
            valves_reset_active_move();
            valves_store_completed_move(valve_id, target_position, false, 0UL, 0U, 0U);
            aborted = true;
        }
    }

    if (aborted)
    {
        adc_release(ADC_OWNER_VALVES);
        PWR_6V5_SERVO_OFF();
    }

    return aborted;
}


/**************************************************************************************************
 * @brief Report whether one completed move result is waiting.
 *
 * @return True when a completion result is latched.
 **************************************************************************************************/
bool valves_has_completed_move()
{
    return (valves_completed_move.pending != 0U);
}


/**************************************************************************************************
 * @brief Read and clear the latched completed move result.
 *
 * @param result Output pointer.
 *
 * @return True when one completion result was returned.
 **************************************************************************************************/
bool valves_get_completed_move(valve_move_result_t *result)
{
    bool got_result;
    uint32_t sample_sum;
    uint16_t sample_count;

    if (result == NULL)
    {
        return false;
    }

    got_result = false;
    sample_sum = 0UL;
    sample_count = 0U;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (valves_completed_move.pending != 0U)
        {
            result->valve_id = (valve_id_t)valves_completed_move.valve_id;
            result->target_position = (valve_position_t)valves_completed_move.target_position;
            result->success = (valves_completed_move.success != 0U);
            sample_sum = valves_completed_move.sample_sum;
            sample_count = valves_completed_move.sample_count;
            result->peak_current_adc = valves_completed_move.peak_current_adc;
            valves_reset_completed_move();
            got_result = true;
        }
    }

    if (!got_result)
    {
        return false;
    }

    if (sample_count > 0U)
    {
        result->average_current_adc = (uint16_t)(sample_sum / sample_count);
    }
    else
    {
        result->average_current_adc = 0U;
    }

    if (valves_is_valid_id(result->valve_id))
    {
        valves_runtime[result->valve_id].last_position = (uint8_t)result->target_position;
        valves_runtime[result->valve_id].last_current_adc = result->average_current_adc;
        valves_runtime[result->valve_id].peak_current_adc = result->peak_current_adc;
    }

    return true;
}


/**************************************************************************************************
 * @brief Return the currently active valve identifier.
 *
 * @return Snapshot of the current active valve, or VALVE_ID_NONE when idle.
 **************************************************************************************************/
valve_id_t valves_get_active_valve()
{
    return (valve_id_t)valves_active_move.valve_id;
}


/**************************************************************************************************
 * @brief Return the currently active target position.
 *
 * @return Snapshot of the current target position.
 **************************************************************************************************/
valve_position_t valves_get_active_target_position()
{
    return (valve_position_t)valves_active_move.target_position;
}


/**************************************************************************************************
 * @brief Return the last completed position remembered for one valve.
 *
 * @param valve_id Valve identifier.
 *
 * @return Last stored position, or VALVE_POSITION_CLOSE for invalid IDs.
 **************************************************************************************************/
valve_position_t valves_get_last_position(valve_id_t valve_id)
{
    if (!valves_is_valid_id(valve_id))
    {
        return VALVE_POSITION_CLOSE;
    }

    return (valve_position_t)valves_runtime[valve_id].last_position;
}


/**************************************************************************************************
 * @brief Return which current-sense source is associated with one valve.
 *
 * @param valve_id Valve identifier.
 *
 * @return Current-sense source enumeration.
 **************************************************************************************************/
valve_current_sense_t valves_get_current_sense_source(valve_id_t valve_id)
{
    if (!valves_is_valid_id(valve_id))
    {
        return VALVE_CURRENT_SENSE_NONE;
    }

    return (valve_current_sense_t)valves_runtime[valve_id].current_sense;
}


/**************************************************************************************************
 * @brief Override one named position pulse width for one valve.
 *
 * @param valve_id Valve identifier.
 * @param position Position to update.
 * @param pulse_us New pulse width.
 *
 * @return True on success.
 **************************************************************************************************/
bool valves_set_position_pulse_us(valve_id_t valve_id, valve_position_t position, uint16_t pulse_us)
{
    valve_config_t *config;

    if (!valves_is_valid_id(valve_id) || !valves_is_valid_position(position) ||
        !valves_is_valid_pulse_us(pulse_us) || valves_is_busy())
    {
        return false;
    }

    config = &valves_runtime[valve_id].config;

    switch (position)
    {
        case VALVE_POSITION_OPEN:
            config->open_us = pulse_us;
            break;

        case VALVE_POSITION_CLOSE:
            config->close_us = pulse_us;
            break;

        case VALVE_POSITION_CLOSE_HARD:
            config->close_hard_us = pulse_us;
            break;

        case VALVE_POSITION_SPARGE_OPEN:
            config->sparge_open_us = pulse_us;
            break;

        case VALVE_POSITION_SPARGE_CLOSE:
            config->sparge_close_us = pulse_us;
            break;

        default:
            return false;
    }

    return true;
}


/**************************************************************************************************
 * @brief Override frame period for one valve.
 *
 * @param valve_id Valve identifier.
 * @param frame_us Frame period in microseconds.
 *
 * @return True on success.
 **************************************************************************************************/
bool valves_set_frame_us(valve_id_t valve_id, uint16_t frame_us)
{
    if (!valves_is_valid_id(valve_id) || !valves_is_valid_frame_us(frame_us) || valves_is_busy())
    {
        return false;
    }

    valves_runtime[valve_id].config.frame_us = frame_us;
    return true;
}


/**************************************************************************************************
 * @brief Override burst length for one valve.
 *
 * @param valve_id Valve identifier.
 * @param burst_ms Burst length in milliseconds.
 *
 * @return True on success.
 **************************************************************************************************/
bool valves_set_burst_ms(valve_id_t valve_id, uint16_t burst_ms)
{
    if (!valves_is_valid_id(valve_id) || !valves_is_valid_burst_ms(burst_ms) || valves_is_busy())
    {
        return false;
    }

    valves_runtime[valve_id].config.burst_ms = burst_ms;
    return true;
}


/**************************************************************************************************
 * @brief Override servo rail settle delay before movement.
 *
 * @param valve_id Valve identifier.
 * @param power_settle_ms Delay in milliseconds.
 *
 * @return True on success.
 **************************************************************************************************/
bool valves_set_power_settle_ms(valve_id_t valve_id, uint16_t power_settle_ms)
{
    if (!valves_is_valid_id(valve_id) || !valves_is_valid_power_settle_ms(power_settle_ms) || valves_is_busy())
    {
        return false;
    }

    valves_runtime[valve_id].config.power_settle_ms = power_settle_ms;
    return true;
}


/**************************************************************************************************
 * @brief Override how many startup frames are ignored by current averaging.
 *
 * @param valve_id Valve identifier.
 * @param frame_count Number of initial frames to ignore.
 *
 * @return True on success.
 **************************************************************************************************/
bool valves_set_startup_frames_to_ignore(valve_id_t valve_id, uint8_t frame_count)
{
    if (!valves_is_valid_id(valve_id) || valves_is_busy())
    {
        return false;
    }

    valves_runtime[valve_id].config.startup_frames_to_ignore = frame_count;
    return true;
}


/**************************************************************************************************
 * @brief Enable or disable current monitoring for one valve.
 *
 * @param valve_id Valve identifier.
 * @param enabled New monitoring state.
 *
 * @return True on success.
 **************************************************************************************************/
bool valves_set_current_monitoring_enabled(valve_id_t valve_id, bool enabled)
{
    if (!valves_is_valid_id(valve_id) || valves_is_busy())
    {
        return false;
    }

    valves_runtime[valve_id].config.current_monitoring_enabled = enabled;
    return true;
}


/**************************************************************************************************
 * @brief Return the live configuration structure for one valve.
 *
 * @param valve_id Valve identifier.
 *
 * @return Pointer to config, or NULL for invalid IDs.
 **************************************************************************************************/
const valve_config_t *valves_get_config(valve_id_t valve_id)
{
    if (!valves_is_valid_id(valve_id))
    {
        return NULL;
    }

    return &valves_runtime[valve_id].config;
}


/**************************************************************************************************
 * @brief Return the last averaged current ADC value stored for one valve.
 *
 * @param valve_id Valve identifier.
 *
 * @return Last averaged ADC value.
 **************************************************************************************************/
uint16_t valves_get_last_current_adc(valve_id_t valve_id)
{
    if (!valves_is_valid_id(valve_id))
    {
        return 0U;
    }

    return valves_runtime[valve_id].last_current_adc;
}


/**************************************************************************************************
 * @brief Return the last peak current ADC value stored for one valve.
 *
 * @param valve_id Valve identifier.
 *
 * @return Last peak ADC value.
 **************************************************************************************************/
uint16_t valves_get_peak_current_adc(valve_id_t valve_id)
{
    if (!valves_is_valid_id(valve_id))
    {
        return 0U;
    }

    return valves_runtime[valve_id].peak_current_adc;
}


/**************************************************************************************************
 * @brief Perform a direct foreground ADC read for one valve's current sense.
 *
 * This helper is disabled while an active background move owns the ADC.
 *
 * @param valve_id Valve identifier.
 *
 * @return ADC value, or 0 when unavailable.
 **************************************************************************************************/
uint16_t valves_read_current_adc(valve_id_t valve_id)
{
    uint8_t channel;

    if (!valves_is_valid_id(valve_id) || valves_is_busy())
    {
        return 0U;
    }

    channel = valves_get_adc_channel_for_source(valves_runtime[valve_id].current_sense);
    if (channel == VALVES_ADC_INVALID_CHANNEL)
    {
        return 0U;
    }

    if (!adc_acquire(ADC_OWNER_DEBUG))
    {
        return 0U;
    }

    if (!adc_read_blocking(ADC_OWNER_DEBUG, channel, &valves_runtime[valve_id].last_current_adc))
    {
        adc_release(ADC_OWNER_DEBUG);
        return 0U;
    }

    adc_release(ADC_OWNER_DEBUG);
    return valves_runtime[valve_id].last_current_adc;
}


// ======================================================================================
//  INTERNAL HELPERS
// ======================================================================================

/**************************************************************************************************
 * @brief Check whether a valve identifier is in range.
 **************************************************************************************************/
static inline bool valves_is_valid_id(valve_id_t valve_id)
{
    return (valve_id > VALVE_ID_NONE) && (valve_id < VALVE_ID_COUNT);
}


/**************************************************************************************************
 * @brief Check whether a valve position enumeration is in range.
 **************************************************************************************************/
static inline bool valves_is_valid_position(valve_position_t position)
{
    return (position < VALVE_POSITION_COUNT);
}


/**************************************************************************************************
 * @brief Check whether a pulse width is within the supported servo range.
 **************************************************************************************************/
static bool valves_is_valid_pulse_us(uint16_t pulse_us)
{
    return (pulse_us >= VALVES_MIN_PULSE_US) && (pulse_us <= VALVES_MAX_PULSE_US);
}


/**************************************************************************************************
 * @brief Check whether a frame period is acceptable for Timer4 scheduling.
 **************************************************************************************************/
static bool valves_is_valid_frame_us(uint16_t frame_us)
{
    return (frame_us >= VALVES_MIN_FRAME_US) &&
           (frame_us <= VALVES_MAX_FRAME_US) &&
           (((uint32_t)frame_us * VALVES_TIMER_TICKS_PER_US) <= 65535UL);
}


/**************************************************************************************************
 * @brief Check whether a burst length is within the supported range.
 **************************************************************************************************/
static bool valves_is_valid_burst_ms(uint16_t burst_ms)
{
    return (burst_ms >= VALVES_MIN_BURST_MS) && (burst_ms <= VALVES_MAX_BURST_MS);
}


/**************************************************************************************************
 * @brief Check whether a servo power settle delay is schedulable on Timer4.
 **************************************************************************************************/
static bool valves_is_valid_power_settle_ms(uint16_t power_settle_ms)
{
    return (power_settle_ms <= VALVES_MAX_POWER_SETTLE_MS);
}


/**************************************************************************************************
 * @brief Return the pulse width corresponding to one named valve position.
 **************************************************************************************************/
static uint16_t valves_get_position_pulse_us(const valve_config_t *config, valve_position_t position)
{
    switch (position)
    {
        case VALVE_POSITION_OPEN:
            return config->open_us;

        case VALVE_POSITION_CLOSE:
            return config->close_us;

        case VALVE_POSITION_CLOSE_HARD:
            return config->close_hard_us;

        case VALVE_POSITION_SPARGE_OPEN:
            return config->sparge_open_us;

        case VALVE_POSITION_SPARGE_CLOSE:
            return config->sparge_close_us;

        default:
            return config->close_us;
    }
}


/**************************************************************************************************
 * @brief Map a current-sense source to the corresponding ADC channel.
 **************************************************************************************************/
static uint8_t valves_get_adc_channel_for_source(uint8_t source)
{
    switch (source)
    {
        case VALVE_CURRENT_SENSE_VALVES:
            return VALVE_CURRENT_SENSE_ADC_CHANNEL;

        case VALVE_CURRENT_SENSE_BOIL_PUMP:
            return BOIL_PUMP_CURRENT_SENSE_ADC_CHANNEL;

        case VALVE_CURRENT_SENSE_MASH_PUMP:
            return MASH_PUMP_CURRENT_SENSE_ADC_CHANNEL;

        default:
            return VALVES_ADC_INVALID_CHANNEL;
    }
}


/**************************************************************************************************
 * @brief Drive one valve output low without re-validating the valve ID.
 **************************************************************************************************/
static inline void valves_set_output_low_unchecked()
{
    *valves_active_move.active_port &= (uint8_t)~(1U << valves_active_move.active_bit);
}


/**************************************************************************************************
 * @brief Drive one valve output high without re-validating the valve ID.
 **************************************************************************************************/
static inline void valves_set_output_high_unchecked()
{
    *valves_active_move.active_port |= (uint8_t)(1U << valves_active_move.active_bit);
}


/**************************************************************************************************
 * @brief Finish the current move and latch the result.
 **************************************************************************************************/
static void valves_finish_move()
{
    uint16_t peak_current_adc;

    peak_current_adc = 0U;

    if (valves_active_move.current_monitoring_enabled != 0U)
    {
        if (valves_active_move.stable_sample_count > 0U)
        {
            peak_current_adc = valves_active_move.stable_peak_adc;
        }
    }

    valves_store_completed_move((valve_id_t)valves_active_move.valve_id,
                                (valve_position_t)valves_active_move.target_position,
                                true,
                                valves_active_move.sample_sum,
                                valves_active_move.stable_sample_count,
                                peak_current_adc);

    *valves_active_move.active_port &= (uint8_t)~(1U << valves_active_move.active_bit);

    valves_reset_active_move();
    adc_release(ADC_OWNER_VALVES);
    PWR_6V5_SERVO_OFF();
}


/**************************************************************************************************
 * @brief Reset the active move state and disable Timer4 interrupts used by the valve driver.
 *
 * ADC ownership is released by the higher-level finish or abort path after teardown completes.
 **************************************************************************************************/
static void valves_reset_active_move()
{
    TIMSK4 &= (uint8_t)~((1U << OCIE4A) | (1U << OCIE4B));
    TIFR4 = (1U << OCF4A) | (1U << OCF4B);

    valves_active_move.busy = 0U;
    valves_active_move.sample_pending = 0U;
    valves_active_move.sample_started = 0U;
    valves_active_move.state = VALVES_MOVE_STATE_IDLE;
    valves_active_move.frame_index = 0U;
    valves_active_move.frame_count = 0U;
    valves_active_move.valve_id = VALVE_ID_NONE;
    valves_active_move.target_position = VALVE_POSITION_CLOSE;
    valves_active_move.active_bit = 0U;
    valves_active_move.current_channel = VALVES_ADC_INVALID_CHANNEL;
    valves_active_move.current_monitoring_enabled = 0U;
    valves_active_move.active_port = NULL;
    valves_active_move.pulse_ticks = 0U;
    valves_active_move.frame_ticks = 0U;
    valves_active_move.sample_offset_ticks = 0U;
    valves_active_move.sample_sum = 0UL;
    valves_active_move.stable_peak_adc = 0U;
    valves_active_move.stable_sample_count = 0U;
}


/**************************************************************************************************
 * @brief Clear the latched completed move result flag.
 **************************************************************************************************/
static void valves_reset_completed_move()
{
    valves_completed_move.pending = 0U;
}


/**************************************************************************************************
 * @brief Latch one completed move result.
 **************************************************************************************************/
static void valves_store_completed_move(valve_id_t valve_id,
                                        valve_position_t target_position,
                                        bool success,
                                        uint32_t sample_sum,
                                        uint16_t sample_count,
                                        uint16_t peak_current_adc)
{
    valves_completed_move.valve_id = (uint8_t)valve_id;
    valves_completed_move.target_position = (uint8_t)target_position;
    valves_completed_move.success = success ? 1U : 0U;
    valves_completed_move.sample_sum = sample_sum;
    valves_completed_move.sample_count = sample_count;
    valves_completed_move.peak_current_adc = peak_current_adc;
    valves_completed_move.pending = 1U;
}


/**************************************************************************************************
 * @brief ADC conversion callback used during active valve moves.
 */
static void valves_adc_callback(uint16_t sample, void *context)
{
    (void)context;

    if (valves_active_move.sample_started == 0U)
    {
        return;
    }

    valves_active_move.sample_started = 0U;
    valves_active_move.sample_pending = 0U;
    valves_active_move.sample_sum += sample;
    valves_active_move.stable_sample_count++;

    if (sample > valves_active_move.stable_peak_adc)
    {
        valves_active_move.stable_peak_adc = sample;
    }
}


// ======================================================================================
//  TIMER / ISR
// ======================================================================================

/**************************************************************************************************
 * @brief Initialize Timer4 as a free-running timing base.
 **************************************************************************************************/
static void valves_timer4_init()
{
    TCCR4A = 0U;
    TCCR4B = 0U;
    TCCR4C = 0U;
    TCNT4 = 0U;
    TIFR4 = (1U << OCF4A) | (1U << OCF4B) | (1U << OCF4C) | (1U << TOV4);
    TIMSK4 = 0U;
    TCCR4B = VALVES_TIMER_PRESCALER_BITS;
}


/**************************************************************************************************
 * @brief Schedule the next Timer4 compare-A event relative to the current counter value.
 **************************************************************************************************/
static void valves_timer4_schedule_a_from_now(uint16_t ticks_from_now)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        valves_timer4_schedule_a_from_now_isr(ticks_from_now);
    }
}


/**************************************************************************************************
 * @brief Schedule the next Timer4 compare-A event relative to the current counter value from ISR context.
 **************************************************************************************************/
static void valves_timer4_schedule_a_from_now_isr(uint16_t ticks_from_now)
{
    uint16_t now;
    uint16_t compare;

    if (ticks_from_now == 0U)
    {
        ticks_from_now = 1U;
    }

    now = TCNT4;
    compare = (uint16_t)(now + ticks_from_now);
    OCR4A = compare;
    TIFR4 = (1U << OCF4A);
    TIMSK4 |= (1U << OCIE4A);
}


/**************************************************************************************************
 * @brief Schedule the next Timer4 compare-B event relative to the current counter value from ISR context.
 **************************************************************************************************/
static void valves_timer4_schedule_b_from_now_isr(uint16_t ticks_from_now)
{
    uint16_t now;
    uint16_t compare;

    if (ticks_from_now == 0U)
    {
        ticks_from_now = 1U;
    }

    now = TCNT4;
    compare = (uint16_t)(now + ticks_from_now);
    OCR4B = compare;
    TIFR4 = (1U << OCF4B);
    TIMSK4 |= (1U << OCIE4B);
}


/**************************************************************************************************
 * @brief Timer4 compare-A ISR.
 *
 * This ISR advances the move state machine through settle, pulse-high, and frame-gap phases.
 **************************************************************************************************/
ISR(TIMER4_COMPA_vect)
{
    valve_runtime_t *valve;

    switch (valves_active_move.state)
    {
        case VALVES_MOVE_STATE_SETTLE:
        case VALVES_MOVE_STATE_FRAME_GAP:
            if (valves_active_move.frame_index >= valves_active_move.frame_count)
            {
                valves_finish_move();
            }
            else
            {
                valve = &valves_runtime[valves_active_move.valve_id];

                valves_set_output_high_unchecked();
                valves_active_move.state = VALVES_MOVE_STATE_PULSE_HIGH;
                valves_active_move.sample_pending = 0U;
                valves_active_move.sample_started = 0U;

                if (valve->config.current_monitoring_enabled &&
                    (valves_active_move.frame_index >= valve->config.startup_frames_to_ignore) &&
                    (valves_active_move.pulse_ticks >= (uint16_t)(VALVES_CURRENT_SAMPLE_MIN_PULSE_US * VALVES_TIMER_TICKS_PER_US)))
                {
                    valves_active_move.sample_pending = 1U;
                    valves_timer4_schedule_b_from_now_isr(valves_active_move.sample_offset_ticks);
                }
                else
                {
                    TIMSK4 &= (uint8_t)~(1U << OCIE4B);
                }

                valves_timer4_schedule_a_from_now_isr(valves_active_move.pulse_ticks);
            }
            break;

        case VALVES_MOVE_STATE_PULSE_HIGH:
            valves_set_output_low_unchecked();
            valves_active_move.frame_index++;
            valves_active_move.state = VALVES_MOVE_STATE_FRAME_GAP;
            TIMSK4 &= (uint8_t)~(1U << OCIE4B);
            valves_timer4_schedule_a_from_now_isr((uint16_t)(valves_active_move.frame_ticks - valves_active_move.pulse_ticks));
            break;

        default:
            valves_reset_active_move();
            PWR_6V5_SERVO_OFF();
            break;
    }
}


/**************************************************************************************************
 * @brief Timer4 compare-B ISR.
 *
 * This ISR triggers one ADC conversion during the high portion of a servo pulse.
 **************************************************************************************************/
ISR(TIMER4_COMPB_vect)
{
    uint8_t channel;

    TIMSK4 &= (uint8_t)~(1U << OCIE4B);

    if ((valves_active_move.busy == 0U) ||
        (valves_active_move.state != VALVES_MOVE_STATE_PULSE_HIGH) ||
        (valves_active_move.sample_pending == 0U) ||
        (valves_active_move.sample_started != 0U))
    {
        return;
    }

    channel = valves_active_move.current_channel;
    if (channel == VALVES_ADC_INVALID_CHANNEL)
    {
        valves_active_move.sample_pending = 0U;
        return;
    }

    valves_active_move.sample_started = 1U;
    if (!adc_start_conversion(ADC_OWNER_VALVES, channel, valves_adc_callback, NULL))
    {
        valves_active_move.sample_started = 0U;
        valves_active_move.sample_pending = 0U;
    }
}
