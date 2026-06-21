#include "Board.h"
#include "Buttons.h"

#define BUTTON_DEBOUNCE_MAX 5U

static uint8_t power_count = 0U;
static uint8_t drain_count = 0U;

volatile bool power_btn_down = false;
volatile bool drain_btn_down = false;
volatile bool power_btn_flag = false;
volatile bool drain_btn_flag = false;

/*********************************************************************
 * @brief Initialize buttons.
 *********************************************************************/
void buttons_init()
{
    /*
     * Match the old ReBrewie electrical behavior documented for this project:
     * both buttons are plain digital inputs, with no internal MCU pull-up,
     * and a pressed button reads HIGH.
     */
    GPIO_INPUT(POWER_BUTTON_DDR, POWER_BUTTON_BIT);
    GPIO_INPUT(DRAIN_BUTTON_DDR, DRAIN_BUTTON_BIT);

    GPIO_PULLUP_OFF(POWER_BUTTON_PORT, POWER_BUTTON_BIT);
    GPIO_PULLUP_OFF(DRAIN_BUTTON_PORT, DRAIN_BUTTON_BIT);
}

/*********************************************************************
 * @brief Update buttons.
 *********************************************************************/
void buttons_update()
{
    /* Snapshot the GPIO port once so both button decisions are based on the
     * same sample point. */
    const uint8_t pind_snapshot = PIND;

    const bool power_active = (pind_snapshot >> POWER_BUTTON_BIT) & 1U;
    const bool drain_active = (pind_snapshot >> DRAIN_BUTTON_BIT) & 1U;
    const bool old_power    = power_btn_down;
    const bool old_drain    = drain_btn_down;

    /*
     * Saturating up/down debounce counters:
     * - count upward while the raw input looks active
     * - count downward while the raw input looks inactive
     * This avoids branches and gives a small amount of input hysteresis.
     */
    power_count += (uint8_t)(
          ( power_active & (power_count < BUTTON_DEBOUNCE_MAX))
        - (!power_active & (power_count > 0U))
    );

    drain_count += (uint8_t)(
          ( drain_active & (drain_count < BUTTON_DEBOUNCE_MAX))
        - (!drain_active & (drain_count > 0U))
    );

    /*
     * Once a button is considered down, keep it down until the counter has
     * fully decayed. This makes the debounced state less twitchy near the
     * threshold.
     */
    power_btn_down  = (power_count >= BUTTON_DEBOUNCE_MAX) | ((power_count > 0U) & old_power);
    drain_btn_down  = (drain_count >= BUTTON_DEBOUNCE_MAX) | ((drain_count > 0U) & old_drain);

    /* Latch only the press edge; higher layers consume and clear the flags. */
    power_btn_flag |= (power_btn_down & !old_power);
    drain_btn_flag |= (drain_btn_down & !old_drain);
}
