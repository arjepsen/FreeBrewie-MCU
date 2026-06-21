# Brewie MCU Structure Notes

## Purpose
This document captures the code structure, naming rules, module ownership, and top-level architectural direction for the Brewie AVR firmware.

It now reflects the current MCU-side standby/startup behavior and the clearer SOM-MCU control split.

## Companion documents
- **`Brewie_MCU_Pin_Map_Updated_2026-04-01.md`** = hardware truth
- **`Brewie_SOM_MCU_Protocol_2026-04-01.md`** = protocol and control model
- **`Brewie_MCU_Runtime_Services_2026-04-01.md`** = outer-loop service ownership
- **`Brewie_MCU_Application_Flow_2026-04-01.md`** = top-level state model
- **`Brewie_MCU_Roadmap_Updated_2026-04-01.md`** = progress and next steps

---

## Core principle
The MCU is not the recipe brain.

The SOM owns:
- user-facing logic
- recipes
- cleaning flows
- manual-service workflows
- when target behavior changes

The MCU owns:
- hardware execution
- measurements
- interlocks
- fault handling
- startup gating after user power-button request
- shutdown behavior

So the MCU should stay generic and compact.

---

## Current top-level ownership

### `Main.c`
Owns:
- init order
- enabling interrupts
- calling the runtime services in the main loop

### `Runtime.h/.c`
Owns:
- `service_fast_tasks()`
- `service_timed_tasks()`
- lightweight runtime cadence ownership that does not belong in `Main.c`

### `Supervisor.h/.c`
Owns:
- top-level MCU state
- user-start gating through the `POWER_BUTTON`
- acceptance of control snapshots at the supervisor level
- current accepted target snapshot
- top-level startup and shutdown handling
- requested outputs toward `outputs_apply()`

### Startup / button interpretation guardrail
The startup path must continue to match old ReBrewie button behavior unless deliberately re-documented:
- `POWER_BUTTON` and `DRAIN_BUTTON` are plain digital inputs
- internal MCU pull-ups are not part of the intended design here
- pressed = `HIGH`

This matters because a pull-up plus `HIGH == pressed` will falsely look like an active button and can break the `STANDBY -> BOOT` boundary.

### `comms_update()`
Owns:
- protocol framing
- CRC/`SEQ` handling
- message decode
- `ACK` / `NACK`
- receiving `CONTROL_SNAPSHOT`, `HEARTBEAT`, `SHUTDOWN_REQUEST`, and `FAULT_CLEAR_REQUEST`
- sending `STATUS_REPORT` and `FAULT_REPORT`

### `faults_update()`
Owns:
- one central fault picture
- clearability/latch behavior
- fault-state changes for reporting upward/outward

### `outputs_apply()`
Owns:
- final safe hardware commit
- MCU-owned derived behaviors such as servo-rail handling around valve motion

---

## Current top-level state model
Top-level MCU states are:
- `STANDBY`
- `BOOT`
- `ACTIVE`
- `FAULT`
- `SHUTDOWN`

### Meaning of `STANDBY`
`STANDBY` is the MCU-only waiting state.

At mains application:
- the dedicated MCU 5 V supply is already present
- the MCU may boot and initialize its own essentials
- the MCU does **not** yet raise the rest of the machine rails
- the MCU does **not** yet start the SOM
- the MCU waits for the user to press the `POWER_BUTTON`
- for the current project step, `STANDBY` should request only on-board `MCU_LED1`
- `POWER_LED` and `DRAIN_LED` should remain off in `STANDBY`

This state is therefore not the same thing as `BOOT`.

### Meaning of `BOOT`
`BOOT` now means:
- the user has requested startup
- the MCU is raising the other rails in order
- the SOM is being powered and is expected to start
- the MCU waits for the first valid `HEARTBEAT`

So `BOOT` is now the **post-start-request startup sequence**, not the mere existence of MCU power.

### Why `STANDBY` exists
There is a real hardware distinction between:
- the MCU being alive on its dedicated supply
and
- the machine/SOM startup sequence having been requested

That distinction is strong enough to earn a separate state.

---

## What top-level states are still not needed
There is still no strong MCU-side reason to keep separate top-level states for:
- `IDLE`
- `READY`
- `MANUAL_SERVICE`
- `RUNNING`

Those meanings belong mainly on the SOM.

An idle machine is simply an `ACTIVE` target snapshot where targets happen to be inactive.

---

## Control model
The protocol uses a `CONTROL_SNAPSHOT` sent from the SOM to the MCU.

This snapshot defines the **requested target state** for the machine hardware.
It is a full snapshot, not a patch.

The MCU should therefore work from:
- current accepted target snapshot
- current measured machine facts
- current fault/interlock state
- current top-level MCU state

It should not work from recipe-step semantics.

---

## What belongs in `CONTROL_SNAPSHOT`
Included:
- heater targets
- pump setpoints
- valve commands/states
- solenoid states

Not included:
- LEDs
- fan policy
- servo-rail enable
- high-level process meaning
- timing like “hold for N minutes”

These exclusions are deliberate:
- LEDs stay MCU-owned
- fan policy stays MCU-owned
- servo rail is derived by the MCU around valve motion

---

## Signal naming reminders
These project naming rules remain current. (relation to MCU pins)

### Power-related outputs
- `PRECHARGE_5V` = `PB5`
- `PWR_EN_5V` = `PB6`
- `PWR_EN_6V5_SERVO` = `PB7`
- `PWR_EN_12V` = `PH7`

### Button controls
- `POWER_BUTTON` = `PD7`
- `DRAIN_BUTTON` = `PD2`
- `POWER_LED` = `PL5`
- `DRAIN_LED` = `PC7`

### Temperature buses
- `PG0` = mash temperature 1-Wire bus
- `PG1` = boil temperature 1-Wire bus

### Solenoids
- `BREW_INLET` = `PA1`
- `COOLING_INLET` = `PA2`

---

## Current module status
The codebase includes working bring-up modules for:
- `Temperature`
- `Pressure`
- `ADC`
- `Valves`
- `Pumps`
- `Heaters`
- `Fans`
- `Solenoids`
- `Runtime`
- `Supervisor`

These remain the correct hardware and runtime modules.
The standby-aware state model does not change their low-level role.

---

## Cross-module ownership rules
These remain important:
- `ADC.c/.h` owns direct ADC register programming
- `Valves` uses non-blocking Timer4-driven PWM signals
- `Pumps` use DAC-set speed plus power-enable & tacho-signal handling
- `Heaters` own heater-driving logic and AC-measurement usage through shared ADC ownership rules
- `Solenoids` own direct inlet-solenoid control
- `Runtime` owns the outer-loop service helpers so they do not bloat `Main.c`

The new standby-aware state model should improve startup truthfulness without forcing low-level driver redesign.
