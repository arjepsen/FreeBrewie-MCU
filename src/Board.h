#ifndef BOARD_H
#define BOARD_H

#include <avr/io.h>
#include <stdint.h>

/*
    Brewie ATmega2560 board mapping

    Purpose:
    - Central place for board-specific signal definitions
    - Tiny GPIO helper macros only
    - No behavior, sequencing, debounce, or driver logic

    Naming notes:
    - Use agreed traced names rather than legacy names
    - Keep enable/control signals separate from measured/sensed signals
*/

/* -------------------------------------------------------------------------- */
/* Core board constants                                                       */
/* -------------------------------------------------------------------------- */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#define UART_BAUD_DEBUG          115200UL

/* -------------------------------------------------------------------------- */
/* Tiny GPIO helper macros                                                    */
/* -------------------------------------------------------------------------- */

#define BIT_MASK(bit)            (1U << (bit))

#define GPIO_OUTPUT(ddr, bit)    ((ddr) |= BIT_MASK(bit))
#define GPIO_INPUT(ddr, bit)     ((ddr) &= (uint8_t)~BIT_MASK(bit))

#define GPIO_HIGH(port, bit)     ((port) |= BIT_MASK(bit))
#define GPIO_LOW(port, bit)      ((port) &= (uint8_t)~BIT_MASK(bit))
#define GPIO_TOGGLE(port, bit)   ((port) ^= BIT_MASK(bit))

#define GPIO_PULLUP_ON(port, bit)    ((port) |= BIT_MASK(bit))
#define GPIO_PULLUP_OFF(port, bit)   ((port) &= (uint8_t)~BIT_MASK(bit))

#define GPIO_READ(pin, bit)      (((pin) & BIT_MASK(bit)) != 0U)

/* -------------------------------------------------------------------------- */
/* MCU-board LEDs                                                             */
/* -------------------------------------------------------------------------- */

#define MCU_LED1_PORT            PORTF
#define MCU_LED1_DDR             DDRF
#define MCU_LED1_PINREG          PINF
#define MCU_LED1_BIT             PF2

#define MCU_LED2_PORT            PORTF
#define MCU_LED2_DDR             DDRF
#define MCU_LED2_PINREG          PINF
#define MCU_LED2_BIT             PF1

/* -------------------------------------------------------------------------- */
/* Buttons & indicator LEDs                                                   */
/* -------------------------------------------------------------------------- */

#define POWER_BUTTON_PORT        PORTD
#define POWER_BUTTON_DDR         DDRD
#define POWER_BUTTON_PINREG      PIND
#define POWER_BUTTON_BIT         PD7

#define DRAIN_BUTTON_PORT        PORTD
#define DRAIN_BUTTON_DDR         DDRD
#define DRAIN_BUTTON_PINREG      PIND
#define DRAIN_BUTTON_BIT         PD2

#define DRAIN_LED_PORT           PORTC
#define DRAIN_LED_DDR            DDRC
#define DRAIN_LED_PINREG         PINC
#define DRAIN_LED_BIT            PC7

#define POWER_LED_PORT           PORTL
#define POWER_LED_DDR            DDRL
#define POWER_LED_PINREG         PINL
#define POWER_LED_BIT            PL5


/* -------------------------------------------------------------------------- */
/* Fan control outputs                                                        */
/* -------------------------------------------------------------------------- */

#define VENT_FAN_EN_PORT         PORTA
#define VENT_FAN_EN_DDR          DDRA
#define VENT_FAN_EN_PINREG       PINA
#define VENT_FAN_EN_BIT          PA0

#define POWER_FAN_EN_PORT        PORTF
#define POWER_FAN_EN_DDR         DDRF
#define POWER_FAN_EN_PINREG      PINF
#define POWER_FAN_EN_BIT         PF0

/* -------------------------------------------------------------------------- */
/* Power control outputs                                                      */
/* -------------------------------------------------------------------------- */

