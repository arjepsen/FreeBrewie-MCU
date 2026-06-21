#include "TWI.h"

#include <avr/io.h>
#include <stdbool.h>
#include <stdint.h>
//#include <stdlib.h>
#include <stddef.h>

// ======================================================================================
//  MACROS
// ======================================================================================

#define TWI_STATUS_MASK 0xF8U

#define TWI_STATE_START_TRANSMITTED 0x08U
#define TWI_STATE_REPEATED_START_TRANSMITTED 0x10U
#define TWI_STATE_SLA_W_ACK 0x18U
#define TWI_STATE_SLA_W_NACK 0x20U
#define TWI_STATE_DATA_TX_ACK 0x28U
#define TWI_STATE_DATA_TX_NACK 0x30U
#define TWI_STATE_ARBITRATION_LOST 0x38U
#define TWI_STATE_SLA_R_ACK 0x40U
#define TWI_STATE_SLA_R_NACK 0x48U
#define TWI_STATE_DATA_RX_ACK 0x50U
#define TWI_STATE_DATA_RX_NACK 0x58U
#define TWI_STATE_BUS_ERROR 0x00U

// TWCR base value: clear TWINT flag and keep the peripheral enabled.
// Named so that every register write is self-documenting.
#define TWI_TWCR_CLEAR_TWINT ((1U << TWINT) | (1U << TWEN))

// Compute TWBR at compile time (prescaler = 1, so TWPS bits remain 0).
// AVR formula: SCL = F_CPU / (16 + 2 * TWBR * prescaler)
// => TWBR = (F_CPU / SCL - 16) / 2
#define TWI_TWBR_VALUE ((uint8_t)(((F_CPU / TWI_SCL_FREQUENCY_HZ) - 16UL) / 2UL))

// ======================================================================================
//  PROTOTYPES
// ======================================================================================

static __attribute__((always_inline)) inline twi_status_t twi_start();
static __attribute__((always_inline)) inline twi_status_t twi_repeated_start();
static __attribute__((always_inline)) inline void twi_stop();
static __attribute__((always_inline)) inline twi_status_t twi_send_sla_w(uint8_t address_7bit);
static __attribute__((always_inline)) inline twi_status_t twi_send_sla_r(uint8_t address_7bit);
static __attribute__((always_inline)) inline twi_status_t twi_write_byte(uint8_t value);
static __attribute__((always_inline)) inline twi_status_t twi_read_byte_ack(uint8_t *value);
static __attribute__((always_inline)) inline twi_status_t twi_read_byte_nack(uint8_t *value);

static __attribute__((always_inline)) inline bool twi_wait_for_twint();
// static uint8_t twi_get_state();
// static uint8_t twi_calculate_twbr(uint32_t scl_frequency_hz);

static twi_status_t twi_write_buffer(const uint8_t *data, uint8_t length);
static twi_status_t twi_read_buffer(uint8_t *data, uint8_t length);

// ======================================================================================
//  IMPLEMENTATIONS
// ======================================================================================

/****************************************************************************************
 * @brief Initialize the AVR hardware TWI peripheral.
 *
 * This driver uses the ATmega2560 hardware TWI block in simple polling-based master mode.
 * The prescaler is fixed to 1, and the bit-rate register is derived from
 * TWI_SCL_FREQUENCY_HZ.
 *
 * The public API is intentionally kept small:
 * - address probe
 * - write transaction
 * - read transaction
 * - write-then-read transaction
 *
 * No interrupt mode, slave mode, or buffering is included here.
 *
 * @return None.
 ****************************************************************************************/
void twi_init()
{
    TWCR = 0U;
    TWSR = 0U;
    TWBR = TWI_TWBR_VALUE;
    TWCR = (1U << TWEN);
}

/****************************************************************************************
 * @brief Probe a 7-bit slave address for presence.
 *
 * Sends: START -> SLA+W -> STOP
 *
 * A NACK on the address is not an error; it means the device is absent.
 * *present is set accordingly and TWI_STATUS_OK is returned in both cases.
 *
 * @param address_7bit  7-bit slave address.
 * @param present       Pointer receiving presence result.
 * @return TWI status code.
 ****************************************************************************************/
twi_status_t twi_probe(uint8_t address_7bit, bool *present)
{
    // Ensure correct pointer.
    if (present == NULL)
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    twi_status_t status = twi_start();
    status = (status == TWI_STATUS_OK) ? twi_send_sla_w(address_7bit) : status;

    twi_stop();
    *present = (status == TWI_STATUS_OK);
    status = (status == TWI_STATUS_SLA_W_NACK) ? TWI_STATUS_OK : status;

    return status;
}

