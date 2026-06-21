#include "Comms.h"

#include "CRC.h"
#include "Faults.h"
#include "Pressure.h"
#include "Runtime.h"
#include "UART.h"
#include "Valves.h"
#include "Pumps.h"
#include "Solenoids.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// ======================================================================================
//  DEFINES
// ======================================================================================

#define COMMS_FRAME_SYNC1                           0xAAU
#define COMMS_FRAME_SYNC2                           0x55U
#define COMMS_FRAME_MAX_DATA_LENGTH                 40U
#define COMMS_HEARTBEAT_TIMEOUT_MS                  3000UL
#define COMMS_STATUS_REPORT_PERIOD_MS               1000UL
#define COMMS_SOLENOID_BIT_BREW_INLET              (1U << 0)
#define COMMS_SOLENOID_BIT_COOLING_INLET           (1U << 1)
#define COMMS_STATUS_BIT_MASH_TEMP_VALID           (1U << 0)
#define COMMS_STATUS_BIT_BOIL_TEMP_VALID           (1U << 1)
#define COMMS_STATUS_BIT_PRESSURE_VALID            (1U << 2)
#define COMMS_STATUS_BIT_HEARTBEAT_ALIVE           (1U << 3)


typedef enum
{
    COMMS_RX_STATE_SYNC1 = 0,
    COMMS_RX_STATE_SYNC2,
    COMMS_RX_STATE_TYPE,
    COMMS_RX_STATE_SEQ,
    COMMS_RX_STATE_LEN,
    COMMS_RX_STATE_DATA,
    COMMS_RX_STATE_CRC
} comms_rx_state_t;

typedef struct
{
    comms_rx_state_t state;
    uint8_t type;
    uint8_t seq;
    uint8_t length;
    uint8_t index;
    uint8_t data[COMMS_FRAME_MAX_DATA_LENGTH];
} comms_rx_runtime_t;


typedef struct
{
    uint8_t mash_target_c;
    uint8_t boil_target_c;
    int16_t mash_temp_c_x10;
    int16_t boil_temp_c_x10;
    uint8_t mash_pump_setpoint;
    uint8_t boil_pump_setpoint;
    uint8_t mash_pump_running;
    uint8_t boil_pump_running;
    uint16_t pressure_count;
    uint8_t solenoid_state_bits;
    uint8_t status_bits;
    uint16_t fault_flags;
    uint8_t valve_state[11];
} comms_status_report_payload_t;


typedef struct
{
    uint16_t active_fault_flags;
    uint16_t latched_fault_flags;
    uint8_t primary_reason;
} comms_fault_report_payload_t;


// ======================================================================================
//  PROTOTYPES
// ======================================================================================

static void comms_rx_reset();
static void comms_process_rx_byte(uint8_t byte);
static void comms_handle_frame(uint8_t type, uint8_t seq, const uint8_t *data, uint8_t length);
static void comms_handle_heartbeat(uint8_t seq, const uint8_t *data, uint8_t length);
static void comms_handle_control_snapshot(uint8_t seq, const uint8_t *data, uint8_t length);
static void comms_handle_shutdown_request(uint8_t seq, const uint8_t *data, uint8_t length);
static void comms_handle_fault_clear_request(uint8_t seq, const uint8_t *data, uint8_t length);
static bool comms_control_snapshot_is_valid(const supervisor_control_snapshot_t *snapshot);
static bool comms_write_frame(uint8_t type, uint8_t seq, const uint8_t *data, uint8_t length);
static bool comms_send_ack(uint8_t acked_type, uint8_t acked_seq);
static bool comms_send_nack(uint8_t rejected_type, uint8_t rejected_seq, comms_nack_reason_t reason);
static bool comms_send_status_report();
static bool comms_send_fault_report();
static uint8_t comms_allocate_tx_seq();
static bool comms_reporting_enabled();
static uint8_t comms_build_solenoid_state_bits();
static void comms_build_status_report_payload(comms_status_report_payload_t *payload);
static void comms_build_fault_report_payload(comms_fault_report_payload_t *payload);
static uint16_t comms_encode_u16(uint8_t *buffer, uint16_t value);
static uint16_t comms_encode_i16(uint8_t *buffer, int16_t value);


