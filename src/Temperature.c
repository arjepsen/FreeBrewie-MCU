#include "Temperature.h"
#include "Board.h"

#include <avr/interrupt.h>
#include <util/crc16.h>
#include <util/delay.h>

#include <stdbool.h>
#include <stdint.h>

// ======================================================================================
//  DEFINES
// ======================================================================================

#define DS18B20_CMD_SKIP_ROM 0xCCU
#define DS18B20_CMD_CONVERT_T 0x44U
#define DS18B20_CMD_READ_SCRATCHPAD 0xBEU

#define ONEWIRE_RESET_LOW_US 480U
#define ONEWIRE_PRESENCE_WAIT_US 70U
#define ONEWIRE_RESET_RECOVERY_US 410U

#define ONEWIRE_WRITE_1_LOW_US 6U
#define ONEWIRE_WRITE_1_RECOVERY_US 54U

#define ONEWIRE_WRITE_0_LOW_US 60U
#define ONEWIRE_WRITE_0_RECOVERY_US 5U

#define ONEWIRE_READ_INIT_LOW_US 3U
#define ONEWIRE_READ_SAMPLE_US 10U
#define ONEWIRE_READ_RECOVERY_US 53U

#define DS18B20_CONVERT_TIMEOUT_MS 800U
#define DS18B20_POLL_DELAY_MS 10U // Increased for fewer loops
#define DS18B20_SCRATCHPAD_SIZE 9U

// ======================================================================================
//  PROTOTYPES
// ======================================================================================
static uint8_t onewire_mask(uint8_t bit);
static void onewire_drive_low(volatile uint8_t *port, volatile uint8_t *ddr, uint8_t bit);
static void onewire_release(volatile uint8_t *port, volatile uint8_t *ddr, uint8_t bit);
static bool onewire_read_pin(volatile uint8_t *pinreg, uint8_t bit);

static bool onewire_reset(volatile uint8_t *port,
                          volatile uint8_t *ddr,
                          volatile uint8_t *pinreg,
                          uint8_t bit);

static void onewire_write_bit(volatile uint8_t *port,
                              volatile uint8_t *ddr,
                              uint8_t bit,
                              uint8_t value);

static uint8_t onewire_read_bit(volatile uint8_t *port,
                                volatile uint8_t *ddr,
                                volatile uint8_t *pinreg,
                                uint8_t bit);

static void onewire_write_byte(volatile uint8_t *port,
                               volatile uint8_t *ddr,
                               uint8_t bit,
                               uint8_t value);

static uint8_t onewire_read_byte(volatile uint8_t *port,
                                 volatile uint8_t *ddr,
                                 volatile uint8_t *pinreg,
                                 uint8_t bit);

// static uint8_t ds18b20_crc8(const uint8_t *data, uint8_t length);
static bool ds18b20_check_scratchpad_crc(const uint8_t *scratchpad);

static bool ds18b20_start_conversion(volatile uint8_t *port,
                                     volatile uint8_t *ddr,
                                     volatile uint8_t *pinreg,
                                     uint8_t bit);

static bool ds18b20_wait_for_conversion(volatile uint8_t *port,
                                        volatile uint8_t *ddr,
                                        volatile uint8_t *pinreg,
                                        uint8_t bit);

static bool ds18b20_read_scratchpad(volatile uint8_t *port,
                                    volatile uint8_t *ddr,
                                    volatile uint8_t *pinreg,
                                    uint8_t bit,
                                    uint8_t *scratchpad);

static bool ds18b20_read_c_x10(volatile uint8_t *port,
                               volatile uint8_t *ddr,
                               volatile uint8_t *pinreg,
                               uint8_t bit,
                               int16_t *temp_c_x10);

// ======================================================================================
//  IMPLEMENTATION
// ======================================================================================

/*********************************************************************
 * @brief Initialize the temperature inputs.
 *
 * Releases both 1-Wire buses so the external pull-up resistor can hold
 * them high. Internal pull-ups are left disabled.
 *********************************************************************/