#define PRECHARGE_5V_PORT        PORTB
#define PRECHARGE_5V_DDR         DDRB
#define PRECHARGE_5V_PINREG      PINB
#define PRECHARGE_5V_BIT         PB5

#define PWR_EN_5V_PORT           PORTB
#define PWR_EN_5V_DDR            DDRB
#define PWR_EN_5V_PINREG         PINB
#define PWR_EN_5V_BIT            PB6

#define PWR_EN_6V5_SERVO_PORT        PORTB
#define PWR_EN_6V5_SERVO_DDR         DDRB
#define PWR_EN_6V5_SERVO_PINREG      PINB
#define PWR_EN_6V5_SERVO_BIT         PB7

#define PWR_EN_12V_PORT             PORTH
#define PWR_EN_12V_DDR              DDRH
#define PWR_EN_12V_PINREG           PINH
#define PWR_EN_12V_BIT              PH7

/*
    PB0 note:
    PB0 is hardware SPI SS. Old hardware appears to expose a 12V-present style signal
    here, but firmware should keep PB0 as an output to preserve SPI master mode.
*/
#define SPI_MASTER_SS_PORT       PORTB
#define SPI_MASTER_SS_DDR        DDRB
#define SPI_MASTER_SS_PINREG     PINB
#define SPI_MASTER_SS_BIT        PB0

/* -------------------------------------------------------------------------- */
/* UART                                                                       */
/* -------------------------------------------------------------------------- */

/* USART0 <-> SOM / ARM side */
#define RX_ARM_PORT              PORTE
#define RX_ARM_DDR               DDRE
#define RX_ARM_PINREG            PINE
#define RX_ARM_BIT               PE0

#define TX_ARM_PORT              PORTE
#define TX_ARM_DDR               DDRE
#define TX_ARM_PINREG            PINE
#define TX_ARM_BIT               PE1

/* -------------------------------------------------------------------------- */
/* Heater control outputs                                                     */
/* -------------------------------------------------------------------------- */

#define MASH_HTR2_CTRL_PORT      PORTG
#define MASH_HTR2_CTRL_DDR       DDRG
#define MASH_HTR2_CTRL_PINREG    PING
#define MASH_HTR2_CTRL_BIT       PG5

#define BOIL_HTR1_CTRL_PORT      PORTE
#define BOIL_HTR1_CTRL_DDR       DDRE
#define BOIL_HTR1_CTRL_PINREG    PINE
#define BOIL_HTR1_CTRL_BIT       PE2

/* -------------------------------------------------------------------------- */
/* Pump control                                                               */
/* -------------------------------------------------------------------------- */

#define MASH_PUMP_EN_PORT        PORTC
#define MASH_PUMP_EN_DDR         DDRC
#define MASH_PUMP_EN_PINREG      PINC
#define MASH_PUMP_EN_BIT         PC3

#define BOIL_PUMP_EN_PORT        PORTC
#define BOIL_PUMP_EN_DDR         DDRC
#define BOIL_PUMP_EN_PINREG      PINC
#define BOIL_PUMP_EN_BIT         PC5

#define MASH_PUMP_TACH_PORT      PORTE
#define MASH_PUMP_TACH_DDR       DDRE
#define MASH_PUMP_TACH_PINREG    PINE
#define MASH_PUMP_TACH_BIT       PE5

#define BOIL_PUMP_TACH_PORT      PORTE
#define BOIL_PUMP_TACH_DDR       DDRE
#define BOIL_PUMP_TACH_PINREG    PINE
#define BOIL_PUMP_TACH_BIT       PE4

/* Pump DAC: MCP4812 over SPI */
#define PUMP_DAC_CS_PORT         PORTB
#define PUMP_DAC_CS_DDR          DDRB
#define PUMP_DAC_CS_PINREG       PINB
#define PUMP_DAC_CS_BIT          PB4