// ======================================================================================
//  GLOBALS
// ======================================================================================

comms_runtime_t comms_runtime =
{
    .heartbeat_seen = false,
    .heartbeat_alive = false,
    .shutdown_request_pending = false,
    .fault_clear_request_pending = false,
    .control_snapshot_pending = false,
    .last_rx_seq = 0U,
    .next_tx_seq = 1U,
    .last_heartbeat_time_ms = 0UL,
    .next_status_report_time_ms = COMMS_STATUS_REPORT_PERIOD_MS,
    .pending_control_snapshot = { 0U, 0U, 0U, 0U, 0U, { 0U } }
};

static comms_rx_runtime_t comms_rx_runtime =
{
    .state = COMMS_RX_STATE_SYNC1,
    .type = 0U,
    .seq = 0U,
    .length = 0U,
    .index = 0U,
    .data = { 0U }
};


// ======================================================================================
//  API FUNCTIONS
// ======================================================================================

/****************************************************************************************
 * @brief Initialize the SOM↔MCU communications runtime.
 *
 * The comms layer owns framed protocol traffic only. It does not directly poke drivers;
 * instead it translates valid incoming frames into compact supervisor-facing facts.
 ****************************************************************************************/
void comms_init()
{
    comms_runtime.heartbeat_seen = false;
    comms_runtime.heartbeat_alive = false;
    comms_runtime.shutdown_request_pending = false;
    comms_runtime.fault_clear_request_pending = false;
    comms_runtime.control_snapshot_pending = false;
    comms_runtime.last_rx_seq = 0U;
    comms_runtime.next_tx_seq = 1U;
    comms_runtime.last_heartbeat_time_ms = 0UL;
    comms_runtime.next_status_report_time_ms = COMMS_STATUS_REPORT_PERIOD_MS;
    comms_runtime.pending_control_snapshot = (supervisor_control_snapshot_t){ 0U, 0U, 0U, 0U, 0U, { 0U } };

    comms_rx_reset();
}

/****************************************************************************************
 * @brief Run one non-blocking communications service pass.
 *
 * Work performed here:
 * - consume available UART bytes into the frame parser
 * - maintain heartbeat liveness state
 * - schedule periodic status reporting once the SOM is active
 * - emit one-shot fault reports when the central fault picture changes
 *
 * The function deliberately stays non-blocking so it can live in the main outer service
 * loop without stretching response times for the rest of the firmware.
 ****************************************************************************************/
void comms_update()
{
    uint8_t byte;

    while (uart_read_byte(&byte))
    {
        comms_process_rx_byte(byte);
    }

    if (comms_runtime.heartbeat_seen &&
        ((runtime_time_ms - comms_runtime.last_heartbeat_time_ms) >= COMMS_HEARTBEAT_TIMEOUT_MS))
    {
        comms_runtime.heartbeat_alive = false;
    }

    if (comms_reporting_enabled() &&
        ((int32_t)(runtime_time_ms - comms_runtime.next_status_report_time_ms) >= 0))
    {
        if (comms_send_status_report())
        {
            comms_runtime.next_status_report_time_ms = runtime_time_ms + COMMS_STATUS_REPORT_PERIOD_MS;
        }
    }

    if (faults_runtime.changed && comms_reporting_enabled())
    {
        (void)comms_send_fault_report();
    }
}

/****************************************************************************************
 * @brief Hand one pending validated control snapshot to the supervisor layer.
 *
 * The comms layer only stores the newest accepted snapshot. The supervisor consumes that
 * snapshot once it is ready to fold the new target state into its control context.
 *
 * @param snapshot Output pointer receiving the pending snapshot.
 *
 * @return true when a snapshot was available and consumed.
 ****************************************************************************************/
bool comms_take_pending_control_snapshot(supervisor_control_snapshot_t *snapshot)
{
    if ((snapshot == NULL) || !comms_runtime.control_snapshot_pending)
    {
        return false;
    }

    *snapshot = comms_runtime.pending_control_snapshot;
    comms_runtime.control_snapshot_pending = false;
    return true;
}

