#include "Pumps.h"

#include "ADC.h"
#include "Board.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <util/atomic.h>


// ======================================================================================
//  CONSTANTS
// ======================================================================================

#define PUMPS_TIMER3_COMPARE_A_TICKS             24999U
#define PUMPS_TIMER3_PRESCALER_BITS              ((1U << CS31) | (1U << CS30))

#define PUMPS_DIAG_SAMPLE_COUNT                  16U
#define PUMPS_DIAG_STARTUP_GRACE_WINDOWS         4U
#define PUMPS_DIAG_MAYBE_THRESHOLD               3U
#define PUMPS_MAX_SPEED                          220U

#define PUMPS_HEALTHY_TACH_MIN                   4U
#define PUMPS_WEAK_TACH_MAX                      2U
#define PUMPS_CURRENT_PRESENT_AVG_MIN            30U
#define PUMPS_CURRENT_PRESENT_PEAK_MIN           45U
#define PUMPS_CURRENT_WEAK_AVG_MAX               24U
#define PUMPS_CURRENT_WEAK_PEAK_MAX              36U

#define PUMPS_DAC_CHANNEL_A_BIT                  0x0000U
#define PUMPS_DAC_CHANNEL_B_BIT                  0x8000U
#define PUMPS_DAC_ACTIVE_BIT                     0x1000U


// ======================================================================================
//  TYPES
// ======================================================================================

typedef struct
{
    volatile uint8_t *enable_port;
    volatile uint8_t *enable_ddr;
    uint8_t enable_bit;
    volatile uint8_t *tach_pinreg;
    uint8_t tach_bit;
    uint8_t adc_channel;
    uint16_t dac_channel_bit;
} pump_hw_t;

typedef struct
{
    uint8_t commanded_speed;
    uint8_t running;
    uint8_t startup_windows_remaining;
    uint8_t dry_counter;
    uint8_t clogged_counter;
    uint8_t current_valid;
    uint8_t state;
    volatile uint16_t tach_total_pulses;
    volatile uint16_t tach_window_pulses;
    uint16_t last_tach_window_pulses;
    uint16_t current_avg_adc;
    uint16_t current_peak_adc;
} pump_runtime_t;


// ======================================================================================
//  VARIABLES
// ======================================================================================

static const pump_hw_t pumps_hw[PUMP_ID_COUNT] =
{
    [PUMP_ID_NONE] =
    {
        .enable_port = NULL,
        .enable_ddr = NULL,
        .enable_bit = 0U,
        .tach_pinreg = NULL,
        .tach_bit = 0U,
        .adc_channel = 0U,
        .dac_channel_bit = PUMPS_DAC_CHANNEL_A_BIT
    },
    [PUMP_ID_MASH] =
    {
        .enable_port = &MASH_PUMP_EN_PORT,
        .enable_ddr = &MASH_PUMP_EN_DDR,
        .enable_bit = MASH_PUMP_EN_BIT,
        .tach_pinreg = &MASH_PUMP_TACH_PINREG,
        .tach_bit = MASH_PUMP_TACH_BIT,
        .adc_channel = MASH_PUMP_CURRENT_SENSE_ADC_CHANNEL,
        .dac_channel_bit = PUMPS_DAC_CHANNEL_B_BIT
    },
    [PUMP_ID_BOIL] =
    {
        .enable_port = &BOIL_PUMP_EN_PORT,
        .enable_ddr = &BOIL_PUMP_EN_DDR,
        .enable_bit = BOIL_PUMP_EN_BIT,
        .tach_pinreg = &BOIL_PUMP_TACH_PINREG,
        .tach_bit = BOIL_PUMP_TACH_BIT,
        .adc_channel = BOIL_PUMP_CURRENT_SENSE_ADC_CHANNEL,
        .dac_channel_bit = PUMPS_DAC_CHANNEL_A_BIT
    }
};

static volatile pump_runtime_t pumps_runtime[PUMP_ID_COUNT] =
{
    [PUMP_ID_NONE] = { 0 },
    [PUMP_ID_MASH] = { 0 },
    [PUMP_ID_BOIL] = { 0 }
};

static volatile uint8_t pumps_diag_tick_pending = 0U;


// ======================================================================================
//  PROTOTYPES
// ======================================================================================

static bool pumps_is_valid_id(pump_id_t pump_id);
static void pumps_write_dac_raw(uint16_t value);
static void pumps_write_dac_speed(pump_id_t pump_id, uint8_t speed);
static void pumps_latch_dac();
static void pumps_sample_current(pump_id_t pump_id);
static void pumps_update_one_diag(pump_id_t pump_id);


