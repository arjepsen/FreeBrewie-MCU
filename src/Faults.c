#include "Faults.h"
#include "Supervisor.h"

#include <stdbool.h>
#include <stdint.h>

#define FAULTS_ALLOWED_STANDBY_FLAGS             SUPERVISOR_OUTPUT_FLAG_MCU_LED1

#define FAULTS_ALLOWED_BOOT_FLAGS                (SUPERVISOR_OUTPUT_FLAG_MCU_LED1 | \
                                                  SUPERVISOR_OUTPUT_FLAG_PRECHARGE_5V | \
                                                  SUPERVISOR_OUTPUT_FLAG_PWR_5V | \
                                                  SUPERVISOR_OUTPUT_FLAG_PWR_12V | \
                                                  SUPERVISOR_OUTPUT_FLAG_POWER_LED)

#define FAULTS_ALLOWED_ACTIVE_FLAGS              (SUPERVISOR_OUTPUT_FLAG_MCU_LED1 | \
                                                  SUPERVISOR_OUTPUT_FLAG_PRECHARGE_5V | \
                                                  SUPERVISOR_OUTPUT_FLAG_PWR_5V | \
                                                  SUPERVISOR_OUTPUT_FLAG_PWR_12V | \
                                                  SUPERVISOR_OUTPUT_FLAG_POWER_LED)

#define FAULTS_ALLOWED_FAULT_FLAGS               (SUPERVISOR_OUTPUT_FLAG_PRECHARGE_5V | \
                                                  SUPERVISOR_OUTPUT_FLAG_PWR_5V | \
                                                  SUPERVISOR_OUTPUT_FLAG_PWR_12V | \
                                                  SUPERVISOR_OUTPUT_FLAG_POWER_LED)

#define FAULTS_ALLOWED_SHUTDOWN_FLAGS            SUPERVISOR_OUTPUT_FLAG_NONE

#define FAULTS_CLEARABLE_FLAGS                   (FAULT_FLAG_UNSAFE_STANDBY_REQUEST | \
                                                  FAULT_FLAG_UNSAFE_ACTIVE_REQUEST | \
                                                  FAULT_FLAG_UNSAFE_FAULT_REQUEST | \
                                                  FAULT_FLAG_UNSAFE_SHUTDOWN_REQUEST | \
                                                  FAULT_FLAG_UNSAFE_BOOT_REQUEST)

#define FAULTS_MODE_FAULT_MASK                   (SUPERVISOR_OUTPUT_MODE_SAFE_ALL_OFF | SUPERVISOR_OUTPUT_MODE_FAULT)

#define FAULTS_REQUEST_HAS_ANY_SPEED(request_ptr)    (((request_ptr)->mash_pump_speed != 0U) || \
                                                      ((request_ptr)->boil_pump_speed != 0U))

#define FAULTS_REQUEST_HAS_ANY_VALVE(request_ptr)    (((request_ptr)->valve_id != 0U) || \
                                                      ((request_ptr)->valve_position != 0U))

#define FAULTS_REQUEST_HAS_HEATER(request_ptr)       ((request_ptr)->heater_request != SUPERVISOR_HEATER_REQUEST_NONE)

#define FAULTS_REQUEST_HAS_ANY_WORK(request_ptr)     (FAULTS_REQUEST_HAS_ANY_SPEED(request_ptr) || \
                                                      FAULTS_REQUEST_HAS_ANY_VALVE(request_ptr) || \
                                                      FAULTS_REQUEST_HAS_HEATER(request_ptr))

faults_runtime_t faults_runtime =
{
    .active_flags = FAULT_FLAG_NONE,
    .latched_flags = FAULT_FLAG_NONE,
    .clearable_flags = FAULT_FLAG_NONE,
    .primary_reason = FAULT_REASON_NONE,
    .changed = false,
    .force_safe_outputs = false,
    .clear_request_pending = false
};

static fault_reason_t faults_select_primary_reason(uint16_t active_flags);

/*********************************************************************
 * @brief Initialize faults.
 *********************************************************************/