/****************************************************************************************
 * @brief Consume a pending shutdown request published by the comms layer.
 *
 * @return true when a shutdown request was pending.
 ****************************************************************************************/
bool comms_take_shutdown_request()
{
    bool pending = comms_runtime.shutdown_request_pending;

    comms_runtime.shutdown_request_pending = false;
    return pending;
}

/****************************************************************************************
 * @brief Consume a pending fault-clear request published by the comms layer.
 *
 * @return true when a fault-clear request was pending.
 ****************************************************************************************/
bool comms_take_fault_clear_request()
{
    bool pending = comms_runtime.fault_clear_request_pending;

    comms_runtime.fault_clear_request_pending = false;
    return pending;
}


// ======================================================================================
//  INTERNAL FUNCTIONS
// ======================================================================================

/****************************************************************************************
 * @brief Reset the receive state machine to the first sync-byte search state.
 ****************************************************************************************/
static void comms_rx_reset()
{
    comms_rx_runtime.state = COMMS_RX_STATE_SYNC1;
    comms_rx_runtime.type = 0U;
    comms_rx_runtime.seq = 0U;
    comms_rx_runtime.length = 0U;
    comms_rx_runtime.index = 0U;
}

/****************************************************************************************
 * @brief Feed one UART byte into the framed protocol parser.
 *
 * The parser is intentionally simple and fully byte-stream oriented:
 * - lock on 0xAA 0x55
 * - collect the fixed header fields
 * - read the payload length that follows
 * - validate CRC over TYPE/SEQ/LEN/DATA
 * - hand the completed frame to the protocol dispatcher
 ****************************************************************************************/
static void comms_process_rx_byte(uint8_t byte)
{
    uint8_t crc;
    uint8_t crc_index;

    switch (comms_rx_runtime.state)
    {
        case COMMS_RX_STATE_SYNC1:
            if (byte == COMMS_FRAME_SYNC1)
            {
                comms_rx_runtime.state = COMMS_RX_STATE_SYNC2;
            }
            break;

        case COMMS_RX_STATE_SYNC2:
            if (byte == COMMS_FRAME_SYNC2)
            {
                comms_rx_runtime.state = COMMS_RX_STATE_TYPE;
            }
            else if (byte != COMMS_FRAME_SYNC1)
            {
                comms_rx_reset();
            }
            break;

        case COMMS_RX_STATE_TYPE:
            comms_rx_runtime.type = byte;
            comms_rx_runtime.state = COMMS_RX_STATE_SEQ;
            break;

        case COMMS_RX_STATE_SEQ:
            comms_rx_runtime.seq = byte;
            comms_rx_runtime.state = COMMS_RX_STATE_LEN;
            break;

        case COMMS_RX_STATE_LEN:
            comms_rx_runtime.length = byte;
            comms_rx_runtime.index = 0U;

            if (comms_rx_runtime.length > COMMS_FRAME_MAX_DATA_LENGTH)
            {
                (void)comms_send_nack(comms_rx_runtime.type,
                                      comms_rx_runtime.seq,
                                      COMMS_NACK_REASON_BAD_LENGTH);
                comms_rx_reset();
            }
            else if (comms_rx_runtime.length == 0U)
            {
                comms_rx_runtime.state = COMMS_RX_STATE_CRC;
            }
            else
            {
                comms_rx_runtime.state = COMMS_RX_STATE_DATA;
            }
            break;

        case COMMS_RX_STATE_DATA:
            comms_rx_runtime.data[comms_rx_runtime.index] = byte;
            comms_rx_runtime.index++;

            if (comms_rx_runtime.index >= comms_rx_runtime.length)
            {
                comms_rx_runtime.state = COMMS_RX_STATE_CRC;
            }
            break;

        case COMMS_RX_STATE_CRC:
            crc = CRC8_Init();
            crc = CRC8_Update(crc, comms_rx_runtime.type);
            crc = CRC8_Update(crc, comms_rx_runtime.seq);
            crc = CRC8_Update(crc, comms_rx_runtime.length);

            for (crc_index = 0U; crc_index < comms_rx_runtime.length; crc_index++)
            {
                crc = CRC8_Update(crc, comms_rx_runtime.data[crc_index]);
            }

            if (crc == byte)
            {
                comms_handle_frame(comms_rx_runtime.type,
                                   comms_rx_runtime.seq,
                                   comms_rx_runtime.data,
                                   comms_rx_runtime.length);
            }

            comms_rx_reset();
            break;

        default:
            comms_rx_reset();
            break;
    }
}