/****************************************************************************************
 * @brief Write one or more bytes to a slave.
 *
 * Sends: START -> SLA+W -> data bytes -> STOP
 *
 * A zero-length write is valid (START/SLA+W/STOP only).
 *
 * @param address_7bit  7-bit slave address.
 * @param data          Pointer to bytes to transmit (may be NULL when length == 0).
 * @param length        Number of bytes to transmit.
 * @return TWI status code.
 ****************************************************************************************/
twi_status_t twi_write(uint8_t address_7bit, const uint8_t *data, uint8_t length)
{
    if ((length > 0U) && (data == NULL))
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    twi_status_t status = twi_start();

    if (status == TWI_STATUS_OK)
    {
        status = twi_send_sla_w(address_7bit);

        if (status == TWI_STATUS_OK)
        {
            status = twi_write_buffer(data, length);
        }
    }

    twi_stop();
    return status;
}

/****************************************************************************************
 * @brief Read one or more bytes from a slave.
 *
 * This sends:
 * START -> SLA+R -> data bytes -> STOP
 *
 * @param address_7bit 7-bit slave address.
 * @param data Pointer to receive buffer.
 * @param length Number of bytes to receive.
 *
 * @return TWI status code.
 ****************************************************************************************/
twi_status_t twi_read(uint8_t address_7bit, uint8_t *data, uint8_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    twi_status_t status = twi_start();

    if (status == TWI_STATUS_OK)
    {
        status = twi_send_sla_r(address_7bit);

        if (status == TWI_STATUS_OK)
        {
            status = twi_read_buffer(data, length);
        }
    }

    twi_stop();
    return status;
}

/****************************************************************************************
 * @brief Write bytes to a slave, then read bytes back using a repeated START.
 *
 * This sends:
 * START -> SLA+W -> tx bytes -> REPEATED START -> SLA+R -> rx bytes -> STOP
 *
 * This is the normal register-read transaction used by many I2C devices.
 *
 * @param address_7bit 7-bit slave address.
 * @param tx_data Pointer to bytes written before the read phase.
 * @param tx_length Number of bytes written before the read phase.
 * @param rx_data Pointer to receive buffer.
 * @param rx_length Number of bytes to receive.
 *
 * @return TWI status code.
 ****************************************************************************************/
twi_status_t twi_write_read(uint8_t address_7bit,
                            const uint8_t *tx_data,
                            uint8_t tx_length,
                            uint8_t *rx_data,
                            uint8_t rx_length)
{
    twi_status_t status = TWI_STATUS_OK;

    if (((tx_length > 0U) && (tx_data == NULL)) ||
        ((rx_length > 0U) && (rx_data == NULL)) ||
        (rx_length == 0U)) // often people allow rx_length==0, but you don't — keep your rule
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    status = twi_start();
    if (status != TWI_STATUS_OK) goto cleanup;

    status = twi_send_sla_w(address_7bit);
    if (status != TWI_STATUS_OK) goto cleanup;

    status = twi_write_buffer(tx_data, tx_length);
    if (status != TWI_STATUS_OK) goto cleanup;

    status = twi_repeated_start();
    if (status != TWI_STATUS_OK) goto cleanup;

    status = twi_send_sla_r(address_7bit);
    if (status != TWI_STATUS_OK) goto cleanup;

    status = twi_read_buffer(rx_data, rx_length);

cleanup:
    twi_stop();
    return status;
}

// ======================================================================================
//  PRIVATE FUNCTIONS
// ======================================================================================

/****************************************************************************************
 * @brief Send a START condition.
 *
 * @return TWI status code.
 ****************************************************************************************/
static __attribute__((always_inline)) inline twi_status_t twi_start()
{
    TWCR = (1U << TWINT) | (1U << TWSTA) | (1U << TWEN);

    if (!twi_wait_for_twint())
    {
        return TWI_STATUS_TIMEOUT;
    }

    uint8_t state = (uint8_t)(TWSR & TWI_STATUS_MASK);

    if (state == TWI_STATE_START_TRANSMITTED)
    {
        return TWI_STATUS_OK;
    }

    if (state == TWI_STATE_ARBITRATION_LOST)
    {
        return TWI_STATUS_ARBITRATION_LOST;
    }

    if (state == TWI_STATE_BUS_ERROR)
    {
        return TWI_STATUS_BUS_ERROR;
    }

    return TWI_STATUS_START_FAILED;
}

/****************************************************************************************
 * @brief Send a repeated START condition.
 *
 * @return TWI status code.
 ****************************************************************************************/
