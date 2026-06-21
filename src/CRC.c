#include "CRC.h"

#include <stddef.h>
#include <stdint.h>


// ======================================================================================
//  API FUNCTIONS
// ======================================================================================

/****************************************************************************************
 * @brief Return the Dallas/Maxim CRC-8 initial value.
 *
 * This keeps the call site explicit and makes the framing code read naturally when a CRC
 * needs to be built incrementally across header and payload fields.
 *
 * @return Initial CRC value.
 ****************************************************************************************/
uint8_t CRC8_Init()
{
    return 0x00U;
}

/****************************************************************************************
 * @brief Update one Dallas/Maxim CRC-8 step.
 *
 * The Dallas/Maxim variant is reflected. In bitwise form that means the runtime update
 * uses the reflected polynomial 0x8C rather than the forward polynomial 0x31.
 *
 * @param crc  Running CRC value.
 * @param data Next byte to fold into the CRC.
 *
 * @return Updated CRC value.
 ****************************************************************************************/
uint8_t CRC8_Update(uint8_t crc, uint8_t data)
{
    uint8_t bit_index;

    crc ^= data;

    for (bit_index = 0U; bit_index < 8U; bit_index++)
    {
        if ((crc & 0x01U) != 0U)
        {
            crc = (uint8_t)((crc >> 1U) ^ 0x8CU);
        }
        else
        {
            crc >>= 1U;
        }
    }

    return crc;
}

/****************************************************************************************
 * @brief Calculate a full Dallas/Maxim CRC-8 over a byte buffer.
 *
 * @param data   Pointer to the buffer to checksum.
 * @param length Number of bytes to include.
 *
 * @return Calculated CRC value. Returns the initial value when @p data is NULL.
 ****************************************************************************************/
uint8_t CRC8_Calculate(const uint8_t *data, size_t length)
{
    uint8_t crc = CRC8_Init();

    if (data == NULL)
    {
        return crc;
    }

    while (length > 0U)
    {
        crc = CRC8_Update(crc, *data);
        data++;
        length--;
    }

    return crc;
}
