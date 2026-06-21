#ifndef COMMS_H
#define COMMS_H

#include "Supervisor.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    COMMS_TYPE_CONTROL_SNAPSHOT = 0x01,
    COMMS_TYPE_HEARTBEAT = 0x02,
    COMMS_TYPE_STATUS_REPORT = 0x03,
    COMMS_TYPE_FAULT_REPORT = 0x04,
    COMMS_TYPE_ACK = 0x05,
    COMMS_TYPE_NACK = 0x06,
    COMMS_TYPE_SHUTDOWN_REQUEST = 0x07,
    COMMS_TYPE_FAULT_CLEAR_REQUEST = 0x08
} comms_message_type_t;

typedef enum
{
    COMMS_NACK_REASON_UNKNOWN_TYPE = 0x01,
    COMMS_NACK_REASON_BAD_LENGTH = 0x02,
    COMMS_NACK_REASON_BAD_PAYLOAD = 0x03,
    COMMS_NACK_REASON_INVALID_STATE = 0x04,
    COMMS_NACK_REASON_SAFETY_REJECTED = 0x05,
    COMMS_NACK_REASON_BUSY = 0x06,
    COMMS_NACK_REASON_UNCLEARABLE_FAULT = 0x07
} comms_nack_reason_t;

typedef struct
{
    bool heartbeat_seen;
    bool heartbeat_alive;
    bool shutdown_request_pending;
    bool fault_clear_request_pending;
    bool control_snapshot_pending;
    uint8_t last_rx_seq;
    uint8_t next_tx_seq;
    uint32_t last_heartbeat_time_ms;
    uint32_t next_status_report_time_ms;
    supervisor_control_snapshot_t pending_control_snapshot;
} comms_runtime_t;

extern comms_runtime_t comms_runtime;

void comms_init();
void comms_update();

bool comms_take_pending_control_snapshot(supervisor_control_snapshot_t *snapshot);
bool comms_take_shutdown_request();
bool comms_take_fault_clear_request();

#endif /* COMMS_H */
