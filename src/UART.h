#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UART_BUFFER_SIZE    64U

typedef enum
{
    UART_ERROR_NONE         = 0x00,
    UART_ERROR_FRAME        = 0x01,
    UART_ERROR_OVERRUN      = 0x02,
    UART_ERROR_PARITY       = 0x04,
    UART_ERROR_RX_OVERFLOW  = 0x08,
    UART_ERROR_TX_OVERFLOW  = 0x10
} uart_error_flags_t;

void uart_init();

bool uart_rx_available();
bool uart_read_byte(uint8_t *byte);
uint8_t uart_rx_count();

bool uart_write_byte(uint8_t byte);
bool uart_write(const uint8_t *data, size_t length);
uint8_t uart_tx_count();
bool uart_tx_busy();

uart_error_flags_t uart_get_error_flags();
void uart_clear_error_flags(uint8_t flags);
void uart_clear_all_error_flags();

#endif