void temperature_init()
{
    onewire_release(&BOIL_TEMP_1WIRE_PORT, &BOIL_TEMP_1WIRE_DDR, BOIL_TEMP_1WIRE_BIT);
    onewire_release(&MASH_TEMP_1WIRE_PORT, &MASH_TEMP_1WIRE_DDR, MASH_TEMP_1WIRE_BIT);
}

/*********************************************************************
 * @brief Read boil temperature in 0.1 °C units.
 *
 * @param temp_c_x10 Output pointer for signed temperature in x10 °C.
 *
 * @return true if a valid reading was obtained, otherwise false.
 *********************************************************************/
bool temperature_read_boil_c_x10(int16_t *temp_c_x10)
{
    return ds18b20_read_c_x10(&BOIL_TEMP_1WIRE_PORT,
                              &BOIL_TEMP_1WIRE_DDR,
                              &BOIL_TEMP_1WIRE_PINREG,
                              BOIL_TEMP_1WIRE_BIT,
                              temp_c_x10);
}

/*********************************************************************
 * @brief Read mash temperature in 0.1 °C units.
 *
 * @param temp_c_x10 Output pointer for signed temperature in x10 °C.
 *
 * @return true if a valid reading was obtained, otherwise false.
 *********************************************************************/
bool temperature_read_mash_c_x10(int16_t *temp_c_x10)
{
    return ds18b20_read_c_x10(&MASH_TEMP_1WIRE_PORT,
                              &MASH_TEMP_1WIRE_DDR,
                              &MASH_TEMP_1WIRE_PINREG,
                              MASH_TEMP_1WIRE_BIT,
                              temp_c_x10);
}

/*********************************************************************
 * @brief Build a bit mask for one GPIO bit.
 *********************************************************************/
static inline uint8_t onewire_mask(uint8_t bit)
{
    return (1U << bit);
}

/*********************************************************************
 * @brief Actively pull the 1-Wire bus low.
 *********************************************************************/
static inline void onewire_drive_low(volatile uint8_t *port, volatile uint8_t *ddr, uint8_t bit)
{
    *port &= ~(1U << bit);
    *ddr |= (1U << bit);
}

/*********************************************************************
 * @brief Release the 1-Wire bus.
 *
 * The pin is returned to input mode with internal pull-up disabled.
 *********************************************************************/
static inline void onewire_release(volatile uint8_t *port, volatile uint8_t *ddr, uint8_t bit)
{
    *port &= ~(1U << bit);
    *ddr &= ~(1U << bit);
}

/*********************************************************************
 * @brief Read the logic level on the 1-Wire pin.
 *********************************************************************/
static inline bool onewire_read_pin(volatile uint8_t *pinreg, uint8_t bit)
{
    return *pinreg & (1U << bit);
}

/*********************************************************************
 * @brief Issue a 1-Wire reset and detect presence.
 *
 * @return true if a presence pulse was detected, otherwise false.
 *********************************************************************/
static bool onewire_reset(volatile uint8_t *port,
                          volatile uint8_t *ddr,
                          volatile uint8_t *pinreg,
                          uint8_t bit)
{
    uint16_t timeout_us = 1000U;
    uint8_t mask = (uint8_t)(1 << bit);

    // Release 1-wire
    *port &= ~mask;
    *ddr &= ~mask;

    // Wait for logic level on 1-wire pin to go high - check timeout.
    while (!(*pinreg & mask)) // Read pin
    {
        if (timeout_us-- == 0U)
        {
            return false;
        }

        _delay_us(1);
    }

    uint8_t sreg = SREG;
    cli();

    // Drive 1-wire low, then do a reset delay.
    *port &= ~mask;
    *ddr |= mask;
    _delay_us(ONEWIRE_RESET_LOW_US);

    // Release, then another delay.
    *ddr &= ~mask;
    _delay_us(ONEWIRE_PRESENCE_WAIT_US);

    bool present = !(*pinreg & mask);

    SREG = sreg;

    _delay_us(ONEWIRE_RESET_RECOVERY_US);

    return present;
}