void faults_init()
{
    faults_runtime.active_flags = FAULT_FLAG_NONE;
    faults_runtime.latched_flags = FAULT_FLAG_NONE;
    faults_runtime.clearable_flags = FAULT_FLAG_NONE;
    faults_runtime.primary_reason = FAULT_REASON_NONE;
    faults_runtime.changed = false;
    faults_runtime.force_safe_outputs = false;
    faults_runtime.clear_request_pending = false;
}

/*********************************************************************
 * @brief Update faults.
 *********************************************************************/
void faults_update()
{
    const supervisor_runtime_t *runtime = &supervisor_runtime;
    const supervisor_output_request_t *request = &runtime->requested_outputs;
    uint16_t previous_active_flags = faults_runtime.active_flags;
    uint16_t problem_flags = FAULT_FLAG_NONE;
    bool has_any_work = FAULTS_REQUEST_HAS_ANY_WORK(request);

    /*
     * Validate the currently requested output picture. At this stage faults are
     * still mainly structural/sanity checks rather than rich machine diagnostics.
     */
    if ((((request->binary_flags & SUPERVISOR_OUTPUT_FLAG_HEATER_ENABLE) != 0U) &&
         (request->heater_request == SUPERVISOR_HEATER_REQUEST_NONE)) ||
        (((request->binary_flags & SUPERVISOR_OUTPUT_FLAG_HEATER_ENABLE) == 0U) &&
         (request->heater_request != SUPERVISOR_HEATER_REQUEST_NONE)))
    {
        problem_flags |= FAULT_FLAG_INVALID_HEATER_REQUEST;
    }

    if (((request->binary_flags & SUPERVISOR_OUTPUT_FLAG_MASH_PUMP_ENABLE) == 0U) &&
        (request->mash_pump_speed != 0U))
    {
        problem_flags |= FAULT_FLAG_INVALID_MASH_PUMP_REQUEST;
    }

    if (((request->binary_flags & SUPERVISOR_OUTPUT_FLAG_BOIL_PUMP_ENABLE) == 0U) &&
        (request->boil_pump_speed != 0U))
    {
        problem_flags |= FAULT_FLAG_INVALID_BOIL_PUMP_REQUEST;
    }

    if ((request->valve_id == 0U) && (request->valve_position != 0U))
    {
        problem_flags |= FAULT_FLAG_INVALID_VALVE_REQUEST;
    }

    if (runtime->state > SUPERVISOR_STATE_SHUTDOWN)
    {
        problem_flags |= FAULT_FLAG_INVALID_SUPERVISOR_STATE;
    }

    switch (runtime->state)
    {
        case SUPERVISOR_STATE_STANDBY:
            if (((request->binary_flags & (uint16_t)(~FAULTS_ALLOWED_STANDBY_FLAGS)) != 0U) ||
                (request->mode_flags != SUPERVISOR_OUTPUT_MODE_NONE) ||
                has_any_work)
            {
                problem_flags |= FAULT_FLAG_UNSAFE_STANDBY_REQUEST;
            }
            break;

        case SUPERVISOR_STATE_BOOT:
            if (((request->binary_flags & (uint16_t)(~FAULTS_ALLOWED_BOOT_FLAGS)) != 0U) ||
                (request->mode_flags != SUPERVISOR_OUTPUT_MODE_NONE) ||
                has_any_work)
            {
                problem_flags |= FAULT_FLAG_UNSAFE_BOOT_REQUEST;
            }
            break;

        case SUPERVISOR_STATE_ACTIVE:
            if (((request->binary_flags & (uint16_t)(~FAULTS_ALLOWED_ACTIVE_FLAGS)) != 0U) ||
                (request->mode_flags != SUPERVISOR_OUTPUT_MODE_ACTIVE) ||
                has_any_work)
            {
                problem_flags |= FAULT_FLAG_UNSAFE_ACTIVE_REQUEST;
            }
            break;

        case SUPERVISOR_STATE_FAULT:
            if (((request->binary_flags & (uint16_t)(~FAULTS_ALLOWED_FAULT_FLAGS)) != 0U) ||
                ((request->mode_flags & FAULTS_MODE_FAULT_MASK) != FAULTS_MODE_FAULT_MASK) ||
                has_any_work)
            {
                problem_flags |= FAULT_FLAG_UNSAFE_FAULT_REQUEST;
            }
            break;

        case SUPERVISOR_STATE_SHUTDOWN:
        default:
            if (((request->binary_flags & (uint16_t)(~FAULTS_ALLOWED_SHUTDOWN_FLAGS)) != 0U) ||
                ((request->mode_flags & SUPERVISOR_OUTPUT_MODE_SAFE_ALL_OFF) == 0U) ||
                has_any_work)
            {
                problem_flags |= FAULT_FLAG_UNSAFE_SHUTDOWN_REQUEST;
            }
            break;
    }

    /* Track which currently observed problems are allowed to be cleared by request. */
    faults_runtime.clearable_flags = (problem_flags & FAULTS_CLEARABLE_FLAGS);
    faults_runtime.latched_flags |= problem_flags;

    if (faults_runtime.clear_request_pending)
    {
        /* Only drop clearable latches if the triggering condition is no longer present. */
        if ((problem_flags & faults_runtime.clearable_flags) == 0U)
        {
            faults_runtime.latched_flags &= (uint16_t)(~FAULTS_CLEARABLE_FLAGS);
        }

        faults_runtime.clear_request_pending = false;
    }

    /* Publish the final fault picture for the rest of the firmware. */
    faults_runtime.active_flags = (uint16_t)(problem_flags | (faults_runtime.latched_flags & FAULTS_CLEARABLE_FLAGS));
    faults_runtime.force_safe_outputs = (faults_runtime.active_flags != 0U);
    faults_runtime.changed = (faults_runtime.active_flags != previous_active_flags);
    faults_runtime.primary_reason = faults_select_primary_reason(faults_runtime.active_flags);
}

