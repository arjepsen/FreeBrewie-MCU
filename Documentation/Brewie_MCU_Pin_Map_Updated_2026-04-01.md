# Brewie MCU Pin Map

Dedicated physical pin map for the **ATmega2560 TQFP-100** used in Brewie.

This table follows the **physical package pin numbering**. The **MCU pin name** column has been corrected against the pinout image and the ATmega2560 datasheet.

## Companion documents
This file is one part of the MCU document set:

- **`Brewie_MCU_Pin_Map_Updated_2026-04-01.md`** = hardware truth
- **`Brewie_MCU_Structure_Notes_Updated_2026-04-01.md`** = naming, ownership, and architecture truth
- **`Brewie_MCU_Roadmap_Updated_2026-04-01.md`** = progress, order of work, and remaining steps
- **`Brewie_MCU_Application_Flow_2026-04-01.md`** = top-level program flow and state model

Use this file when the question is **what is connected where**.
Use the structure notes when the question is **how the code should think about it**.
Use the roadmap when the question is **what we do next**.
Use the application-flow document when the question is **how the finished firmware should be organized above the drivers**.

---

## Current update note (2026-04-01)
- the project has a separate `Supervisor` layer above the hardware modules
- the project has a dedicated `Runtime` module that owns the service helpers that should not live in `Main.c`
- the top-level MCU startup model now includes a `STANDBY` state where the MCU is alive on its dedicated supply and waits for the `POWER_BUTTON`
- `PB7` should be treated as the agreed 6.5 V servo-rail enable signal
- `PL5` is the currently used `POWER_LED` signal in the codebase
- `PF3` should no longer be treated as the active power LED pin in the current project notes
- `PG0` is the mash temperature 1-Wire bus and `PG1` is the boil temperature 1-Wire bus
- `PG5` = `MASH_HTR2_CTRL` and `PE2` = `BOIL_HTR1_CTRL` are now active heater outputs in the current code
- `PK0` = `AC_MEASURE` is now actively used by `Heaters` through `ADC.c/.h` for contiguous measurement windows
- `Timer4` is currently used by `Valves` for ISR-generated servo control pulses
- `Timer3` is currently used by `Pumps` for the diagnostics tick
- `ADC.c/.h` is now the intended owner of direct ADC register access, with `Valves`, `Pumps`, `Heaters`, and `Solenoids` all going through that shared ownership model
- `PA1` / `PK2` and `PA2` / `PK3` are now live-confirmed as the two inlet-solenoid output + current-sense pairs
- the latest code state includes a dedicated `Solenoids` module for `BREW_INLET` and `COOLING_INLET`
- `POWER_BUTTON` and `DRAIN_BUTTON` follow the old ReBrewie electrical behavior: plain digital inputs, no internal MCU pull-up, and pressed = HIGH
- `STANDBY` output policy for the current project is intentionally simple: only on-board `MCU_LED1` should be on; `POWER_LED`, `DRAIN_LED`, SOM-start rails, and other user-facing indicators must remain off until later state logic explicitly enables them

---

## Important startup interpretation reminder
The pin map itself does not define behavior, but the current project interpretation is now:

- mains power brings up the dedicated MCU-side supply
- the MCU enters `STANDBY`
- the MCU waits for `POWER_BUTTON`
- only after that does the MCU raise the other rails and start the SOM

So power-control pins like:
- `PRECHARGE_5V`
- `PWR_EN_5V`
- `PWR_EN_12V`

must be understood in that standby-aware startup context.
