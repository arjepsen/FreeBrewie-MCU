#include "Supervisor.h"
#include "Buttons.h"
#include "Comms.h"

#include <stdint.h>


// ======================================================================================
//  DEFINES
// ======================================================================================

#define SUPERVISOR_UPDATE_PERIOD_MS                      1U
#define SUPERVISOR_SHUTDOWN_SETTLE_DELAY_MS              50U

#define SUPERVISOR_STANDBY_BASE_FLAGS                    SUPERVISOR_OUTPUT_FLAG_MCU_LED1

#define SUPERVISOR_BOOT_BASE_FLAGS                       (SUPERVISOR_OUTPUT_FLAG_MCU_LED1 | \
                                                          SUPERVISOR_OUTPUT_FLAG_PRECHARGE_5V | \
                                                          SUPERVISOR_OUTPUT_FLAG_PWR_5V | \
                                                          SUPERVISOR_OUTPUT_FLAG_PWR_12V | \
                                                          SUPERVISOR_OUTPUT_FLAG_POWER_LED)

#define SUPERVISOR_ACTIVE_BASE_FLAGS                     SUPERVISOR_BOOT_BASE_FLAGS

#define SUPERVISOR_FAULT_BASE_FLAGS                      (SUPERVISOR_OUTPUT_FLAG_PRECHARGE_5V | \
                                                          SUPERVISOR_OUTPUT_FLAG_PWR_5V | \
                                                          SUPERVISOR_OUTPUT_FLAG_PWR_12V | \
                                                          SUPERVISOR_OUTPUT_FLAG_POWER_LED)

#define SUPERVISOR_REQUEST_CLEAR(request_ptr)            do { \
                                                              *(request_ptr) = (supervisor_output_request_t){ \
                                                                  SUPERVISOR_OUTPUT_FLAG_NONE, \
                                                                  SUPERVISOR_OUTPUT_MODE_NONE, \
                                                                  0U, \
                                                                  0U, \
                                                                  0U, \
                                                                  0U, \
                                                                  SUPERVISOR_HEATER_REQUEST_NONE \
                                                              }; \
                                                          } while (0)

#define SUPERVISOR_ENTER_STATE(next_state)               do { \
                                                              supervisor_runtime.previous_state = supervisor_runtime.state; \
                                                              supervisor_runtime.state = (next_state); \
                                                              supervisor_runtime.state_entry = true; \
                                                              supervisor_runtime.state_entry_time_ms = supervisor_runtime.time_ms; \
                                                          } while (0)

#define SUPERVISOR_DEADLINE_REACHED()                    ((int32_t)(supervisor_runtime.time_ms - supervisor_runtime.deadline_ms) >= 0)

#define SUPERVISOR_SNAPSHOT_BREW_INLET_BIT               (1U << 0)
#define SUPERVISOR_SNAPSHOT_COOLING_INLET_BIT            (1U << 1)


// ======================================================================================
//  PROTOTYPES
// ======================================================================================
static uint8_t supervisor_collect_pending_events();
static void supervisor_consume_pending_control_snapshot();
static void supervisor_apply_snapshot_to_request(supervisor_output_request_t *request);
static void supervisor_raise_fault_clear_request();


// ======================================================================================
//  GLOBALS
// ======================================================================================

supervisor_runtime_t supervisor_runtime =
{
    .state = SUPERVISOR_STATE_STANDBY,
    .previous_state = SUPERVISOR_STATE_STANDBY,
    .time_ms = 0UL,
    .state_entry_time_ms = 0UL,
    .deadline_ms = 0UL,
    .state_entry = true,
    .pending_events = SUPERVISOR_EVENT_NONE,
    .primary_fault_code = FAULT_REASON_NONE,
    .boot_phase = SUPERVISOR_BOOT_PHASE_PRECHARGE,
    .shutdown_phase = SUPERVISOR_SHUTDOWN_PHASE_START,
    .accepted_snapshot = { 0U, 0U, 0U, 0U, 0U, { 0U } },
    .requested_outputs = { SUPERVISOR_OUTPUT_FLAG_NONE, SUPERVISOR_OUTPUT_MODE_NONE, 0U, 0U, 0U, 0U, SUPERVISOR_HEATER_REQUEST_NONE }
};


// ======================================================================================
//  API FUNCTIONS
// ======================================================================================

/*********************************************************************
 * @brief Initialize the supervisor runtime state.
 *
 * The supervisor starts in STANDBY so the dedicated MCU supply can be
 * alive without automatically raising the rest of the machine rails. The
 * accepted control snapshot stays cleared until higher layers supply valid
 * target data.
 *********************************************************************/