static __attribute__((always_inline)) inline twi_status_t twi_repeated_start()
{
    TWCR = (1U << TWINT) | (1U << TWSTA) | (1U << TWEN);

    if (!twi_wait_for_twint())
    {
        return TWI_STATUS_TIMEOUT;
    }

    uint8_t state = (uint8_t)(TWSR & TWI_STATUS_MASK);

    if (state == TWI_STATE_REPEATED_START_TRANSMITTED)
    {
        return TWI_STATUS_OK;
    }

    if (state == TWI_STATE_ARBITRATION_LOST)
    {
        return TWI_STATUS_ARBITRATION_LOST;
    }

    if (state == TWI_STATE_BUS_ERROR)
    {
        return TWI_STATUS_BUS_ERROR;
    }

    return TWI_STATUS_REPEATED_START_FAILED;
}

/****************************************************************************************
 * @brief Send a STOP condition and wait for the bus to release.
 *
 * TWSTO clears itself in hardware when the STOP has been transmitted. Waiting for that
 * makes back-to-back transactions deterministic.  uint16_t counter for the same reason
 * as in twi_wait_for_twint.
 ****************************************************************************************/
static __attribute__((always_inline)) inline void twi_stop(void)
{
    uint16_t timeout = TWI_TIMEOUT_ITERATIONS;
 
    TWCR = (1U << TWINT) | (1U << TWSTO) | (1U << TWEN);
 
    while ((TWCR & (1U << TWSTO)) && timeout--)
    {
        __asm__ __volatile__("");  // Compiler barrier — do not remove
    }
}

/****************************************************************************************
 * @brief Send slave address with write bit.
 *
 * @param address_7bit 7-bit slave address.
 *
 * @return TWI status code.
 ****************************************************************************************/
static __attribute__((always_inline)) inline twi_status_t twi_send_sla_w(uint8_t address_7bit)
{
    TWDR = (uint8_t)(address_7bit << 1);
    TWCR = TWI_TWCR_CLEAR_TWINT;

    if (!twi_wait_for_twint())
    {
        return TWI_STATUS_TIMEOUT;
    }

    uint8_t state = (uint8_t)(TWSR & TWI_STATUS_MASK);

    if (state == TWI_STATE_SLA_W_ACK)
    {
        return TWI_STATUS_OK;
    }

    if (state == TWI_STATE_SLA_W_NACK)
    {
        return TWI_STATUS_SLA_W_NACK;
    }

    if (state == TWI_STATE_ARBITRATION_LOST)
    {
        return TWI_STATUS_ARBITRATION_LOST;
    }

    if (state == TWI_STATE_BUS_ERROR)
    {
        return TWI_STATUS_BUS_ERROR;
    }

    return TWI_STATUS_UNEXPECTED_STATE;
}

/****************************************************************************************
 * @brief Send slave address with read bit.
 *
 * @param address_7bit 7-bit slave address.
 *
 * @return TWI status code.
 ****************************************************************************************/
static __attribute__((always_inline)) inline twi_status_t twi_send_sla_r(uint8_t address_7bit)
{
    TWDR = (uint8_t)((address_7bit << 1) | 0x01U);
    TWCR = TWI_TWCR_CLEAR_TWINT;

    if (!twi_wait_for_twint())
    {
        return TWI_STATUS_TIMEOUT;
    }

    uint8_t state = (uint8_t)(TWSR & TWI_STATUS_MASK);

    if (state == TWI_STATE_SLA_R_ACK)
    {
        return TWI_STATUS_OK;
    }

    if (state == TWI_STATE_SLA_R_NACK)
    {
        return TWI_STATUS_SLA_R_NACK;
    }

    if (state == TWI_STATE_ARBITRATION_LOST)
    {
        return TWI_STATUS_ARBITRATION_LOST;
    }

    if (state == TWI_STATE_BUS_ERROR)
    {
        return TWI_STATUS_BUS_ERROR;
    }

    return TWI_STATUS_UNEXPECTED_STATE;
}

/****************************************************************************************
 * @brief Write one data byte.
 *
 * @param value Byte to transmit.
 *
 * @return TWI status code.
 ****************************************************************************************/
static __attribute__((always_inline)) inline twi_status_t twi_write_byte(uint8_t value)
{
    TWDR = value;
    TWCR = TWI_TWCR_CLEAR_TWINT;

    if (!twi_wait_for_twint())
    {
        return TWI_STATUS_TIMEOUT;
    }

    uint8_t state = (uint8_t)(TWSR & TWI_STATUS_MASK);

    if (state == TWI_STATE_DATA_TX_ACK)
    {
        return TWI_STATUS_OK;
    }

    if (state == TWI_STATE_DATA_TX_NACK)
    {
        return TWI_STATUS_DATA_NACK;
    }

    if (state == TWI_STATE_ARBITRATION_LOST)
    {
        return TWI_STATUS_ARBITRATION_LOST;
    }

    if (state == TWI_STATE_BUS_ERROR)
    {
        return TWI_STATUS_BUS_ERROR;
    }

    return TWI_STATUS_UNEXPECTED_STATE;
}