/*********************************************************************
 * @brief Write one 1-Wire bit.
 *********************************************************************/
static inline void onewire_write_bit(volatile uint8_t *port,
                                     volatile uint8_t *ddr,
                                     uint8_t bit,
                                     uint8_t value)
{
    uint8_t mask = (uint8_t)(1 << bit);

    uint8_t sreg = SREG;
    cli();

    // Drive the 1-wire low
    *port &= ~mask;
    *ddr |= mask;

    if (value)
    {
        _delay_us(ONEWIRE_WRITE_1_LOW_US);
        *ddr &= ~mask; // Release 1-wire
        SREG = sreg;
        _delay_us(ONEWIRE_WRITE_1_RECOVERY_US);
    }
    else
    {
        _delay_us(ONEWIRE_WRITE_0_LOW_US);
        *ddr &= ~mask; // Release 1-wire
        SREG = sreg;
        _delay_us(ONEWIRE_WRITE_0_RECOVERY_US);
    }
}

/*********************************************************************
 * @brief Read one 1-Wire bit.
 *********************************************************************/
static inline uint8_t onewire_read_bit(volatile uint8_t *port,
                                       volatile uint8_t *ddr,
                                       volatile uint8_t *pinreg,
                                       uint8_t bit)
{
    uint8_t mask = (uint8_t)(1 << bit);

    // Save interrupts.
    uint8_t sreg = SREG;
    cli();

    // Drive 1-wire low.
    *port &= ~mask;
    *ddr |= mask;
    _delay_us(ONEWIRE_READ_INIT_LOW_US);

    *ddr &= ~mask; // Release 1-wire.
    _delay_us(ONEWIRE_READ_SAMPLE_US);

    // Read the bit, and restore interrupts.
    uint8_t value = (*pinreg & mask) ? 1U : 0U;
    SREG = sreg;

    _delay_us(ONEWIRE_READ_RECOVERY_US);

    return value;
}

/*********************************************************************
 * @brief Write one byte LSB-first on 1-Wire.
 *********************************************************************/
static inline void onewire_write_byte(volatile uint8_t *port,
                                      volatile uint8_t *ddr,
                                      uint8_t bit,
                                      uint8_t value)
{
    uint8_t bit_index;
    for (bit_index = 0U; bit_index < 8U; bit_index++)
    {
        onewire_write_bit(port, ddr, bit, value & 0x01U);
        value >>= 1U;
    }
}

/*********************************************************************
 * @brief Read one byte LSB-first from 1-Wire.
 *********************************************************************/
static uint8_t onewire_read_byte(volatile uint8_t *port,
                                 volatile uint8_t *ddr,
                                 volatile uint8_t *pinreg,
                                 uint8_t bit)
{
    uint8_t value = 0U;
    uint8_t bit_index;

    for (bit_index = 0U; bit_index < 8U; bit_index++)
    {
        if (onewire_read_bit(port, ddr, pinreg, bit) != 0U)
        {
            value |= (uint8_t)(1U << bit_index);
        }
    }

    return value;
}

/*********************************************************************
 * @brief Ds18b20 check scratchpad crc.
 *********************************************************************/
static inline bool ds18b20_check_scratchpad_crc(const uint8_t *scratchpad)
{
    uint8_t crc = 0U;

    crc = _crc_ibutton_update(crc, scratchpad[0]);
    crc = _crc_ibutton_update(crc, scratchpad[1]);
    crc = _crc_ibutton_update(crc, scratchpad[2]);
    crc = _crc_ibutton_update(crc, scratchpad[3]);
    crc = _crc_ibutton_update(crc, scratchpad[4]);
    crc = _crc_ibutton_update(crc, scratchpad[5]);
    crc = _crc_ibutton_update(crc, scratchpad[6]);
    crc = _crc_ibutton_update(crc, scratchpad[7]);

    return (crc == scratchpad[8]);
}

