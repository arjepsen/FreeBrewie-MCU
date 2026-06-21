#include "UART.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <util/atomic.h>   // AVR-libc

/* --------------------------------------------------------------------------
 * Private UART configuration
 * -------------------------------------------------------------------------- */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#define UART_BAUD_RATE 115200UL
#define UART_USE_DOUBLE_SPEED 1U

/*
 * Buffer size must be a power of 2.
 * One slot is sacrificed so full and empty can be distinguished.
 */
#define UART_BUFFER_MASK (UART_BUFFER_SIZE - 1U)

#if ((UART_BUFFER_SIZE & UART_BUFFER_MASK) != 0U)
#error UART_BUFFER_SIZE must be a power of 2
#endif

/*
 * Fixed USART selection for the SOM link.
 * Change these defines if the traced SOM UART is not USART0.
 */
#define UART_ubrrh UBRR0H
#define UART_ubrrl UBRR0L
#define UART_ucsra UCSR0A
#define UART_ucsrb UCSR0B
#define UART_ucsrc UCSR0C
#define UART_udr UDR0

#define UART_rxcie_bit RXCIE0
#define UART_txen_bit TXEN0
#define UART_rxen_bit RXEN0
#define UART_udrie_bit UDRIE0
#define UART_u2x_bit U2X0

#define UART_ucsz0_bit UCSZ00
#define UART_ucsz1_bit UCSZ01
#define UART_ucsz2_bit UCSZ02

#define UART_fe_bit FE0
#define UART_dor_bit DOR0
#define UART_upe_bit UPE0

#define UART_baud_div_normal ((F_CPU + (8UL * UART_BAUD_RATE)) / (16UL * UART_BAUD_RATE) - 1UL)
#define UART_baud_div_u2x ((F_CPU + (4UL * UART_BAUD_RATE)) / (8UL * UART_BAUD_RATE) - 1UL)

#if (UART_USE_DOUBLE_SPEED != 0U)
#define UART_ubrr_value UART_baud_div_u2x
#else
#define UART_ubrr_value UART_baud_div_normal
#endif

/* --------------------------------------------------------------------------
 * Private types and storage
 * -------------------------------------------------------------------------- */