#define PUMP_DAC_SCK_PORT        PORTB
#define PUMP_DAC_SCK_DDR         DDRB
#define PUMP_DAC_SCK_PINREG      PINB
#define PUMP_DAC_SCK_BIT         PB1

#define PUMP_DAC_MOSI_PORT       PORTB
#define PUMP_DAC_MOSI_DDR        DDRB
#define PUMP_DAC_MOSI_PINREG     PINB
#define PUMP_DAC_MOSI_BIT        PB2

#define PUMP_DAC_MISO_PORT       PORTB
#define PUMP_DAC_MISO_DDR        DDRB
#define PUMP_DAC_MISO_PINREG     PINB
#define PUMP_DAC_MISO_BIT        PB3

#define PUMP_DAC_LDAC_PORT       PORTH
#define PUMP_DAC_LDAC_DDR        DDRH
#define PUMP_DAC_LDAC_PINREG     PINH
#define PUMP_DAC_LDAC_BIT        PH6

/* -------------------------------------------------------------------------- */
/* Valve / solenoid outputs                                                   */
/* -------------------------------------------------------------------------- */

#define BREW_INLET_PORT          PORTA
#define BREW_INLET_DDR           DDRA
#define BREW_INLET_PINREG        PINA
#define BREW_INLET_BIT           PA1

#define COOLING_INLET_PORT       PORTA
#define COOLING_INLET_DDR        DDRA
#define COOLING_INLET_PINREG     PINA
#define COOLING_INLET_BIT        PA2

#define HOP1_VALVE_PORT          PORTA
#define HOP1_VALVE_DDR           DDRA
#define HOP1_VALVE_PINREG        PINA
#define HOP1_VALVE_BIT           PA4

#define HOP2_VALVE_PORT          PORTA
#define HOP2_VALVE_DDR           DDRA
#define HOP2_VALVE_PINREG        PINA
#define HOP2_VALVE_BIT           PA7

#define HOP3_VALVE_PORT          PORTA
#define HOP3_VALVE_DDR           DDRA
#define HOP3_VALVE_PINREG        PINA
#define HOP3_VALVE_BIT           PA3

#define HOP4_VALVE_PORT          PORTA
#define HOP4_VALVE_DDR           DDRA
#define HOP4_VALVE_PINREG        PINA
#define HOP4_VALVE_BIT           PA5

#define MASH_RTN_VALVE_PORT      PORTA
#define MASH_RTN_VALVE_DDR       DDRA
#define MASH_RTN_VALVE_PINREG    PINA
#define MASH_RTN_VALVE_BIT       PA6

#define BOIL_INLET_VALVE_PORT    PORTC
#define BOIL_INLET_VALVE_DDR     DDRC
#define BOIL_INLET_VALVE_PINREG  PINC
#define BOIL_INLET_VALVE_BIT     PC1

#define MASH_IN_VALVE_PORT       PORTJ
#define MASH_IN_VALVE_DDR        DDRJ
#define MASH_IN_VALVE_PINREG     PINJ
#define MASH_IN_VALVE_BIT        PJ2

#define BOIL_RTN_VALVE_PORT      PORTJ
#define BOIL_RTN_VALVE_DDR       DDRJ
#define BOIL_RTN_VALVE_PINREG    PINJ
#define BOIL_RTN_VALVE_BIT       PJ3

#define OUTLET_VALVE_PORT        PORTJ
#define OUTLET_VALVE_DDR         DDRJ
#define OUTLET_VALVE_PINREG      PINJ
#define OUTLET_VALVE_BIT         PJ5

#define COOL_VALVE_PORT          PORTJ
#define COOL_VALVE_DDR           DDRJ
#define COOL_VALVE_PINREG        PINJ
#define COOL_VALVE_BIT           PJ6

/* Optional / provisioned but apparently unused in machine */
#define VALVE_5_PORT             PORTJ
#define VALVE_5_DDR              DDRJ
#define VALVE_5_PINREG           PINJ
#define VALVE_5_BIT              PJ4