/*********************************************************************
 * @brief Start one temperature conversion.
 *********************************************************************/
static bool ds18b20_start_conversion(volatile uint8_t *port,
                                     volatile uint8_t *ddr,
                                     volatile uint8_t *pinreg,
                                     uint8_t bit)
{
    if (!onewire_reset(port, ddr, pinreg, bit))
    {
        return false;
    }

    onewire_write_byte(port, ddr, bit, DS18B20_CMD_SKIP_ROM);
    onewire_write_byte(port, ddr, bit, DS18B20_CMD_CONVERT_T);

    return true;
}

/*********************************************************************
 * @brief Wait until conversion finishes.
 *
 * Because the sensor is externally powered, the bus can be polled and
 * the device will return 0 while busy and 1 when conversion is done.
 *********************************************************************/
static bool ds18b20_wait_for_conversion(volatile uint8_t *port,
                                        volatile uint8_t *ddr,
                                        volatile uint8_t *pinreg,
                                        uint8_t bit)
{
    uint16_t elapsed_ms;

    for (elapsed_ms = 0U; elapsed_ms < DS18B20_CONVERT_TIMEOUT_MS; elapsed_ms += DS18B20_POLL_DELAY_MS)
    {
        if (onewire_read_bit(port, ddr, pinreg, bit) != 0U)
        {
            return true;
        }

        _delay_ms(DS18B20_POLL_DELAY_MS);
    }

    return false;
}

/*********************************************************************
 * @brief Read the full 9-byte scratchpad and verify CRC.
 *********************************************************************/
static bool ds18b20_read_scratchpad(volatile uint8_t *port,
                                    volatile uint8_t *ddr,
                                    volatile uint8_t *pinreg,
                                    uint8_t bit,
                                    uint8_t *scratchpad)
{
    if (!onewire_reset(port, ddr, pinreg, bit))
    {
        return false;
    }

    onewire_write_byte(port, ddr, bit, DS18B20_CMD_SKIP_ROM);
    onewire_write_byte(port, ddr, bit, DS18B20_CMD_READ_SCRATCHPAD);

    uint8_t index;

    for (index = 0U; index < DS18B20_SCRATCHPAD_SIZE; index++)
    {
        scratchpad[index] = onewire_read_byte(port, ddr, pinreg, bit);
    }

    return ds18b20_check_scratchpad_crc(scratchpad);
}

/*********************************************************************
 * @brief Read one DS18B20 and return temperature in x10 °C.
 *
 * Keeps the device at its default 12-bit resolution. This preserves
 * 0.0625 °C raw LSB size, which is the safer choice for a trustworthy
 * x10 °C result. Conversion completion is polled to avoid always taking
 * the full worst-case 750 ms.
 *********************************************************************/
static bool ds18b20_read_c_x10(volatile uint8_t *port,
                               volatile uint8_t *ddr,
                               volatile uint8_t *pinreg,
                               uint8_t bit,
                               int16_t *temp_c_x10)
{
    uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE];

    if ((temp_c_x10 == 0) ||
        (!ds18b20_start_conversion(port, ddr, pinreg, bit)) ||
        (!ds18b20_wait_for_conversion(port, ddr, pinreg, bit)) ||
        (!ds18b20_read_scratchpad(port, ddr, pinreg, bit, scratchpad)))
    {
        return false;
    }

    int16_t raw_temp = (int16_t)(((uint16_t)scratchpad[1] << 8U) | scratchpad[0]);
    int32_t scaled_temp = (int32_t)raw_temp * 10L;

    scaled_temp += (scaled_temp >= 0) ? 8L : -8L;

    *temp_c_x10 = (int16_t)(scaled_temp >> 4);

    return true;
}