/****************************************************************************************
 * @brief Dispatch a completed validated frame to the appropriate message handler.
 *
 * CRC validation has already succeeded before this dispatcher is entered. The remaining
 * checks are semantic ones: known type, valid length, valid payload, and current-state
 * acceptance rules.
 ****************************************************************************************/
static void comms_handle_frame(uint8_t type, uint8_t seq, const uint8_t *data, uint8_t length)
{
    comms_runtime.last_rx_seq = seq;

    switch (type)
    {
        case COMMS_TYPE_HEARTBEAT:
            comms_handle_heartbeat(seq, data, length);
            break;

        case COMMS_TYPE_CONTROL_SNAPSHOT:
            comms_handle_control_snapshot(seq, data, length);
            break;

        case COMMS_TYPE_SHUTDOWN_REQUEST:
            comms_handle_shutdown_request(seq, data, length);
            break;

        case COMMS_TYPE_FAULT_CLEAR_REQUEST:
            comms_handle_fault_clear_request(seq, data, length);
            break;

        default:
            (void)comms_send_nack(type, seq, COMMS_NACK_REASON_UNKNOWN_TYPE);
            break;
    }
}

/****************************************************************************************
 * @brief Handle an incoming heartbeat frame.
 *
 * Heartbeat frames have no payload. Their sole purpose is to prove that the SOM is still
 * alive and that framed traffic is now actually running.
 ****************************************************************************************/
static void comms_handle_heartbeat(uint8_t seq, const uint8_t *data, uint8_t length)
{
    (void)seq;
    (void)data;

    if (length != 0U)
    {
        (void)comms_send_nack(COMMS_TYPE_HEARTBEAT, seq, COMMS_NACK_REASON_BAD_LENGTH);
        return;
    }

    comms_runtime.heartbeat_seen = true;
    comms_runtime.heartbeat_alive = true;
    comms_runtime.last_heartbeat_time_ms = runtime_time_ms;
}

/****************************************************************************************
 * @brief Handle an incoming full control snapshot.
 *
 * The first protocol path treats the snapshot as a compact target-state image. The
 * comms layer validates framing and basic payload legality, then stages the snapshot for
 * supervisor consumption.
 ****************************************************************************************/
static void comms_handle_control_snapshot(uint8_t seq, const uint8_t *data, uint8_t length)
{
    supervisor_control_snapshot_t snapshot;
    uint8_t valve_index;

    if (length != 16U)
    {
        (void)comms_send_nack(COMMS_TYPE_CONTROL_SNAPSHOT, seq, COMMS_NACK_REASON_BAD_LENGTH);
        return;
    }

    if (supervisor_runtime.state != SUPERVISOR_STATE_ACTIVE)
    {
        (void)comms_send_nack(COMMS_TYPE_CONTROL_SNAPSHOT, seq, COMMS_NACK_REASON_INVALID_STATE);
        return;
    }

    snapshot.mash_target_c = data[0];
    snapshot.boil_target_c = data[1];
    snapshot.mash_pump_setpoint = data[2];
    snapshot.boil_pump_setpoint = data[3];
    snapshot.solenoid_state_bits = data[4];

    for (valve_index = 0U; valve_index < 11U; valve_index++)
    {
        snapshot.valve_command[valve_index] = data[5U + valve_index];
    }

    if (!comms_control_snapshot_is_valid(&snapshot))
    {
        (void)comms_send_nack(COMMS_TYPE_CONTROL_SNAPSHOT, seq, COMMS_NACK_REASON_BAD_PAYLOAD);
        return;
    }

    comms_runtime.pending_control_snapshot = snapshot;
    comms_runtime.control_snapshot_pending = true;

    (void)comms_send_ack(COMMS_TYPE_CONTROL_SNAPSHOT, seq);
}