/* -------------------------------------------------------------------------- */
/* Temperature 1-Wire Buses                                                   */
/* -------------------------------------------------------------------------- */

#define BOIL_TEMP_1WIRE_PORT      PORTG
#define BOIL_TEMP_1WIRE_DDR       DDRG
#define BOIL_TEMP_1WIRE_PINREG    PING
#define BOIL_TEMP_1WIRE_BIT       PG1

#define MASH_TEMP_1WIRE_PORT      PORTG
#define MASH_TEMP_1WIRE_DDR       DDRG
#define MASH_TEMP_1WIRE_PINREG    PING
#define MASH_TEMP_1WIRE_BIT       PG0

/* -------------------------------------------------------------------------- */
/* Mass sensor i2c                                                            */
/* -------------------------------------------------------------------------- */
#define BOIL_MASS_SCL_PORT       PORTD
#define BOIL_MASS_SCL_DDR        DDRD
#define BOIL_MASS_SCL_PINREG     PIND
#define BOIL_MASS_SCL_BIT        PD0

#define BOIL_MASS_SDA_PORT       PORTD
#define BOIL_MASS_SDA_DDR        DDRD
#define BOIL_MASS_SDA_PINREG     PIND
#define BOIL_MASS_SDA_BIT        PD1

/*
    Optional mash-side mass sensor connection:
    Hardware appears provisioned, but sensor is not populated.
*/
// #define MASH_MASS_SCL_PORT       PORTD
// #define MASH_MASS_SCL_DDR        DDRD
// #define MASH_MASS_SCL_PINREG     PIND
// #define MASH_MASS_SCL_BIT        PD4

// #define MASH_MASS_SDA_PORT       PORTD
// #define MASH_MASS_SDA_DDR        DDRD
// #define MASH_MASS_SDA_PINREG     PIND
// #define MASH_MASS_SDA_BIT        PD5

/* -------------------------------------------------------------------------- */
/* ADC channels                                                               */
/* -------------------------------------------------------------------------- */

/*
    ATmega2560 ADC channel mapping:
    PF0..PF7 -> ADC0..ADC7
    PK0..PK7 -> ADC8..ADC15
*/

#define ADC_CHANNEL_PK0          8U
#define ADC_CHANNEL_PK1          9U
#define ADC_CHANNEL_PK2          10U
#define ADC_CHANNEL_PK3          11U
#define ADC_CHANNEL_PK4          12U
#define ADC_CHANNEL_PK5          13U
#define ADC_CHANNEL_PK6          14U
#define ADC_CHANNEL_PK7          15U

#define AC_MEAS_ADC_CHANNEL                     ADC_CHANNEL_PK0
#define UC_BOARD_TEMP_ADC_CHANNEL               ADC_CHANNEL_PK1
#define BREW_INLET_CURRENT_SENSE_ADC_CHANNEL    ADC_CHANNEL_PK2
#define COOLING_INLET_CURRENT_SENSE_ADC_CHANNEL ADC_CHANNEL_PK3
#define VALVE_CURRENT_SENSE_ADC_CHANNEL         ADC_CHANNEL_PK4
#define BOIL_PUMP_CURRENT_SENSE_ADC_CHANNEL     ADC_CHANNEL_PK5
#define MASH_PUMP_CURRENT_SENSE_ADC_CHANNEL     ADC_CHANNEL_PK6

// /* Temporary compatibility aliases while the rest of the code is updated. */
// #define HOP_VALVES_CURRENT_SENSE_ADC_CHANNEL    VALVE_CURRENT_SENSE_ADC_CHANNEL
// #define BOIL_PCB_CURRENT_SENSE_ADC_CHANNEL      BOIL_PUMP_CURRENT_SENSE_ADC_CHANNEL
// #define MASH_PCB_CURRENT_SENSE_ADC_CHANNEL      MASH_PUMP_CURRENT_SENSE_ADC_CHANNEL