// ======================================================================================
//  API
// ======================================================================================

/**************************************************************************************************
 * @brief Initialize pump GPIO, SPI DAC output, tach interrupts, and Timer3 diagnostics tick.
 **************************************************************************************************/
void pumps_init()
{
    uint8_t pump_id;

    GPIO_OUTPUT(SPI_MASTER_SS_DDR, SPI_MASTER_SS_BIT);
    GPIO_HIGH(SPI_MASTER_SS_PORT, SPI_MASTER_SS_BIT);

    GPIO_OUTPUT(PUMP_DAC_CS_DDR, PUMP_DAC_CS_BIT);
    GPIO_OUTPUT(PUMP_DAC_SCK_DDR, PUMP_DAC_SCK_BIT);
    GPIO_OUTPUT(PUMP_DAC_MOSI_DDR, PUMP_DAC_MOSI_BIT);
    GPIO_INPUT(PUMP_DAC_MISO_DDR, PUMP_DAC_MISO_BIT);
    GPIO_OUTPUT(PUMP_DAC_LDAC_DDR, PUMP_DAC_LDAC_BIT);

    GPIO_HIGH(PUMP_DAC_CS_PORT, PUMP_DAC_CS_BIT);
    GPIO_HIGH(PUMP_DAC_LDAC_PORT, PUMP_DAC_LDAC_BIT);

    SPCR = (1U << SPE) | (1U << MSTR) | (1U << SPR0);
    SPSR = 0U;

    for (pump_id = (uint8_t)PUMP_ID_MASH; pump_id < (uint8_t)PUMP_ID_COUNT; pump_id++)
    {
        GPIO_OUTPUT(*pumps_hw[pump_id].enable_ddr, pumps_hw[pump_id].enable_bit);
        GPIO_LOW(*pumps_hw[pump_id].enable_port, pumps_hw[pump_id].enable_bit);

        pumps_runtime[pump_id].commanded_speed = 0U;
        pumps_runtime[pump_id].running = 0U;
        pumps_runtime[pump_id].startup_windows_remaining = 0U;
        pumps_runtime[pump_id].dry_counter = 0U;
        pumps_runtime[pump_id].clogged_counter = 0U;
        pumps_runtime[pump_id].current_valid = 0U;
        pumps_runtime[pump_id].state = PUMP_DIAG_STATE_IDLE;
        pumps_runtime[pump_id].tach_total_pulses = 0U;
        pumps_runtime[pump_id].tach_window_pulses = 0U;
        pumps_runtime[pump_id].last_tach_window_pulses = 0U;
        pumps_runtime[pump_id].current_avg_adc = 0U;
        pumps_runtime[pump_id].current_peak_adc = 0U;
    }

    GPIO_INPUT(MASH_PUMP_TACH_DDR, MASH_PUMP_TACH_BIT);
    GPIO_INPUT(BOIL_PUMP_TACH_DDR, BOIL_PUMP_TACH_BIT);
    GPIO_PULLUP_ON(MASH_PUMP_TACH_PORT, MASH_PUMP_TACH_BIT);
    GPIO_PULLUP_ON(BOIL_PUMP_TACH_PORT, BOIL_PUMP_TACH_BIT);

    EICRB = (uint8_t)((EICRB & (uint8_t)~((1U << ISC41) | (1U << ISC40) | (1U << ISC51) | (1U << ISC50))) |
                      (1U << ISC41) |
                      (1U << ISC51));
    EIMSK |= (1U << INT4) | (1U << INT5);

    TCCR3A = 0U;
    TCCR3B = 0U;
    TCNT3 = 0U;
    OCR3A = PUMPS_TIMER3_COMPARE_A_TICKS;
    TIFR3 = (1U << OCF3A);
    TIMSK3 = (1U << OCIE3A);
    TCCR3B = (1U << WGM32) | PUMPS_TIMER3_PRESCALER_BITS;

    pumps_write_dac_speed(PUMP_ID_MASH, 0U);
    pumps_write_dac_speed(PUMP_ID_BOIL, 0U);
    pumps_latch_dac();
}


/**************************************************************************************************
 * @brief Run pump background work.
 *
 * Keep calling this from the main loop. The ISR only raises a pending flag; the heavier ADC burst
 * sampling and diagnostics classification happen here.
 **************************************************************************************************/