/****************************************************************************************
 * @brief Handle an incoming shutdown request.
 ****************************************************************************************/
static void comms_handle_shutdown_request(uint8_t seq, const uint8_t *data, uint8_t length)
{
    (void)data;

    if (length != 0U)
    {
        (void)comms_send_nack(COMMS_TYPE_SHUTDOWN_REQUEST, seq, COMMS_NACK_REASON_BAD_LENGTH);
        return;
    }

    if (supervisor_runtime.state == SUPERVISOR_STATE_SHUTDOWN)
    {
        (void)comms_send_nack(COMMS_TYPE_SHUTDOWN_REQUEST, seq, COMMS_NACK_REASON_INVALID_STATE);
        return;
    }

    comms_runtime.shutdown_request_pending = true;
    (void)comms_send_ack(COMMS_TYPE_SHUTDOWN_REQUEST, seq);
}

/****************************************************************************************
 * @brief Handle an incoming fault-clear request.
 ****************************************************************************************/
static void comms_handle_fault_clear_request(uint8_t seq, const uint8_t *data, uint8_t length)
{
    (void)data;

    if (length != 0U)
    {
        (void)comms_send_nack(COMMS_TYPE_FAULT_CLEAR_REQUEST, seq, COMMS_NACK_REASON_BAD_LENGTH);
        return;
    }

    comms_runtime.fault_clear_request_pending = true;
    (void)comms_send_ack(COMMS_TYPE_FAULT_CLEAR_REQUEST, seq);
}

/****************************************************************************************
 * @brief Validate the semantic contents of one parsed control snapshot.
 *
 * This is intentionally limited to what the comms layer can judge locally:
 * - solenoid bitfield must not contain unknown bits
 * - valve command values must map to known valve positions
 *
 * Higher-level acceptance rules remain a supervisor concern.
 ****************************************************************************************/
static bool comms_control_snapshot_is_valid(const supervisor_control_snapshot_t *snapshot)
{
    uint8_t valve_index;

    if (snapshot == NULL)
    {
        return false;
    }

    if ((snapshot->solenoid_state_bits & (uint8_t)~(COMMS_SOLENOID_BIT_BREW_INLET | COMMS_SOLENOID_BIT_COOLING_INLET)) != 0U)
    {
        return false;
    }

    for (valve_index = 0U; valve_index < 11U; valve_index++)
    {
        if (snapshot->valve_command[valve_index] >= (uint8_t)VALVE_POSITION_COUNT)
        {
            return false;
        }
    }

    return true;
}

/****************************************************************************************
 * @brief Serialize and queue one outbound protocol frame.
 *
 * The frame is built into a temporary stack buffer and queued to the UART software TX
 * ring as a single write. If the UART buffer is too full, the caller simply retries on a
 * later service pass.
 ****************************************************************************************/
static bool comms_write_frame(uint8_t type, uint8_t seq, const uint8_t *data, uint8_t length)
{
    uint8_t frame[2U + 3U + COMMS_FRAME_MAX_DATA_LENGTH + 1U];
    uint8_t crc = CRC8_Init();
    uint8_t index = 0U;
    uint8_t payload_index;

    if (length > COMMS_FRAME_MAX_DATA_LENGTH)
    {
        return false;
    }

    frame[index++] = COMMS_FRAME_SYNC1;
    frame[index++] = COMMS_FRAME_SYNC2;
    frame[index++] = type;
    frame[index++] = seq;
    frame[index++] = length;

    crc = CRC8_Update(crc, type);
    crc = CRC8_Update(crc, seq);
    crc = CRC8_Update(crc, length);

    for (payload_index = 0U; payload_index < length; payload_index++)
    {
        frame[index++] = data[payload_index];
        crc = CRC8_Update(crc, data[payload_index]);
    }

    frame[index++] = crc;
    return uart_write(frame, index);
}

/****************************************************************************************
 * @brief Send an ACK frame that references one accepted inbound message.
 ****************************************************************************************/