/****************************************************************************************
 * @brief Read one byte and return ACK.
 *
 * @param value Pointer receiving the byte.
 *
 * @return TWI status code.
 ****************************************************************************************/
static __attribute__((always_inline)) inline twi_status_t twi_read_byte_ack(uint8_t *value)
{
    if (value == NULL)
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    TWCR = (1U << TWINT) | (1U << TWEN) | (1U << TWEA);

    if (!twi_wait_for_twint())
    {
        return TWI_STATUS_TIMEOUT;
    }

    uint8_t state = (uint8_t)(TWSR & TWI_STATUS_MASK);

    if (state == TWI_STATE_DATA_RX_ACK)
    {
        *value = TWDR;
        return TWI_STATUS_OK;
    }

    if (state == TWI_STATE_ARBITRATION_LOST)
    {
        return TWI_STATUS_ARBITRATION_LOST;
    }

    if (state == TWI_STATE_BUS_ERROR)
    {
        return TWI_STATUS_BUS_ERROR;
    }

    return TWI_STATUS_UNEXPECTED_STATE;
}

/****************************************************************************************
 * @brief Read one byte and return NACK.
 *
 * @param value Pointer receiving the byte.
 *
 * @return TWI status code.
 ****************************************************************************************/
static __attribute__((always_inline)) inline twi_status_t twi_read_byte_nack(uint8_t *value)
{
    if (value == NULL)
    {
        return TWI_STATUS_INVALID_ARGUMENT;
    }

    TWCR = TWI_TWCR_CLEAR_TWINT;

    if (!twi_wait_for_twint())
    {
        return TWI_STATUS_TIMEOUT;
    }

    uint8_t state = (uint8_t)(TWSR & TWI_STATUS_MASK);

    if (state == TWI_STATE_DATA_RX_NACK)
    {
        *value = TWDR;
        return TWI_STATUS_OK;
    }

    if (state == TWI_STATE_ARBITRATION_LOST)
    {
        return TWI_STATUS_ARBITRATION_LOST;
    }

    if (state == TWI_STATE_BUS_ERROR)
    {
        return TWI_STATUS_BUS_ERROR;
    }

    return TWI_STATUS_UNEXPECTED_STATE;
}

/****************************************************************************************
 * @brief Transmit a buffer of bytes.
 *
 * Uses pointer arithmetic instead of indexed access to avoid a per-iteration ADD on the
 * AVR address calculation.
 *
 * @param data    Transmit buffer (may be NULL when length == 0).
 * @param length  Number of bytes to send.
 * @return TWI status code.
 ****************************************************************************************/
static twi_status_t twi_write_buffer(const uint8_t *data, uint8_t length)
{
    while (length--)
    {
        twi_status_t status = twi_write_byte(*data++);
        if (status != TWI_STATUS_OK)
        {
            return status;
        }
    }
 
    return TWI_STATUS_OK;
}

/****************************************************************************************
 * @brief Receive a buffer of bytes.
 *
 * All but the last byte are ACKed; the final byte is NACKed to signal end-of-read to
 * the slave. The loop is split to avoid an index < (length - 1) test on every iteration.
 *
 * @param data    Receive buffer (caller guarantees non-NULL, length >= 1).
 * @param length  Number of bytes to receive.
 * @return TWI status code.
 ****************************************************************************************/
static twi_status_t twi_read_buffer(uint8_t *data, uint8_t length)
{
    twi_status_t status;
 
    // ACK all but the last byte
    while (length-- > 1U)
    {
        status = twi_read_byte_ack(data++);
        if (status != TWI_STATUS_OK)
        {
            return status;
        }
    }
 
    // NACK the final byte
    return twi_read_byte_nack(data);
}

/****************************************************************************************
 * @brief Wait until TWINT is set, with timeout protection.
 *
 * uint16_t is used for the counter instead of uint32_t: on an 8-bit AVR a 32-bit
 * decrement costs 4 register operations per loop vs. 2 for 16-bit, so this halves
 * the per-iteration overhead on the hot spin path.
 *
 * The volatile asm barrier prevents the compiler from hoisting the TWCR read out of
 * the loop body.
 *
 * @return true if TWINT set within timeout, false on timeout.
 ****************************************************************************************/
static __attribute__((always_inline)) inline bool twi_wait_for_twint(void)
{
    uint16_t timeout = TWI_TIMEOUT_ITERATIONS;
 
    while (!(TWCR & (1U << TWINT)) && timeout--)
    {
        __asm__ __volatile__("");  // compiler barrier — do not remove
    }
 
    return (TWCR & (1U << TWINT)) != 0U;
}