typedef struct
{
    volatile uint8_t buffer[UART_BUFFER_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} uart_ring_buffer_t;

static uart_ring_buffer_t rx_buffer;
static uart_ring_buffer_t tx_buffer;
static volatile uint8_t uart_error_state = UART_ERROR_NONE;

/* --------------------------------------------------------------------------
 * Private helpers
 * -------------------------------------------------------------------------- */

static bool uart_rx_buffer_push(uint8_t byte)
{
    /* ISR-side push into the RX software ring buffer. */
    uint8_t head = rx_buffer.head;
    uint8_t next_head = (head + 1U) & UART_BUFFER_MASK;
    uint8_t tail = rx_buffer.tail;

    if (next_head == tail)
    {
        uart_error_state |= UART_ERROR_RX_OVERFLOW;
        return false;
    }

    rx_buffer.buffer[head] = byte;
    rx_buffer.head = next_head;
    return true;
}

/*********************************************************************
 * @brief Pop one byte from the RX ring buffer.
 *********************************************************************/
static bool uart_rx_buffer_pop(uint8_t *byte)
{
    /* Foreground-side pop from the RX software ring buffer. */
    uint8_t tail;
    uint8_t head;

    if (byte == NULL)
    {
        return false;
    }

    tail = rx_buffer.tail;
    head = rx_buffer.head;

    if (head == tail)
    {
        return false;
    }

    *byte = rx_buffer.buffer[tail];
    rx_buffer.tail = (tail + 1U) & UART_BUFFER_MASK;
    return true;
}

/*********************************************************************
 * @brief Push one byte into the TX ring buffer.
 *********************************************************************/
static bool uart_tx_buffer_push(uint8_t byte)
{
    /* Foreground-side push into the TX software ring buffer. */
    uint8_t head = tx_buffer.head;
    uint8_t next_head = (head + 1U) & UART_BUFFER_MASK;
    uint8_t tail = tx_buffer.tail;

    if (next_head == tail)
    {
        uart_error_state |= UART_ERROR_TX_OVERFLOW;
        return false;
    }

    tx_buffer.buffer[head] = byte;
    tx_buffer.head = next_head;
    return true;
}

/*********************************************************************
 * @brief Pop one byte from the TX ring buffer.
 *********************************************************************/
static inline bool uart_tx_buffer_pop(uint8_t *byte)
{
    /* ISR-side pop from the TX software ring buffer. */
    uint8_t tail;
    uint8_t head;

    if (byte == NULL)
    {
        return false;
    }

    tail = tx_buffer.tail;
    head = tx_buffer.head;

    if (head == tail)
    {
        return false;
    }

    *byte = tx_buffer.buffer[tail];
    tx_buffer.tail = (tail + 1U) & UART_BUFFER_MASK;
    return true;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void uart_init()
{
    rx_buffer.head = 0U;
    rx_buffer.tail = 0U;

    tx_buffer.head = 0U;
    tx_buffer.tail = 0U;

    uart_error_state = UART_ERROR_NONE;

#if (UART_USE_DOUBLE_SPEED != 0U)
    UART_ucsra = (1U << UART_u2x_bit);
#else
    UART_ucsra = 0U;
#endif

    UART_ubrrh = (uint8_t)((UART_ubrr_value >> 8) & 0xFFU);
    UART_ubrrl = (uint8_t)(UART_ubrr_value & 0xFFU);

    /*
     * Async UART, 8 data bits, no parity, 1 stop bit.
     * RX complete interrupt is enabled.
     */
    UART_ucsrb = (1U << UART_rxen_bit) |
                 (1U << UART_txen_bit) |
                 (1U << UART_rxcie_bit);

    UART_ucsrb &= (uint8_t)~(1U << UART_ucsz2_bit);

    UART_ucsrc = (1U << UART_ucsz1_bit) |
                 (1U << UART_ucsz0_bit);

    /*
     * Ensure UDRE interrupt is disabled at init.
     * It is only enabled when there is data waiting in the software TX buffer.
     */
    UART_ucsrb &= (uint8_t)~(1U << UART_udrie_bit);
}

/*********************************************************************
 * @brief Report whether at least one RX byte is buffered.
 *********************************************************************/
bool uart_rx_available()
{
    uint8_t tail = rx_buffer.tail;
    uint8_t head = rx_buffer.head;

    return (head != tail);
}

/*********************************************************************
 * @brief Read one received byte from the RX buffer.
 *********************************************************************/
bool uart_read_byte(uint8_t *byte)
{
    return uart_rx_buffer_pop(byte);
}

/*********************************************************************
 * @brief Return the number of buffered RX bytes.
 *********************************************************************/
uint8_t uart_rx_count()
{
    uint8_t tail = rx_buffer.tail;
    uint8_t head = rx_buffer.head;

    return (head - tail) & UART_BUFFER_MASK;
}

/*********************************************************************
 * @brief Write uart byte.
 *********************************************************************/
bool uart_write_byte(uint8_t byte)
{
    bool result = uart_tx_buffer_push(byte);

    if (result)
    {
        /*
         * Enable UDRE interrupt so bytes in the software TX buffer
         * are fed into the UART data register as it becomes ready.
         */
        UART_ucsrb |= (1U << UART_udrie_bit);
    }

    return result;
}

/*********************************************************************
 * @brief Write uart.
 *********************************************************************/
bool uart_write(const uint8_t *data, size_t length)
{
    size_t i;

    if ((data == NULL) && (length > 0U))
    {
        return false;
    }

    for (i = 0U; i < length; i++)
    {
        if (!uart_tx_buffer_push(data[i]))
        {
            return false;
        }
    }

    if (length > 0U)
    {
        /*
         * Enable UDRE interrupt so bytes in the software TX buffer
         * are fed into the UART data register as it becomes ready.
         */
        UART_ucsrb |= (1U << UART_udrie_bit);
    }

    return true;
}

/*********************************************************************
 * @brief Return the number of buffered TX bytes.
 *********************************************************************/
uint8_t uart_tx_count()
{
    uint8_t tail = tx_buffer.tail;
    uint8_t head = tx_buffer.head;

    return (head - tail) & UART_BUFFER_MASK;
}

/*********************************************************************
 * @brief Report whether UART transmission is still in progress.
 *********************************************************************/
bool uart_tx_busy()
{
    uint8_t tail = tx_buffer.tail;
    uint8_t head = tx_buffer.head;

    return (head != tail);
}

/*********************************************************************
 * @brief Get uart error flags.
 *********************************************************************/
uart_error_flags_t uart_get_error_flags()
{
    return uart_error_state;
}

/*********************************************************************
 * @brief Clear uart error flags.
 *********************************************************************/
void uart_clear_error_flags(uint8_t flags)
{
    ATOMIC_BLOCK(ATOMIC_FORCEON) 
    {
        uart_error_state &= ~flags;
    }
}

/*********************************************************************
 * @brief Clear uart all error flags.
 *********************************************************************/
void uart_clear_all_error_flags(void)
{
    ATOMIC_BLOCK(ATOMIC_FORCEON) 
    {
        uart_error_state = UART_ERROR_NONE;
    }
}

/* --------------------------------------------------------------------------
 * Interrupt service routines
 * -------------------------------------------------------------------------- */

ISR(USART0_RX_vect)
{
    uint8_t sts = UART_ucsra;
    uint8_t data = UART_udr;

    // Map hardware error bits → software flags
    uart_error_state |=
        ((sts >> 4) & UART_ERROR_FRAME)   |     // FE0  (bit 4) → bit 0
        ((sts >> 2) & UART_ERROR_OVERRUN) |     // DOR0 (bit 3) → bit 1
        (sts       & UART_ERROR_PARITY);        // UPE0 (bit 2) → bit 2

    (void)uart_rx_buffer_push(data);
}

/*********************************************************************
 * @brief Handle the USART0_UDRE_vect interrupt service routine.
 *********************************************************************/
ISR(USART0_UDRE_vect)
{
    uint8_t data;

    if (uart_tx_buffer_pop(&data))
    {
        UART_udr = data;
    }
    else
    {
        /*
         * Disable UDRE interrupt because there are no more bytes
         * waiting in the software TX buffer.
         */
        UART_ucsrb &= (uint8_t)~(1U << UART_udrie_bit);
    }
}
