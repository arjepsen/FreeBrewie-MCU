#include "Runtime.h"

#include "Buttons.h"
#include "Comms.h"
#include "Fans.h"
#include "Heaters.h"
#include "Pressure.h"
#include "Pumps.h"
#include "Temperature.h"
#include "TWI.h"
#include "UART.h"

#include <util/delay.h>


// ======================================================================================
//  DEFINES
// ======================================================================================

#define RUNTIME_TIMED_SERVICE_PERIOD_MS          1UL


// ======================================================================================
//  GLOBALS
// ======================================================================================

volatile uint32_t runtime_time_ms = 0UL;


// ======================================================================================
//  API FUNCTIONS
// ======================================================================================

/*********************************************************************
 * @brief Initialize the shared runtime-service layer.
 *
 * This owns the fast/timed service helpers and their supporting
 * infrastructure, so Main only has to call into the runtime layer.
 *********************************************************************/
void runtime_init()
{
    buttons_init();
    uart_init();
    twi_init();
    temperature_init();
    pressure_init();
    comms_init();
}

/*********************************************************************
 * @brief Run very frequent, non-blocking service work.
 *********************************************************************/
void service_fast_tasks()
{
    buttons_update();
}

/*********************************************************************
 * @brief Run timed service work at the current temporary 1 ms cadence.
 *
 * This preserves the existing pacing source while moving timed-service
 * ownership out of Main.
 *********************************************************************/
void service_timed_tasks()
{
    _delay_ms(RUNTIME_TIMED_SERVICE_PERIOD_MS);
    runtime_time_ms += RUNTIME_TIMED_SERVICE_PERIOD_MS;

    heaters_update();
    pumps_update();
    fans_update();
}
