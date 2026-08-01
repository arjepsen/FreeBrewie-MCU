# Brewie MCU Pin Map

Dedicated physical pin map for the **ATmega2560 TQFP-100** used in Brewie.

This table follows the **physical package pin numbering**. The **MCU pin name** column has been corrected against the pinout image you provided and the ATmega2560 datasheet.

## Companion documents
This file is one part of a 4-document set:

- **`Brewie_MCU_Pin_Map_Updated_2026-03-23.md`** = hardware truth
- **`Brewie_MCU_Structure_Notes_Updated_2026-03-23.md`** = naming, ownership, and architecture truth
- **`Brewie_MCU_Roadmap_Updated_2026-03-23.md`** = progress, order of work, and remaining steps
- **`Brewie_MCU_Application_Flow_2026-03-23.md`** = top-level program flow and state model

Use this file when the question is **what is connected where**.
Use the structure notes when the question is **how the code should think about it**.
Use the roadmap when the question is **what we do next**.
Use the application-flow document when the question is **how the finished firmware should be organized above the drivers**.

---

## Current update note (2026-03-23)
- the project now also has a separate `App` layer above `Main.c`, but that belongs in the structure, roadmap, and application-flow documents rather than in the pin table.
- `PB7` should be treated as the agreed 6.5 V servo-rail enable signal.
- `PL5` is the currently used `POWER_LED` signal in the codebase.
- `PF3` should no longer be treated as the active power LED pin in the current project notes.
- `PG0` is the mash temperature 1-Wire bus and `PG1` is the boil temperature 1-Wire bus.
- `PG5` = `MASH_HTR2_CTRL` and `PE2` = `BOIL_HTR1_CTRL` are now active heater outputs in the current code.
- `PK0` = `AC_MEASURE` is now actively used by `Heaters` through `ADC.c/.h` for contiguous measurement windows.
- `Timer4` is currently used by `Valves` for ISR-generated servo control pulses.
- `Timer3` is currently used by `Pumps` for the diagnostics tick.
- `ADC.c/.h` is now the intended owner of direct ADC register access, with `Valves`, `Pumps`, `Heaters`, and `Solenoids` all going through that shared ownership model.
- `PA1` / `PK2` and `PA2` / `PK3` are now live-confirmed as the two inlet-solenoid output + current-sense pairs.
- the latest code state now includes a dedicated `Solenoids` module for `BREW_INLET` and `COOLING_INLET`.

## Status key
- ✅ = confirmed from traced schematic / board work
- 🟡 = inferred from code or partially provisioned
- ◻ = N/C
- ⏺ = crystal pin
- ⚡ = power, ground, reset, or reference pin