void pumps_update()
{
    uint8_t pending;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        pending = pumps_diag_tick_pending;
        if (pumps_diag_tick_pending > 0U)
        {
            pumps_diag_tick_pending = 0U;
        }
    }

    while (pending > 0U)
    {
        pumps_update_one_diag(PUMP_ID_MASH);
        pumps_update_one_diag(PUMP_ID_BOIL);
        pending--;
    }
}


/**************************************************************************************************
 * @brief Set one pump speed command.
 **************************************************************************************************/
bool pumps_set_speed(pump_id_t pump_id, uint8_t speed)
{
    if (!pumps_is_valid_id(pump_id))
    {
        return false;
    }

    if (speed > PUMPS_MAX_SPEED)
    {
        speed = PUMPS_MAX_SPEED;
    }

    pumps_runtime[pump_id].commanded_speed = speed;
    pumps_write_dac_speed(pump_id, speed);
    pumps_latch_dac();

    if (speed == 0U)
    {
        (void)pumps_stop(pump_id);
    }

    return true;
}


/**************************************************************************************************
 * @brief Get the current commanded speed.
 **************************************************************************************************/
uint8_t pumps_get_speed(pump_id_t pump_id)
{
    if (!pumps_is_valid_id(pump_id))
    {
        return 0U;
    }

    return pumps_runtime[pump_id].commanded_speed;
}


/**************************************************************************************************
 * @brief Start one pump.
 **************************************************************************************************/
bool pumps_start(pump_id_t pump_id)
{
    if (!pumps_is_valid_id(pump_id))
    {
        return false;
    }

    if (pumps_runtime[pump_id].commanded_speed == 0U)
    {
        return false;
    }

    pumps_runtime[pump_id].running = 1U;
    pumps_runtime[pump_id].startup_windows_remaining = PUMPS_DIAG_STARTUP_GRACE_WINDOWS;
    pumps_runtime[pump_id].dry_counter = 0U;
    pumps_runtime[pump_id].clogged_counter = 0U;
    pumps_runtime[pump_id].current_valid = 0U;
    pumps_runtime[pump_id].state = PUMP_DIAG_STATE_STARTING;
    pumps_runtime[pump_id].last_tach_window_pulses = 0U;
    pumps_runtime[pump_id].current_avg_adc = 0U;
    pumps_runtime[pump_id].current_peak_adc = 0U;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        pumps_runtime[pump_id].tach_window_pulses = 0U;
    }

    pumps_write_dac_speed(pump_id, pumps_runtime[pump_id].commanded_speed);
    pumps_latch_dac();
    GPIO_HIGH(*pumps_hw[pump_id].enable_port, pumps_hw[pump_id].enable_bit);
    return true;
}


/**************************************************************************************************
 * @brief Stop one pump.
 **************************************************************************************************/
bool pumps_stop(pump_id_t pump_id)
{
    if (!pumps_is_valid_id(pump_id))
    {
        return false;
    }

    GPIO_LOW(*pumps_hw[pump_id].enable_port, pumps_hw[pump_id].enable_bit);
    pumps_write_dac_speed(pump_id, 0U);
    pumps_latch_dac();

    pumps_runtime[pump_id].running = 0U;
    pumps_runtime[pump_id].startup_windows_remaining = 0U;
    pumps_runtime[pump_id].dry_counter = 0U;
    pumps_runtime[pump_id].clogged_counter = 0U;
    pumps_runtime[pump_id].current_valid = 0U;
    pumps_runtime[pump_id].state = PUMP_DIAG_STATE_IDLE;
    pumps_runtime[pump_id].last_tach_window_pulses = 0U;
    pumps_runtime[pump_id].current_avg_adc = 0U;
    pumps_runtime[pump_id].current_peak_adc = 0U;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        pumps_runtime[pump_id].tach_window_pulses = 0U;
    }

    return true;
}


/**************************************************************************************************
 * @brief Stop both pumps.
 **************************************************************************************************/
void pumps_all_off()
{
    (void)pumps_stop(PUMP_ID_MASH);
    (void)pumps_stop(PUMP_ID_BOIL);
}


/**************************************************************************************************
 * @brief Report whether one pump is running.
 **************************************************************************************************/
bool pumps_is_running(pump_id_t pump_id)
{
    if (!pumps_is_valid_id(pump_id))
    {
        return false;
    }

    return (pumps_runtime[pump_id].running != 0U);
}


/**************************************************************************************************
 * @brief Get total tach pulses.
 **************************************************************************************************/
