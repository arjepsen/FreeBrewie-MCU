#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include "Faults.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    SUPERVISOR_STATE_STANDBY = 0,
    SUPERVISOR_STATE_BOOT,
    SUPERVISOR_STATE_ACTIVE,
    SUPERVISOR_STATE_FAULT,
    SUPERVISOR_STATE_SHUTDOWN
} supervisor_state_t;

typedef enum
{
    SUPERVISOR_EVENT_NONE = 0,
    SUPERVISOR_EVENT_POWER_BUTTON = (1U << 0),
    SUPERVISOR_EVENT_DRAIN_BUTTON = (1U << 1),
    SUPERVISOR_EVENT_FAULT_CHANGED = (1U << 2)
} supervisor_event_flag_t;

typedef enum
{
    SUPERVISOR_BOOT_PHASE_PRECHARGE = 0,
    SUPERVISOR_BOOT_PHASE_5V,
    SUPERVISOR_BOOT_PHASE_12V
} supervisor_boot_phase_t;

typedef enum
{
    SUPERVISOR_SHUTDOWN_PHASE_START = 0,
    SUPERVISOR_SHUTDOWN_PHASE_WAIT_COMPLETE = 1,
    SUPERVISOR_SHUTDOWN_PHASE_COMPLETE = 2
} supervisor_shutdown_phase_t;

typedef enum
{
    SUPERVISOR_OUTPUT_FLAG_NONE = 0,
    SUPERVISOR_OUTPUT_FLAG_PRECHARGE_5V = (1U << 0),
    SUPERVISOR_OUTPUT_FLAG_PWR_5V = (1U << 1),
    SUPERVISOR_OUTPUT_FLAG_PWR_12V = (1U << 2),
    SUPERVISOR_OUTPUT_FLAG_PWR_6V5_SERVO = (1U << 3),
    SUPERVISOR_OUTPUT_FLAG_MCU_LED1 = (1U << 4),
    SUPERVISOR_OUTPUT_FLAG_POWER_LED = (1U << 5),
    SUPERVISOR_OUTPUT_FLAG_DRAIN_LED = (1U << 6),
    SUPERVISOR_OUTPUT_FLAG_BREW_INLET = (1U << 7),
    SUPERVISOR_OUTPUT_FLAG_COOLING_INLET = (1U << 8),
    SUPERVISOR_OUTPUT_FLAG_MASH_PUMP_ENABLE = (1U << 9),
    SUPERVISOR_OUTPUT_FLAG_BOIL_PUMP_ENABLE = (1U << 10),
    SUPERVISOR_OUTPUT_FLAG_FAN_ENABLE = (1U << 11),
    SUPERVISOR_OUTPUT_FLAG_HEATER_ENABLE = (1U << 12)
} supervisor_output_flag_t;

typedef enum
{
    SUPERVISOR_OUTPUT_MODE_NONE = 0,
    SUPERVISOR_OUTPUT_MODE_SAFE_ALL_OFF = (1U << 0),
    SUPERVISOR_OUTPUT_MODE_ACTIVE = (1U << 1),
    SUPERVISOR_OUTPUT_MODE_SHUTDOWN = (1U << 2),
    SUPERVISOR_OUTPUT_MODE_FAULT = (1U << 3)
} supervisor_output_mode_flag_t;

typedef enum
{
    SUPERVISOR_HEATER_REQUEST_NONE = 0,
    SUPERVISOR_HEATER_REQUEST_MASH,
    SUPERVISOR_HEATER_REQUEST_BOIL
} supervisor_heater_request_t;

typedef struct
{
    uint8_t mash_target_c;
    uint8_t boil_target_c;
    uint8_t mash_pump_setpoint;
    uint8_t boil_pump_setpoint;
    uint8_t solenoid_state_bits;
    uint8_t valve_command[11];
} supervisor_control_snapshot_t;

typedef struct
{
    uint16_t binary_flags;
    uint8_t mode_flags;
    uint8_t mash_pump_speed;
    uint8_t boil_pump_speed;
    uint8_t valve_id;
    uint8_t valve_position;
    supervisor_heater_request_t heater_request;
} supervisor_output_request_t;

typedef struct
{
    supervisor_state_t state;
    supervisor_state_t previous_state;
    uint32_t time_ms;
    uint32_t state_entry_time_ms;
    uint32_t deadline_ms;
    bool state_entry;
    uint8_t pending_events;
    fault_reason_t primary_fault_code;
    supervisor_boot_phase_t boot_phase;
    supervisor_shutdown_phase_t shutdown_phase;
    supervisor_control_snapshot_t accepted_snapshot;
    supervisor_output_request_t requested_outputs;
} supervisor_runtime_t;

extern supervisor_runtime_t supervisor_runtime;

void supervisor_init();
void supervisor_update();

#endif /* SUPERVISOR_H */