#define AC_MEAS_PORT                        PORTK
#define AC_MEAS_DDR                         DDRK
#define AC_MEAS_PINREG                      PINK
#define AC_MEAS_BIT                         PK0

#define UC_BOARD_TEMP_PORT                  PORTK
#define UC_BOARD_TEMP_DDR                   DDRK
#define UC_BOARD_TEMP_PINREG                PINK
#define UC_BOARD_TEMP_BIT                   PK1

#define BREW_INLET_CURRENT_SENSE_PORT       PORTK
#define BREW_INLET_CURRENT_SENSE_DDR        DDRK
#define BREW_INLET_CURRENT_SENSE_PINREG     PINK
#define BREW_INLET_CURRENT_SENSE_BIT        PK2

#define COOLING_INLET_CURRENT_SENSE_PORT    PORTK
#define COOLING_INLET_CURRENT_SENSE_DDR     DDRK
#define COOLING_INLET_CURRENT_SENSE_PINREG  PINK
#define COOLING_INLET_CURRENT_SENSE_BIT     PK3

#define VALVE_CURRENT_SENSE_PORT            PORTK
#define VALVE_CURRENT_SENSE_DDR             DDRK
#define VALVE_CURRENT_SENSE_PINREG          PINK
#define VALVE_CURRENT_SENSE_BIT             PK4

#define BOIL_PUMP_CURRENT_SENSE_PORT        PORTK
#define BOIL_PUMP_CURRENT_SENSE_DDR         DDRK
#define BOIL_PUMP_CURRENT_SENSE_PINREG      PINK
#define BOIL_PUMP_CURRENT_SENSE_BIT         PK5

#define MASH_PUMP_CURRENT_SENSE_PORT        PORTK
#define MASH_PUMP_CURRENT_SENSE_DDR         DDRK
#define MASH_PUMP_CURRENT_SENSE_PINREG      PINK
#define MASH_PUMP_CURRENT_SENSE_BIT         PK6

// /* Temporary compatibility aliases while the rest of the code is updated. */
// #define HOP_VALVES_CURRENT_SENSE_PORT       VALVE_CURRENT_SENSE_PORT
// #define HOP_VALVES_CURRENT_SENSE_DDR        VALVE_CURRENT_SENSE_DDR
// #define HOP_VALVES_CURRENT_SENSE_PINREG     VALVE_CURRENT_SENSE_PINREG
// #define HOP_VALVES_CURRENT_SENSE_BIT        VALVE_CURRENT_SENSE_BIT

// #define BOIL_PCB_CURRENT_SENSE_PORT         BOIL_PUMP_CURRENT_SENSE_PORT
// #define BOIL_PCB_CURRENT_SENSE_DDR          BOIL_PUMP_CURRENT_SENSE_DDR
// #define BOIL_PCB_CURRENT_SENSE_PINREG       BOIL_PUMP_CURRENT_SENSE_PINREG
// #define BOIL_PCB_CURRENT_SENSE_BIT          BOIL_PUMP_CURRENT_SENSE_BIT

// #define MASH_PCB_CURRENT_SENSE_PORT         MASH_PUMP_CURRENT_SENSE_PORT
// #define MASH_PCB_CURRENT_SENSE_DDR          MASH_PUMP_CURRENT_SENSE_DDR
// #define MASH_PCB_CURRENT_SENSE_PINREG       MASH_PUMP_CURRENT_SENSE_PINREG
// #define MASH_PCB_CURRENT_SENSE_BIT          MASH_PUMP_CURRENT_SENSE_BIT

/* -------------------------------------------------------------------------- */
/* Convenience init-state macros                                              */
/* -------------------------------------------------------------------------- */

#define BUTTON_ACTIVE_STATE      1U
#define LED_ON_STATE             1U
#define LED_OFF_STATE            0U

#endif