uint16_t pumps_get_tach_pulses(pump_id_t pump_id)
{
    uint16_t pulses = 0U;

    if (!pumps_is_valid_id(pump_id))
    {
        return 0U;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        pulses = pumps_runtime[pump_id].tach_total_pulses;
    }

    return pulses;
}


/**************************************************************************************************
 * @brief Reset total tach pulses.
 **************************************************************************************************/
void pumps_reset_tach_pulses(pump_id_t pump_id)
{
    if (!pumps_is_valid_id(pump_id))
    {
        return;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        pumps_runtime[pump_id].tach_total_pulses = 0U;
        pumps_runtime[pump_id].tach_window_pulses = 0U;
    }

    pumps_runtime[pump_id].last_tach_window_pulses = 0U;
}


/**************************************************************************************************
 * @brief Read the instantaneous tach input level.
 **************************************************************************************************/
bool pumps_get_tach_level(pump_id_t pump_id)
{
    if (!pumps_is_valid_id(pump_id))
    {
        return false;
    }

    return GPIO_READ(*pumps_hw[pump_id].tach_pinreg, pumps_hw[pump_id].tach_bit);
}


/**************************************************************************************************
 * @brief Get the latest current average ADC value.
 **************************************************************************************************/
uint16_t pumps_get_current_avg_adc(pump_id_t pump_id)
{
    if (!pumps_is_valid_id(pump_id))
    {
        return 0U;
    }

    return pumps_runtime[pump_id].current_avg_adc;
}


/**************************************************************************************************
 * @brief Get the latest current peak ADC value.
 **************************************************************************************************/
uint16_t pumps_get_current_peak_adc(pump_id_t pump_id)
{
    if (!pumps_is_valid_id(pump_id))
    {
        return 0U;
    }

    return pumps_runtime[pump_id].current_peak_adc;
}


/**************************************************************************************************
 * @brief Get diagnostics state.
 **************************************************************************************************/
pump_diag_state_t pumps_get_diag_state(pump_id_t pump_id)
{
    if (!pumps_is_valid_id(pump_id))
    {
        return PUMP_DIAG_STATE_IDLE;
    }

    return (pump_diag_state_t)pumps_runtime[pump_id].state;
}


/**************************************************************************************************
 * @brief Report whether the diagnostics currently suggest maybe-dry.
 **************************************************************************************************/
bool pumps_is_maybe_dry(pump_id_t pump_id)
{
    return (pumps_get_diag_state(pump_id) == PUMP_DIAG_STATE_MAYBE_DRY);
}


/**************************************************************************************************
 * @brief Hard dry promotion is intentionally disabled for now.
 **************************************************************************************************/
bool pumps_is_dry(pump_id_t pump_id)
{
    (void)pump_id;
    return false;
}


/**************************************************************************************************
 * @brief Report whether the diagnostics currently suggest maybe-clogged.
 **************************************************************************************************/
bool pumps_is_maybe_clogged(pump_id_t pump_id)
{
    return (pumps_get_diag_state(pump_id) == PUMP_DIAG_STATE_MAYBE_CLOGGED);
}


/**************************************************************************************************
 * @brief Hard clogged promotion is intentionally disabled for now.
 **************************************************************************************************/
bool pumps_is_clogged(pump_id_t pump_id)
{
    (void)pump_id;
    return false;
}


/**************************************************************************************************
 * @brief Copy one pump status snapshot.
 **************************************************************************************************/
bool pumps_get_status(pump_id_t pump_id, pump_status_t *status)
{
    if (!pumps_is_valid_id(pump_id) || (status == NULL))
    {
        return false;
    }

    status->commanded_speed = pumps_runtime[pump_id].commanded_speed;
    status->running = (pumps_runtime[pump_id].running != 0U);
    status->current_valid = (pumps_runtime[pump_id].current_valid != 0U);
    status->current_avg_adc = pumps_runtime[pump_id].current_avg_adc;
    status->current_peak_adc = pumps_runtime[pump_id].current_peak_adc;
    status->dry_counter = pumps_runtime[pump_id].dry_counter;
    status->clogged_counter = pumps_runtime[pump_id].clogged_counter;
    status->state = (pump_diag_state_t)pumps_runtime[pump_id].state;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        status->tach_total_pulses = pumps_runtime[pump_id].tach_total_pulses;
        status->tach_window_pulses = pumps_runtime[pump_id].last_tach_window_pulses;
    }

    return true;
}


