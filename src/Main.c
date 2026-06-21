#include "Supervisor.h"
#include "Comms.h"
#include "Faults.h"
#include "Outputs.h"
#include "Runtime.h"

#include <avr/interrupt.h>


// ======================================================================================
//  PROTOTYPES
// ======================================================================================
static void main_init();
static void main_loop();


// ======================================================================================
//  MAIN
// ======================================================================================

/*********************************************************************
 * @brief Firmware entry point.
 *
 * Brings the top-level runtime online, enables interrupts, and then
 * runs the documented outer service loop.
 *
 * @return Never returns.
 *********************************************************************/
int main()
{
    main_init();

    /* From this point onward the drivers that rely on interrupts may run. */
    sei();

    main_loop();
    return 0;
}


// ======================================================================================
//  INTERNAL FUNCTIONS
// ======================================================================================

/*********************************************************************
 * @brief Initialize the top-level runtime modules.
 *
 * Main owns only the overall initialization order. Runtime-service
 * helpers live outside Main so this file stays small and truthful.
 *********************************************************************/
static void main_init()
{
    runtime_init();
    faults_init();
    supervisor_init();
    outputs_init();
}

/*********************************************************************
 * @brief Run the preferred outer firmware loop.
 *
 * The service order matches the current architecture documents:
 * fast tasks, timed tasks, communications, faults, supervisor, then
 * final output commit.
 *********************************************************************/
static void main_loop()
{
    for (;;)
    {
        service_fast_tasks();
        service_timed_tasks();
        comms_update();
        faults_update();
        supervisor_update();
        outputs_apply();
    }
}
