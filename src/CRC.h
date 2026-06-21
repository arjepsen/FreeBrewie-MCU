#ifndef CRC_H
#define CRC_H

#include <stddef.h>
#include <stdint.h>

/*
 * CRC-8 Dallas/Maxim
 * - polynomial: 0x31 (reflected implementation uses 0x8C)
 * - init: 0x00
 * - refin/refout: true
 * - xorout: 0x00
 */
uint8_t CRC8_Init();
uint8_t CRC8_Update(uint8_t crc, uint8_t data);
uint8_t CRC8_Calculate(const uint8_t *data, size_t length);

#endif /* CRC_H */