// ======================================================================================
//  INTERNALS
// ======================================================================================

/**************************************************************************************************
 * @brief Sample one pump current channel as a short burst.
 **************************************************************************************************/
static void pumps_sample_current(pump_id_t pump_id)
{
    uint8_t index;
    uint16_t sample;
    uint16_t peak;
    uint32_t sum;

    if (!pumps_runtime[pump_id].running)
    {
        pumps_runtime[pump_id].current_valid = 0U;
        pumps_runtime[pump_id].current_avg_adc = 0U;
        pumps_runtime[pump_id].current_peak_adc = 0U;
        return;
    }

    if (!adc_acquire(ADC_OWNER_PUMPS))
    {
        pumps_runtime[pump_id].current_valid = 0U;
        return;
    }

    sum = 0UL;
    peak = 0U;

    for (index = 0U; index < PUMPS_DIAG_SAMPLE_COUNT; index++)
    {
        if (!adc_read_blocking(ADC_OWNER_PUMPS, pumps_hw[pump_id].adc_channel, &sample))
        {
            adc_release(ADC_OWNER_PUMPS);
            pumps_runtime[pump_id].current_valid = 0U;
            return;
        }

        sum += sample;
        if (sample > peak)
        {
            peak = sample;
        }
    }

    adc_release(ADC_OWNER_PUMPS);

    pumps_runtime[pump_id].current_valid = 1U;
    pumps_runtime[pump_id].current_avg_adc = (uint16_t)(sum / PUMPS_DIAG_SAMPLE_COUNT);
    pumps_runtime[pump_id].current_peak_adc = peak;
}


/**************************************************************************************************
 * @brief Update diagnostics for one pump.
 *
 * This intentionally stays conservative. Strong tach always wins over flaky current, and hard fault
 * promotion is disabled until the fully assembled machine can be tested in normal operation.
 **************************************************************************************************/
static void pumps_update_one_diag(pump_id_t pump_id)
{
    uint16_t tach_window;
    bool tach_healthy;
    bool tach_weak;
    bool current_present;
    bool current_weak;

    if (!pumps_is_valid_id(pump_id))
    {
        return;
    }

    if (!pumps_runtime[pump_id].running)
    {
        pumps_runtime[pump_id].state = PUMP_DIAG_STATE_IDLE;
        pumps_runtime[pump_id].dry_counter = 0U;
        pumps_runtime[pump_id].clogged_counter = 0U;
        return;
    }

    pumps_sample_current(pump_id);

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tach_window = pumps_runtime[pump_id].tach_window_pulses;
        pumps_runtime[pump_id].tach_window_pulses = 0U;
    }

    pumps_runtime[pump_id].last_tach_window_pulses = tach_window;

    if (pumps_runtime[pump_id].startup_windows_remaining > 0U)
    {
        pumps_runtime[pump_id].startup_windows_remaining--;
        pumps_runtime[pump_id].state = PUMP_DIAG_STATE_STARTING;
        pumps_runtime[pump_id].dry_counter = 0U;
        pumps_runtime[pump_id].clogged_counter = 0U;
        return;
    }

    tach_healthy = (tach_window >= PUMPS_HEALTHY_TACH_MIN);
    tach_weak = (tach_window <= PUMPS_WEAK_TACH_MAX);
    current_present = (pumps_runtime[pump_id].current_valid != 0U) &&
                      ((pumps_runtime[pump_id].current_avg_adc >= PUMPS_CURRENT_PRESENT_AVG_MIN) ||
                       (pumps_runtime[pump_id].current_peak_adc >= PUMPS_CURRENT_PRESENT_PEAK_MIN));
    current_weak = (pumps_runtime[pump_id].current_valid != 0U) &&
                   (pumps_runtime[pump_id].current_avg_adc <= PUMPS_CURRENT_WEAK_AVG_MAX) &&
                   (pumps_runtime[pump_id].current_peak_adc <= PUMPS_CURRENT_WEAK_PEAK_MAX);

    if (tach_healthy)
    {
        pumps_runtime[pump_id].dry_counter = 0U;
        pumps_runtime[pump_id].clogged_counter = 0U;
        pumps_runtime[pump_id].state = PUMP_DIAG_STATE_RUNNING;
        return;
    }

    if (!pumps_runtime[pump_id].current_valid)
    {
        pumps_runtime[pump_id].dry_counter = 0U;
        pumps_runtime[pump_id].clogged_counter = 0U;
        pumps_runtime[pump_id].state = PUMP_DIAG_STATE_RUNNING;
        return;
    }

    if (tach_weak && current_weak)
    {
        if (pumps_runtime[pump_id].dry_counter < 255U)
        {
            pumps_runtime[pump_id].dry_counter++;
        }
        pumps_runtime[pump_id].clogged_counter = 0U;
    }
    else if (tach_weak && current_present)
    {
        if (pumps_runtime[pump_id].clogged_counter < 255U)
        {
            pumps_runtime[pump_id].clogged_counter++;
        }
        pumps_runtime[pump_id].dry_counter = 0U;
    }
    else
    {
        pumps_runtime[pump_id].dry_counter = 0U;
        pumps_runtime[pump_id].clogged_counter = 0U;
    }

    if (pumps_runtime[pump_id].dry_counter >= PUMPS_DIAG_MAYBE_THRESHOLD)
    {
        pumps_runtime[pump_id].state = PUMP_DIAG_STATE_MAYBE_DRY;
    }
    else if (pumps_runtime[pump_id].clogged_counter >= PUMPS_DIAG_MAYBE_THRESHOLD)
    {
        pumps_runtime[pump_id].state = PUMP_DIAG_STATE_MAYBE_CLOGGED;
    }
    else
    {
        pumps_runtime[pump_id].state = PUMP_DIAG_STATE_RUNNING;
    }
}