void supervisor_init()
{
    supervisor_runtime.state = SUPERVISOR_STATE_STANDBY;
    supervisor_runtime.previous_state = SUPERVISOR_STATE_STANDBY;
    supervisor_runtime.time_ms = 0UL;
    supervisor_runtime.state_entry_time_ms = 0UL;
    supervisor_runtime.deadline_ms = 0UL;
    supervisor_runtime.state_entry = true;
    supervisor_runtime.pending_events = SUPERVISOR_EVENT_NONE;
    supervisor_runtime.primary_fault_code = FAULT_REASON_NONE;
    supervisor_runtime.boot_phase = SUPERVISOR_BOOT_PHASE_PRECHARGE;
    supervisor_runtime.shutdown_phase = SUPERVISOR_SHUTDOWN_PHASE_START;
    supervisor_runtime.accepted_snapshot = (supervisor_control_snapshot_t){ 0U, 0U, 0U, 0U, 0U, { 0U } };
    SUPERVISOR_REQUEST_CLEAR(&supervisor_runtime.requested_outputs);
}

/*********************************************************************
 * @brief Run the top-level supervisor state machine.
 *
 * The supervisor sits between raw runtime facts and final output
 * application. It decides which high-level state the MCU should be in
 * and converts accepted control intent into a compact output request.
 *********************************************************************/