/*********************************************************************
 * @brief Select faults primary reason.
 *********************************************************************/
static fault_reason_t faults_select_primary_reason(uint16_t active_flags)
{
    if ((active_flags & FAULT_FLAG_INVALID_SUPERVISOR_STATE) != 0U)
    {
        return FAULT_REASON_INVALID_SUPERVISOR_STATE;
    }

    if ((active_flags & FAULT_FLAG_INVALID_HEATER_REQUEST) != 0U)
    {
        return FAULT_REASON_INVALID_HEATER_REQUEST;
    }

    if ((active_flags & FAULT_FLAG_INVALID_MASH_PUMP_REQUEST) != 0U)
    {
        return FAULT_REASON_INVALID_MASH_PUMP_REQUEST;
    }

    if ((active_flags & FAULT_FLAG_INVALID_BOIL_PUMP_REQUEST) != 0U)
    {
        return FAULT_REASON_INVALID_BOIL_PUMP_REQUEST;
    }

    if ((active_flags & FAULT_FLAG_INVALID_VALVE_REQUEST) != 0U)
    {
        return FAULT_REASON_INVALID_VALVE_REQUEST;
    }

    if ((active_flags & FAULT_FLAG_UNSAFE_STANDBY_REQUEST) != 0U)
    {
        return FAULT_REASON_UNSAFE_STANDBY_REQUEST;
    }

    if ((active_flags & FAULT_FLAG_UNSAFE_BOOT_REQUEST) != 0U)
    {
        return FAULT_REASON_UNSAFE_BOOT_REQUEST;
    }

    if ((active_flags & FAULT_FLAG_UNSAFE_ACTIVE_REQUEST) != 0U)
    {
        return FAULT_REASON_UNSAFE_ACTIVE_REQUEST;
    }

    if ((active_flags & FAULT_FLAG_UNSAFE_FAULT_REQUEST) != 0U)
    {
        return FAULT_REASON_UNSAFE_FAULT_REQUEST;
    }

    if ((active_flags & FAULT_FLAG_UNSAFE_SHUTDOWN_REQUEST) != 0U)
    {
        return FAULT_REASON_UNSAFE_SHUTDOWN_REQUEST;
    }

    return FAULT_REASON_NONE;
}