/**************************************************************************************************
 * @brief Write one raw 16-bit frame to the MCP4812.
 **************************************************************************************************/
static void pumps_write_dac_raw(uint16_t value)
{
    GPIO_LOW(PUMP_DAC_CS_PORT, PUMP_DAC_CS_BIT);

    SPDR = (uint8_t)(value >> 8);
    while ((SPSR & (1U << SPIF)) == 0U)
    {
    }

    SPDR = (uint8_t)value;
    while ((SPSR & (1U << SPIF)) == 0U)
    {
    }

    GPIO_HIGH(PUMP_DAC_CS_PORT, PUMP_DAC_CS_BIT);
}


/**************************************************************************************************
 * @brief Write one logical speed to one DAC channel.
 **************************************************************************************************/
static void pumps_write_dac_speed(pump_id_t pump_id, uint8_t speed)
{
    uint16_t value;

    if (!pumps_is_valid_id(pump_id))
    {
        return;
    }

    value = pumps_hw[pump_id].dac_channel_bit |
            PUMPS_DAC_ACTIVE_BIT |
            ((uint16_t)speed << 4);

    pumps_write_dac_raw(value);
}


/**************************************************************************************************
 * @brief Pulse LDAC so both DAC outputs update.
 **************************************************************************************************/
static void pumps_latch_dac()
{
    GPIO_LOW(PUMP_DAC_LDAC_PORT, PUMP_DAC_LDAC_BIT);
    __asm__ __volatile__("nop\n\tnop\n\tnop\n\t");
    GPIO_HIGH(PUMP_DAC_LDAC_PORT, PUMP_DAC_LDAC_BIT);
}


/**************************************************************************************************
 * @brief Check whether one pump id is valid.
 **************************************************************************************************/
static bool pumps_is_valid_id(pump_id_t pump_id)
{
    return ((pump_id == PUMP_ID_MASH) || (pump_id == PUMP_ID_BOIL));
}


// ======================================================================================
//  INTERRUPTS
// ======================================================================================

/*********************************************************************
 * @brief Handle the INT4_vect interrupt service routine.
 *********************************************************************/
ISR(INT4_vect)
{
    if (pumps_runtime[PUMP_ID_BOIL].running != 0U)
    {
        pumps_runtime[PUMP_ID_BOIL].tach_total_pulses++;
        pumps_runtime[PUMP_ID_BOIL].tach_window_pulses++;
    }
}


/*********************************************************************
 * @brief Handle the INT5_vect interrupt service routine.
 *********************************************************************/
ISR(INT5_vect)
{
    if (pumps_runtime[PUMP_ID_MASH].running != 0U)
    {
        pumps_runtime[PUMP_ID_MASH].tach_total_pulses++;
        pumps_runtime[PUMP_ID_MASH].tach_window_pulses++;
    }
}


/*********************************************************************
 * @brief Handle the TIMER3_COMPA_vect interrupt service routine.
 *********************************************************************/
ISR(TIMER3_COMPA_vect)
{
    if (pumps_diag_tick_pending < 255U)
    {
        pumps_diag_tick_pending++;
    }
}