void supervisor_update()
{
    supervisor_output_request_t *request = &supervisor_runtime.requested_outputs;
    uint8_t pending_events;

    /*
     * Keep a simple local millisecond notion for the supervisor. This is
     * currently advanced once per supervisor pass because the runtime loop
     * is still paced at 1 ms.
     */
    supervisor_runtime.time_ms += SUPERVISOR_UPDATE_PERIOD_MS;

    /*
     * Pull the newest validated control snapshot across the comms -> supervisor
     * boundary before state handling runs. This keeps the accepted target set
     * owned by the supervisor instead of letting the protocol layer reach into
     * output logic directly.
     */
    supervisor_consume_pending_control_snapshot();

    /*
     * Gather edge-like events from buttons and fault transitions so the
     * state logic can react without reading scattered globals directly.
     */
    pending_events = supervisor_collect_pending_events();
    supervisor_runtime.pending_events = pending_events;
    supervisor_runtime.primary_fault_code = faults_runtime.primary_reason;

    /*
     * A non-zero central fault picture forces the supervisor into FAULT,
     * unless a shutdown sequence has already taken ownership.
     */
    if ((faults_runtime.active_flags != 0U) &&
        (supervisor_runtime.state != SUPERVISOR_STATE_FAULT) &&
        (supervisor_runtime.state != SUPERVISOR_STATE_SHUTDOWN))
    {
        SUPERVISOR_ENTER_STATE(SUPERVISOR_STATE_FAULT);
    }

    /*
     * Start from a cleared request every pass. Each state then builds the
     * exact request it wants to publish to outputs_apply().
     */
    SUPERVISOR_REQUEST_CLEAR(request);

    switch (supervisor_runtime.state)
    {
        case SUPERVISOR_STATE_STANDBY:
            if (supervisor_runtime.state_entry)
            {
                supervisor_runtime.state_entry = false;
                supervisor_runtime.boot_phase = SUPERVISOR_BOOT_PHASE_PRECHARGE;
                supervisor_runtime.deadline_ms = 0UL;
            }

            /*
             * In STANDBY only the dedicated MCU supply is assumed to exist.
             * The user must explicitly request machine start with the power
             * button before the supervisor begins raising the other rails and
             * bringing the SOM online.
             */
            request->binary_flags = SUPERVISOR_STANDBY_BASE_FLAGS;
            request->mode_flags = SUPERVISOR_OUTPUT_MODE_NONE;

            if ((pending_events & SUPERVISOR_EVENT_POWER_BUTTON) != 0U)
            {
                SUPERVISOR_ENTER_STATE(SUPERVISOR_STATE_BOOT);
            }
            break;

        case SUPERVISOR_STATE_BOOT:
            if (supervisor_runtime.state_entry)
            {
                supervisor_runtime.state_entry = false;
            }

            /*
             * For the current bring-up step, BOOT only means:
             * - user has pressed the power button
             * - the SOM power rails are enabled
             * - the power-button LED is turned on
             *
             * The richer phased startup sequence can be reintroduced later once
             * this simple documented behavior is verified on hardware.
             */
            request->binary_flags = SUPERVISOR_BOOT_BASE_FLAGS;
            request->mode_flags = SUPERVISOR_OUTPUT_MODE_NONE;

            if (comms_runtime.heartbeat_seen && comms_runtime.heartbeat_alive)
            {
                SUPERVISOR_ENTER_STATE(SUPERVISOR_STATE_ACTIVE);
            }
            break;

        case SUPERVISOR_STATE_ACTIVE:
            if (supervisor_runtime.state_entry)
            {
                supervisor_runtime.state_entry = false;
            }

            request->binary_flags = SUPERVISOR_ACTIVE_BASE_FLAGS;
            request->mode_flags = SUPERVISOR_OUTPUT_MODE_ACTIVE;

            /*
             * Communication-owned requests are consumed here through the
             * public comms accessors so the protocol layer publishes facts
             * upward instead of having the supervisor poke comms internals.
             */
            if (comms_take_shutdown_request())
            {
                SUPERVISOR_ENTER_STATE(SUPERVISOR_STATE_SHUTDOWN);
                break;
            }

            if (comms_take_fault_clear_request())
            {
                supervisor_raise_fault_clear_request();
            }

            /*
             * Apply the accepted snapshot on top of the ACTIVE base flags.
             * The accepted snapshot remains the supervisor-owned source of
             * target intent until a newer accepted snapshot replaces it.
             */
            supervisor_apply_snapshot_to_request(request);

            /*
             * Heartbeat loss drops normal control ownership. The fault layer
             * should eventually own a dedicated heartbeat-loss fault, but this
             * temporary direct transition keeps behavior safe and explicit now.
             */
            if (comms_runtime.heartbeat_seen && !comms_runtime.heartbeat_alive)
            {
                SUPERVISOR_ENTER_STATE(SUPERVISOR_STATE_FAULT);
            }
            break;

        case SUPERVISOR_STATE_FAULT:
            if (supervisor_runtime.state_entry)
            {
                supervisor_runtime.state_entry = false;
            }

            request->binary_flags = SUPERVISOR_FAULT_BASE_FLAGS;
            request->mode_flags = (SUPERVISOR_OUTPUT_MODE_SAFE_ALL_OFF | SUPERVISOR_OUTPUT_MODE_FAULT);

            if (comms_take_shutdown_request())
            {
                SUPERVISOR_ENTER_STATE(SUPERVISOR_STATE_SHUTDOWN);
            }
            else
            {
                if (comms_take_fault_clear_request())
                {
                    supervisor_raise_fault_clear_request();
                }

                if ((pending_events & SUPERVISOR_EVENT_DRAIN_BUTTON) != 0U)
                {
                    supervisor_raise_fault_clear_request();
                }

                if (((pending_events & SUPERVISOR_EVENT_FAULT_CHANGED) != 0U) &&
                         (faults_runtime.active_flags == 0U) &&
                         (!comms_runtime.heartbeat_seen || comms_runtime.heartbeat_alive))
                {
                    /*
                     * Only return to ACTIVE when the fault picture is clear and
                     * communications is either healthy or not yet in use.
                     */
                    SUPERVISOR_ENTER_STATE(SUPERVISOR_STATE_ACTIVE);
                }
            }
            break;

        case SUPERVISOR_STATE_SHUTDOWN:
        default:
            if (supervisor_runtime.state_entry)
            {
                supervisor_runtime.state_entry = false;
                supervisor_runtime.shutdown_phase = SUPERVISOR_SHUTDOWN_PHASE_START;
                supervisor_runtime.deadline_ms = supervisor_runtime.time_ms + SUPERVISOR_SHUTDOWN_SETTLE_DELAY_MS;
            }

            /*
             * Shutdown explicitly requests the safe-output path. The phased
             * state is kept so the shutdown sequence can later grow without
             * reshaping the top-level flow again.
             */
            request->mode_flags = (SUPERVISOR_OUTPUT_MODE_SAFE_ALL_OFF | SUPERVISOR_OUTPUT_MODE_SHUTDOWN);

            if ((supervisor_runtime.shutdown_phase == SUPERVISOR_SHUTDOWN_PHASE_START) && SUPERVISOR_DEADLINE_REACHED())
            {
                supervisor_runtime.shutdown_phase = SUPERVISOR_SHUTDOWN_PHASE_WAIT_COMPLETE;
                supervisor_runtime.deadline_ms = supervisor_runtime.time_ms + SUPERVISOR_SHUTDOWN_SETTLE_DELAY_MS;
            }
            else if ((supervisor_runtime.shutdown_phase == SUPERVISOR_SHUTDOWN_PHASE_WAIT_COMPLETE) && SUPERVISOR_DEADLINE_REACHED())
            {
                supervisor_runtime.shutdown_phase = SUPERVISOR_SHUTDOWN_PHASE_COMPLETE;
            }
            break;
    }
}


// ======================================================================================
//  INTERNAL FUNCTIONS
// ======================================================================================