static bool comms_send_ack(uint8_t acked_type, uint8_t acked_seq)
{
    uint8_t payload[2];

    payload[0] = acked_type;
    payload[1] = acked_seq;

    return comms_write_frame(COMMS_TYPE_ACK,
                             comms_allocate_tx_seq(),
                             payload,
                             (uint8_t)sizeof(payload));
}

/****************************************************************************************
 * @brief Send a NACK frame that references one rejected inbound message.
 ****************************************************************************************/
static bool comms_send_nack(uint8_t rejected_type, uint8_t rejected_seq, comms_nack_reason_t reason)
{
    uint8_t payload[3];

    payload[0] = rejected_type;
    payload[1] = rejected_seq;
    payload[2] = (uint8_t)reason;

    return comms_write_frame(COMMS_TYPE_NACK,
                             comms_allocate_tx_seq(),
                             payload,
                             (uint8_t)sizeof(payload));
}

/****************************************************************************************
 * @brief Build and send one status-report frame.
 *
 * The first status report keeps the payload compact but still genuinely useful: accepted
 * targets, pump state, pressure count, solenoid states, valve states, and the central
 * fault bitmap. Temperature fields are already reserved in the payload but remain marked
 * invalid until temperature refresh is moved into a non-blocking timed-service cache.
 ****************************************************************************************/
static bool comms_send_status_report()
{
    comms_status_report_payload_t payload;
    uint8_t encoded[32];
    uint8_t index = 0U;
    uint8_t valve_index;

    comms_build_status_report_payload(&payload);

    encoded[index++] = payload.mash_target_c;
    encoded[index++] = payload.boil_target_c;
    index = (uint8_t)(index + comms_encode_i16(&encoded[index], payload.mash_temp_c_x10));
    index = (uint8_t)(index + comms_encode_i16(&encoded[index], payload.boil_temp_c_x10));
    encoded[index++] = payload.mash_pump_setpoint;
    encoded[index++] = payload.boil_pump_setpoint;
    encoded[index++] = payload.mash_pump_running;
    encoded[index++] = payload.boil_pump_running;
    index = (uint8_t)(index + comms_encode_u16(&encoded[index], payload.pressure_count));
    encoded[index++] = payload.solenoid_state_bits;
    encoded[index++] = payload.status_bits;
    index = (uint8_t)(index + comms_encode_u16(&encoded[index], payload.fault_flags));

    for (valve_index = 0U; valve_index < 11U; valve_index++)
    {
        encoded[index++] = payload.valve_state[valve_index];
    }

    return comms_write_frame(COMMS_TYPE_STATUS_REPORT,
                             comms_allocate_tx_seq(),
                             encoded,
                             index);
}

/****************************************************************************************
 * @brief Build and send one fault-report frame when the fault picture changes.
 ****************************************************************************************/
static bool comms_send_fault_report()
{
    comms_fault_report_payload_t payload;
    uint8_t encoded[5];
    uint8_t index = 0U;

    comms_build_fault_report_payload(&payload);

    index = (uint8_t)(index + comms_encode_u16(&encoded[index], payload.active_fault_flags));
    index = (uint8_t)(index + comms_encode_u16(&encoded[index], payload.latched_fault_flags));
    encoded[index++] = payload.primary_reason;

    return comms_write_frame(COMMS_TYPE_FAULT_REPORT,
                             comms_allocate_tx_seq(),
                             encoded,
                             index);
}

/****************************************************************************************
 * @brief Allocate the next rolling transmit SEQ value.
 ****************************************************************************************/
static uint8_t comms_allocate_tx_seq()
{
    uint8_t seq = comms_runtime.next_tx_seq;

    comms_runtime.next_tx_seq++;
    if (comms_runtime.next_tx_seq == 0U)
    {
        comms_runtime.next_tx_seq = 1U;
    }

    if (seq == 0U)
    {
        seq = 1U;
        comms_runtime.next_tx_seq = 2U;
    }

    return seq;
}

/****************************************************************************************
 * @brief Tell whether periodic reporting should currently be active.
 ****************************************************************************************/
