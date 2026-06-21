#ifndef FAULTS_H
#define FAULTS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    FAULT_REASON_NONE = 0,
    FAULT_REASON_INVALID_SUPERVISOR_STATE = 1,
    FAULT_REASON_INVALID_HEATER_REQUEST = 2,
    FAULT_REASON_INVALID_MASH_PUMP_REQUEST = 3,
    FAULT_REASON_INVALID_BOIL_PUMP_REQUEST = 4,
    FAULT_REASON_INVALID_VALVE_REQUEST = 5,
    FAULT_REASON_UNSAFE_STANDBY_REQUEST = 6,
    FAULT_REASON_UNSAFE_ACTIVE_REQUEST = 7,
    FAULT_REASON_UNSAFE_FAULT_REQUEST = 8,
    FAULT_REASON_UNSAFE_SHUTDOWN_REQUEST = 9,
    FAULT_REASON_UNSAFE_BOOT_REQUEST = 10
} fault_reason_t;

typedef enum
{
    FAULT_FLAG_NONE = 0,
    FAULT_FLAG_INVALID_SUPERVISOR_STATE = (1U << 0),
    FAULT_FLAG_INVALID_HEATER_REQUEST = (1U << 1),
    FAULT_FLAG_INVALID_MASH_PUMP_REQUEST = (1U << 2),
    FAULT_FLAG_INVALID_BOIL_PUMP_REQUEST = (1U << 3),
    FAULT_FLAG_INVALID_VALVE_REQUEST = (1U << 4),
    FAULT_FLAG_UNSAFE_STANDBY_REQUEST = (1U << 5),
    FAULT_FLAG_UNSAFE_ACTIVE_REQUEST = (1U << 6),
    FAULT_FLAG_UNSAFE_FAULT_REQUEST = (1U << 7),
    FAULT_FLAG_UNSAFE_SHUTDOWN_REQUEST = (1U << 8),
    FAULT_FLAG_UNSAFE_BOOT_REQUEST = (1U << 9)
} fault_flag_t;

typedef struct
{
    uint16_t active_flags;
    uint16_t latched_flags;
    uint16_t clearable_flags;
    fault_reason_t primary_reason;
    bool changed;
    bool force_safe_outputs;
    bool clear_request_pending;
} faults_runtime_t;

extern faults_runtime_t faults_runtime;

void faults_init();
void faults_update();

#endif /* FAULTS_H */