/*********************************************************************
 * @brief Pull the newest pending control snapshot into supervisor-owned storage.
 *
 * The protocol layer only validates and stages an incoming snapshot. The
 * supervisor owns the accepted target state that survives across loop passes,
 * state transitions, and output application. Keeping the handoff explicit here
 * makes it obvious where SOM intent becomes MCU control context.
 *********************************************************************/
static void supervisor_consume_pending_control_snapshot()
{
    supervisor_control_snapshot_t snapshot;

    if (comms_take_pending_control_snapshot(&snapshot))
    {
        /*
         * Always keep the newest accepted target snapshot, even if the MCU is
         * still in BOOT or temporarily in FAULT. That way the active target set
         * is already prepared when the supervisor is allowed to resume normal
         * control handling.
         */
        supervisor_runtime.accepted_snapshot = snapshot;
    }
}


/*********************************************************************
 * @brief Collect and consume edge-triggered top-level events.
 *
 * The drain button remains a generic local service action. It is no
 * longer tied to a dedicated manual-service state.
 *
 * @return Pending event flags for this update pass.
 *********************************************************************/
static uint8_t supervisor_collect_pending_events()
{
    uint8_t pending_events = SUPERVISOR_EVENT_NONE;

    if (power_btn_flag)
    {
        power_btn_flag = false;
        pending_events |= SUPERVISOR_EVENT_POWER_BUTTON;
    }

    if (drain_btn_flag)
    {
        drain_btn_flag = false;
        pending_events |= SUPERVISOR_EVENT_DRAIN_BUTTON;
    }

    if (faults_runtime.changed)
    {
        pending_events |= SUPERVISOR_EVENT_FAULT_CHANGED;
    }

    return pending_events;
}

/*********************************************************************
 * @brief Convert the accepted control snapshot into an output request.
 *
 * This helper keeps the ACTIVE-state case readable by isolating the
 * direct mapping from accepted target snapshot to requested outputs.
 *
 * @param request Output request being built for this pass.
 *********************************************************************/
static void supervisor_apply_snapshot_to_request(supervisor_output_request_t *request)
{
    const supervisor_control_snapshot_t *snapshot = &supervisor_runtime.accepted_snapshot;
    uint8_t valve_index;

    if ((snapshot->solenoid_state_bits & SUPERVISOR_SNAPSHOT_BREW_INLET_BIT) != 0U)
    {
        request->binary_flags |= SUPERVISOR_OUTPUT_FLAG_BREW_INLET;
    }

    if ((snapshot->solenoid_state_bits & SUPERVISOR_SNAPSHOT_COOLING_INLET_BIT) != 0U)
    {
        request->binary_flags |= SUPERVISOR_OUTPUT_FLAG_COOLING_INLET;
    }

    if (snapshot->mash_pump_setpoint != 0U)
    {
        request->binary_flags |= SUPERVISOR_OUTPUT_FLAG_MASH_PUMP_ENABLE;
        request->mash_pump_speed = snapshot->mash_pump_setpoint;
    }

    if (snapshot->boil_pump_setpoint != 0U)
    {
        request->binary_flags |= SUPERVISOR_OUTPUT_FLAG_BOIL_PUMP_ENABLE;
        request->boil_pump_speed = snapshot->boil_pump_setpoint;
    }

    /*
     * Until closed-loop heater control is implemented, treat any non-zero
     * target as a simple heater request toward the existing output path.
     * Mash is preferred if both are non-zero, matching the single-heater
     * request model already present in Outputs/Faults.
     */
    if (snapshot->mash_target_c != 0U)
    {
        request->binary_flags |= SUPERVISOR_OUTPUT_FLAG_HEATER_ENABLE;
        request->heater_request = SUPERVISOR_HEATER_REQUEST_MASH;
    }
    else if (snapshot->boil_target_c != 0U)
    {
        request->binary_flags |= SUPERVISOR_OUTPUT_FLAG_HEATER_ENABLE;
        request->heater_request = SUPERVISOR_HEATER_REQUEST_BOIL;
    }

    /*
     * Only publish one valve move request at a time because the existing
     * valve path is modeled around a single active move command.
     */
    for (valve_index = 0U; valve_index < (uint8_t)(sizeof(snapshot->valve_command) / sizeof(snapshot->valve_command[0])); valve_index++)
    {
        if (snapshot->valve_command[valve_index] != 0U)
        {
            request->valve_id = (uint8_t)(valve_index + 1U);
            request->valve_position = snapshot->valve_command[valve_index];
            break;
        }
    }
}

/*********************************************************************
 * @brief Raise a centralized fault-clear request.
 *
 * The supervisor uses the same central fault-clear hook regardless of
 * whether the trigger came from communications or a local button.
 *********************************************************************/
static void supervisor_raise_fault_clear_request()
{
    faults_runtime.clear_request_pending = true;
}
