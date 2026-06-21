#ifndef TWI_H
#define TWI_H

#include <stdbool.h>
#include <stdint.h>


// ======================================================================================
//  MACROS
// ======================================================================================

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#ifndef TWI_SCL_FREQUENCY_HZ
#define TWI_SCL_FREQUENCY_HZ                     100000UL
#endif

#define TWI_TIMEOUT_ITERATIONS                   50000UL


// ======================================================================================
//  TYPES
// ======================================================================================

typedef enum
{
    TWI_STATUS_OK = 0,
    TWI_STATUS_TIMEOUT,
    TWI_STATUS_START_FAILED,
    TWI_STATUS_REPEATED_START_FAILED,
    TWI_STATUS_SLA_W_NACK,
    TWI_STATUS_SLA_R_NACK,
    TWI_STATUS_DATA_NACK,
    TWI_STATUS_BUS_ERROR,
    TWI_STATUS_ARBITRATION_LOST,
    TWI_STATUS_INVALID_ARGUMENT,
    TWI_STATUS_UNEXPECTED_STATE
} twi_status_t;


// ======================================================================================
//  PROTOTYPES
// ======================================================================================

void twi_init();
twi_status_t twi_probe(uint8_t address_7bit, bool *present);
twi_status_t twi_write(uint8_t address_7bit, const uint8_t *data, uint8_t length);
twi_status_t twi_read(uint8_t address_7bit, uint8_t *data, uint8_t length);
twi_status_t twi_write_read(uint8_t address_7bit,
                            const uint8_t *tx_data,
                            uint8_t tx_length,
                            uint8_t *rx_data,
                            uint8_t rx_length);

#endif