static bool comms_reporting_enabled()
{
    return ((supervisor_runtime.state == SUPERVISOR_STATE_ACTIVE) ||
            (supervisor_runtime.state == SUPERVISOR_STATE_FAULT) ||
            (supervisor_runtime.state == SUPERVISOR_STATE_SHUTDOWN));
}

/****************************************************************************************
 * @brief Build the outbound two-bit solenoid state image.
 ****************************************************************************************/
static uint8_t comms_build_solenoid_state_bits()
{
    bool enabled = false;
    uint8_t bits = 0U;

    if (solenoids_get(SOLENOID_ID_BREW_INLET, &enabled) && enabled)
    {
        bits |= COMMS_SOLENOID_BIT_BREW_INLET;
    }

    if (solenoids_get(SOLENOID_ID_COOLING_INLET, &enabled) && enabled)
    {
        bits |= COMMS_SOLENOID_BIT_COOLING_INLET;
    }

    return bits;
}

/****************************************************************************************
 * @brief Collect the latest machine facts into one status-report payload.
 ****************************************************************************************/
static void comms_build_status_report_payload(comms_status_report_payload_t *payload)
{
    pressure_reading_t pressure_reading;
    pump_status_t mash_pump_status;
    pump_status_t boil_pump_status;
    uint8_t valve_index;

    *payload = (comms_status_report_payload_t){ 0U, 0U, 0, 0, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, { 0U } };

    payload->mash_target_c = supervisor_runtime.accepted_snapshot.mash_target_c;
    payload->boil_target_c = supervisor_runtime.accepted_snapshot.boil_target_c;
    payload->mash_pump_setpoint = supervisor_runtime.accepted_snapshot.mash_pump_setpoint;
    payload->boil_pump_setpoint = supervisor_runtime.accepted_snapshot.boil_pump_setpoint;
    payload->solenoid_state_bits = comms_build_solenoid_state_bits();
    payload->fault_flags = faults_runtime.active_flags;
    /*
     * Temperature.c currently performs blocking 1-Wire conversions. Do not call it from
     * comms_update(), which must stay responsive and non-blocking. The first protocol
     * path therefore leaves the temperature-valid bits clear until temperature refresh is
     * moved into a timed-service cache later.
     */

    if ((pressure_read_processed(&pressure_reading) == TWI_STATUS_OK) && pressure_reading.valid)
    {
        payload->pressure_count = pressure_reading.pressure_count;
        payload->status_bits |= COMMS_STATUS_BIT_PRESSURE_VALID;
    }

    if (comms_runtime.heartbeat_alive)
    {
        payload->status_bits |= COMMS_STATUS_BIT_HEARTBEAT_ALIVE;
    }

    if (pumps_get_status(PUMP_ID_MASH, &mash_pump_status))
    {
        payload->mash_pump_running = mash_pump_status.running ? 1U : 0U;
    }

    if (pumps_get_status(PUMP_ID_BOIL, &boil_pump_status))
    {
        payload->boil_pump_running = boil_pump_status.running ? 1U : 0U;
    }

    for (valve_index = 0U; valve_index < 11U; valve_index++)
    {
        payload->valve_state[valve_index] = (uint8_t)valves_get_last_position((valve_id_t)(valve_index + 1U));
    }
}

/****************************************************************************************
 * @brief Collect the current central fault picture into one fault-report payload.
 ****************************************************************************************/
static void comms_build_fault_report_payload(comms_fault_report_payload_t *payload)
{
    payload->active_fault_flags = faults_runtime.active_flags;
    payload->latched_fault_flags = faults_runtime.latched_flags;
    payload->primary_reason = (uint8_t)faults_runtime.primary_reason;
}

/****************************************************************************************
 * @brief Encode an unsigned 16-bit value into little-endian wire order.
 *
 * @return Number of bytes written.
 ****************************************************************************************/
static uint16_t comms_encode_u16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
    return 2U;
}

/****************************************************************************************
 * @brief Encode a signed 16-bit value into little-endian wire order.
 *
 * @return Number of bytes written.
 ****************************************************************************************/
static uint16_t comms_encode_i16(uint8_t *buffer, int16_t value)
{
    return comms_encode_u16(buffer, (uint16_t)value);
}
