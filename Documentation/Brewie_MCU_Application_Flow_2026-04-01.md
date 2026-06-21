# Brewie MCU Application Flow

## Purpose
This document defines the overall MCU program flow and top-level state model.

It is meant to work together with:
- **`Brewie_SOM_MCU_Protocol_2026-04-01.md`** = communication and control model
- **`Brewie_MCU_Runtime_Services_2026-04-01.md`** = outer-loop responsibilities
- **`Brewie_MCU_Structure_Notes_Updated_2026-04-01.md`** = naming, ownership, and architecture truth
- **`Brewie_MCU_Roadmap_Updated_2026-04-01.md`** = progress and next steps

---

## Main rule
The MCU does not know what a recipe is.

The SOM owns:
- recipe logic
- cleaning flows
- manual-service UI/logic
- timing of steps
- when target behavior changes

The MCU owns:
- hardware execution
- measurements
- safety/interlocks
- fault handling
- user-gated startup
- shutdown behavior

So the supervisor layer on the MCU is mainly a **control-context and safety layer**, not a brewing-workflow engine.

---

## Top-level machine states
The MCU now uses five top-level states:
- `STANDBY`
- `BOOT`
- `ACTIVE`
- `FAULT`
- `SHUTDOWN`

### `STANDBY`
The MCU board has a dedicated 5 V supply that comes up when mains power is applied.

In `STANDBY`:
- the MCU initializes its own essential hardware
- the rest of the machine rails remain off
- the SOM is not started yet
- the MCU waits for the user to press the `POWER_BUTTON`
- only on-board `MCU_LED1` should be on
- `POWER_LED` and `DRAIN_LED` should remain off

This is the user-gated pre-start state.

### `BOOT`
After the user presses the `POWER_BUTTON`, the MCU begins the real startup sequence.

In `BOOT`:
- the MCU raises the other rails in phases
- the MCU powers the SOM
- the MCU does not execute normal control snapshots yet
- the MCU waits for the SOM to start sending valid `HEARTBEAT` frames

### `ACTIVE`
Normal operating state.
The MCU may receive and apply valid control snapshots.
An idle machine is simply an `ACTIVE` control snapshot where targets happen to be off or inactive.

### `FAULT`
A fault has forced the MCU into fault-handling behavior.
Normal control snapshots are blocked or clamped to safe behavior.

### `SHUTDOWN`
Controlled stop and power-down path.
The MCU handles the machine and SOM shutdown sequence.

---

## Entry path after mains power is applied
The intended high-level sequence is now:

1. mains power is applied
2. the dedicated MCU 5 V supply comes up
3. MCU initializes and enters `STANDBY`
4. MCU waits for user power-button request
5. user presses `POWER_BUTTON`
6. MCU enters `BOOT`
7. MCU raises the required rails and powers the SOM
8. SOM boots
9. SOM sends first valid `HEARTBEAT`
10. MCU enters `ACTIVE`
11. MCU starts normal reporting

This is more truthful than treating all of that as one `BOOT` phase.

### Button interpretation note
For this startup path to work correctly, `POWER_BUTTON` must be interpreted the same way as in old ReBrewie:
- plain digital input
- no internal pull-up
- pressed = `HIGH`

If that polarity is changed accidentally, the MCU may leave `STANDBY` immediately at mains-up, which is incorrect.

---

## Entering `ACTIVE`
The MCU enters `ACTIVE` when the SOM has booted and the MCU receives the first valid `HEARTBEAT`.

That means:
1. the user has already requested startup
2. the MCU has already entered `BOOT`
3. the SOM is already powered and booting
4. the SOM sends `HEARTBEAT`
5. MCU enters `ACTIVE`
6. MCU starts normal reporting

---

## Why there is now a separate `STANDBY`
There is a real hardware and behavior difference between:
- MCU alive on its dedicated supply
and
- system startup requested and SOM power-up in progress

That difference is important enough to model explicitly.

`STANDBY` means:
- the machine is not yet starting
- the MCU is waiting for the user

`BOOT` means:
- startup has been requested
- the system/SOM startup sequence is in progress

---

## Why there is still no separate `IDLE`, `READY`, `MANUAL_SERVICE`, or `RUNNING`
The SOM sends the same kind of `CONTROL_SNAPSHOT` regardless of whether the higher-level intent is:
- brewing
- cleaning
- manual service

So from the MCU perspective there is still no strong reason to maintain separate top-level execution states for those meanings.
The distinction belongs mainly on the SOM.

The MCU mainly needs to know:
- am I in standby waiting for a user start request?
- am I in startup waiting for the SOM?
- am I in normal active control?
- am I in fault handling?
- am I shutting down?

That model is more truthful and still compact.

---

## Periodic task model
The firmware should still be structured around a few clear service layers.

### Fast service
Examples:
- button update
- UART RX/TX servicing
- non-blocking driver service
- lightweight event intake

### Timed service
Examples:
- sensor refresh
- heater measurement progression
- pump diagnostics progression
- status-cadence bookkeeping
- current temporary loop pacing while the runtime structure is still incomplete

### Supervisor service
Examples:
- detect power-button startup request while in `STANDBY`
- consume accepted protocol requests
- update top-level state
- request safe or active outputs
- react to fault summary

---

## Preferred loop shape

```c
for (;;)
{
    service_fast_tasks();
    service_timed_tasks();
    comms_update();
    faults_update();
    supervisor_update();
    outputs_apply();
}
```

---

## Control flow
Preferred model:
1. SOM sends `CONTROL_SNAPSHOT`
2. `comms_update()` validates and decodes it
3. supervisor layer accepts or rejects it according to current MCU state and safety rules
4. accepted target set becomes the active requested target state
5. `outputs_apply()` commits the final allowed hardware outputs

The MCU therefore works from the current requested target snapshot.
It does not execute a recipe script.

---

## Fault flow
Preferred model:
1. modules and timed services produce fault facts
2. `faults_update()` builds the current central fault picture
3. supervisor layer reacts at the top level
4. if required, MCU enters `FAULT`
5. safe outputs are requested and applied
6. MCU reports the fault picture to the SOM

---

## Supervisor runtime context
The supervisor layer should stay compact.
It mainly needs to own:
- current top-level state
- previous state / transition helper info if useful
- current accepted target snapshot from the SOM
- compact state-entry / state-age information
- power-button start request handling
- top-level shutdown progress
- top-level fault reaction context
- requested outputs published toward `outputs_apply()`

It should not become:
- a recipe engine
- a copy of every driver runtime struct
- a UART parser
- an ADC owner
- a large warehouse of raw sensor history

---

## Requested-output model
The supervisor layer should publish a compact requested-output state.
This should be derived from:
- the current accepted `CONTROL_SNAPSHOT`
- current MCU top-level state
- current safety/fault gating

So the output path becomes:
- SOM target snapshot
- accepted active target set
- safety/fault gating
- final hardware commit

---

## Practical interpretation of `ACTIVE`
`ACTIVE` does not mean pumps and heaters must be running.
It only means:
- the MCU is in normal operating mode
- valid control snapshots may be accepted and applied

An effectively idle machine is just a valid `ACTIVE` target set where targets happen to be inactive.