| Phys. pin | MCU pin | Arduino alias | Function | Description | Notes | Status |
|---:|---|---|---|---|---|:---:|
| 1 | PG5 | D4 | GPIO / timer output | `MASH_HTR2_CTRL` | Maybe use PWM? | ✅ |
| 2 | PE0 | D0 / RX0 | UART RX0 | `RX_ARM` | | ✅ |
| 3 | PE1 | D1 / TX0 | UART TX0 | `TX_ARM` | | ✅ |
| 4 | PE2 | — | GPIO | `BOIL_HTR1_CTRL` | Maybe use PWM? | ✅ |
| 5 | PE3 | D5 | GPIO / PWM | N/C | | ◻ |
| 6 | PE4 | D2 | GPIO / interrupt | `BOIL_PUMP_TACHO` || ✅ |
| 7 | PE5 | D3 | GPIO / interrupt | `MASH_PUMP_TACHO` || ✅ |
| 8 | PE6 | — | GPIO / interrupt | N/C | | ◻ |
| 9 | PE7 | — | GPIO / clock out | N/C | | ◻ |
| 10 | VCC | — | Power | Digital supply || ⚡ |
| 11 | GND | — | Ground | Ground || ⚡ |
| 12 | PH0 | D17 / RX2 | GPIO / UART RX2 | N/C || ◻ |
| 13 | PH1 | D16 / TX2 | GPIO / UART TX2 | N/C || ◻ |
| 14 | PH2 | — | GPIO | N/C || ◻ |
| 15 | PH3 | D6 | GPIO / PWM | N/C || ◻ |
| 16 | PH4 | D7 | GPIO / PWM | N/C || ◻ |
| 17 | PH5 | D8 | GPIO / PWM | N/C || ◻ |
| 18 | PH6 | D9 | GPIO | `PUMP_DAC_LDAC` || ✅ |
| 19 | PB0 | D53 / SS | SPI SS | N/C || ◻ |
| 20 | PB1 | D52 / SCK | SPI SCK | `PUMP_DAC_CLK` || ✅ |
| 21 | PB2 | D51 / MOSI | SPI MOSI | `PUMP_DAC_SDI` / SPI MOSI || ✅ |
| 22 | PB3 | D50 / MISO | SPI MISO | `PUMP_DAC_MISO` | Maybe not connected? | ◻ |
| 23 | PB4 | D10 | GPIO | `PUMP_DAC_CS` || ✅ |
| 24 | PB5 | D11 | GPIO | `PRE_CHARGE` || ✅ |
| 25 | PB6 | D12 | GPIO | `PWR_EN_5V` || ✅ |
| 26 | PB7 | D13 | GPIO | `PWR_EN_6V5_SERVO` | Agreed power-rail name; active in current code | ✅ |
| 27 | PH7 | — | GPIO | `PWR_EN_12V` || ✅ |
| 28 | PG3 | — | GPIO / oscillator | N/C || ◻ |
| 29 | PG4 | — | GPIO / oscillator | N/C || ◻ |
| 30 | RESET | — | Reset | MCU reset input | Reset from SOM module| ⚡ |
| 31 | VCC | — | Power | Digital supply || ⚡ |
| 32 | GND | — | Ground | Ground || ⚡ |
| 33 | XTAL2 | — | Crystal | External crystal connection || ⏺ |
| 34 | XTAL1 | — | Crystal | External crystal connection || ⏺ |
| 35 | PL0 | D49 | GPIO / timer input | N/C | | 🟡 |
| 36 | PL1 | D48 | GPIO / timer input | N/C | | 🟡 |
| 37 | PL2 | D47 | GPIO / timer input | N/C | | 🟡 |
| 38 | PL3 | D46 | GPIO / PWM | N/C || 🟡 |
| 39 | PL4 | D45 | GPIO / PWM | N/C | | 🟡 |
| 40 | PL5 | D44 | GPIO | `POWER_LED` | Confirmed in current code `Board.h` / `Leds.h` | ✅ |
| 41 | PL6 | D43 | GPIO | N/C | | 🟡 |
| 42 | PL7 | D42 | GPIO | N/C || 🟡 |
| 43 | PD0 | D21 / SCL | I²C SCL | `BOIL_MASS_SCL` || ✅ |
| 44 | PD1 | D20 / SDA | I²C SDA | `BOIL_MASS_SDA` || ✅ |
| 45 | PD2 | D19 / RX1 | GPIO | `DRAIN_BTN` || ✅ |
| 46 | PD3 | D18 / TX1 | GPIO / UART TX1 / interrupt | N/C || ◻ |
| 47 | PD4 | — | GPIO / timer input | `MASH_MASS_SENSOR_SCL` | No device, but connection ready | ✅ |
| 48 | PD5 | — | GPIO / timer | `MASH_MASS_SENSOR_SDA` |  No device, but connection ready | ✅ |
| 49 | PD6 | — | GPIO / timer | N/C || ◻ |
| 50 | PD7 | D38 | GPIO | `PWR_BTN` | | ✅ |
| 51 | PG0 | D41 | GPIO | `MASH_TEMP_1WIRE` | Mash temperature 1-Wire bus | ✅ |
| 52 | PG1 | D40 | GPIO | `BOIL_TEMP_1WIRE` | Boil temperature 1-Wire bus | ✅ |
| 53 | PC0 | D37 | GPIO / address bus | N/C || ◻ |
| 54 | PC1 | D36 | GPIO / address bus | `BOIL_INL_VALVE` || ✅ |
| 55 | PC2 | D35 | GPIO / address bus | N/C || ◻ |
| 56 | PC3 | D34 | GPIO / address bus | `MASH_PUMP_EN` || ✅ |
| 57 | PC4 | D33 | GPIO / address bus | N/C || ◻ |
| 58 | PC5 | D32 | GPIO / address bus | `BOIL_PUMP_EN` || ✅ |
| 59 | PC6 | D31 | GPIO / address bus | N/C || ◻ |
| 60 | PC7 | D30 | GPIO / address bus | `DRAIN_BTN_LED` || ✅ |
| 61 | VCC | — | Power | Digital supply || ⚡ |
| 62 | GND | — | Ground | Ground || ⚡ |
| 63 | PJ0 | D15 / RX3 | GPIO / UART RX3 | N/C || 🟡 |
| 64 | PJ1 | D14 / TX3 | GPIO / UART TX3 | N/C || 🟡 |
| 65 | PJ2 | — | GPIO | `MASH_IN_VALVE` || ✅ |
| 66 | PJ3 | — | GPIO | `BOIL_RTN_VALVE` || ✅ |
| 67 | PJ4 | — | GPIO | `VALVE_5` | No device in machine, but hardware ready | 🟡 |
| 68 | PJ5 | — | GPIO | `OUTLET_VALVE` || ✅ |
| 69 | PJ6 | — | GPIO | `COOL_VALVE` || ✅ |
| 70 | PG2 | D39 | GPIO | N/C || ◻ |
| 71 | PA7 | D29 | GPIO / address bus | `HOP2_VALVE` || ✅ |
| 72 | PA6 | D28 | GPIO / address bus | `MASH_RTN_VALVE` || ✅ |
| 73 | PA5 | D27 | GPIO / address bus | `HOP4_VALVE` || ✅ |
| 74 | PA4 | D26 | GPIO / address bus | `HOP1_VALVE` || ✅ |
| 75 | PA3 | D25 | GPIO / address bus | `HOP3_VALVE` || ✅ |
| 76 | PA2 | D24 | GPIO / address bus | `COOLING_INLET` | Dedicated inlet-solenoid output; live-tested with `PK3` current rise | ✅ |
| 77 | PA1 | D23 | GPIO / address bus | `BREW_INLET` | Dedicated inlet-solenoid output; live-tested with `PK2` current rise | ✅ |
| 78 | PA0 | D22 | GPIO / address bus | `FAN_EN` || ✅ |
| 79 | PJ7 | — | GPIO | N/C  || 🟡 |
| 80 | GND | — | Ground | Ground || ⚡ |
| 81 | VCC | — | Power | Digital supply || ⚡ |
| 82 | PK7 | A15 / D69 | ADC | N/C | | 🟡 |
| 83 | PK6 | A14 / D68 | ADC | `MASH_SIDE_CRNT_SNS` || ✅ |
| 84 | PK5 | A13 / D67 | ADC | `BOIL_SIDE_CRNT_SNS` || ✅ |
| 85 | PK4 | A12 / D66 | ADC | `HOP_VALVES_CRNT_SNS` || ✅ |
| 86 | PK3 | A11 / D65 | ADC | `COOLING_INLET_CURRENT_SENSE` | Baseline ~19–20 ADC, energized ~339–340 alone, ~332 with both on | ✅ |
| 87 | PK2 | A10 / D64 | ADC | `BREW_INLET_CURRENT_SENSE` | Baseline ~18 ADC, energized ~320–321 alone, ~313–314 with both on | ✅ |
| 88 | PK1 | A9 / D63 | ADC | `uC_BOARD_TEMP` || ✅ |
| 89 | PK0 | A8 / D62 | ADC | `AC_MEASURE` || ✅ |
| 90 | PF7 | A7 / D61 | ADC | N/C || ◻ |
| 91 | PF6 | A6 / D60 | ADC | N/C || ◻ |
| 92 | PF5 | A5 / D59 | ADC | N/C || ◻ |
| 93 | PF4 | A4 / D58 | ADC | N/C || ◻ |
| 94 | PF3 | A3 / D57 | ADC / GPIO | N/C | No longer the active `POWER_LED` signal in current project notes | ◻ |
| 95 | PF2 | A2 / D56 | ADC / GPIO | `MCU_LED1` || ✅ |
| 96 | PF1 | A1 / D55 | ADC / GPIO | `MCU_LED2` || ✅ |
| 97 | PF0 | A0 / D54 | ADC / GPIO | N/C || ◻ |
| 98 | AREF | — | Reference | ADC reference pin || ⚡ |
| 99 | GND | — | Ground | Ground || ⚡ |
| 100 | AVCC | — | Power | Analog supply || ⚡ |


