# Brewie MCU Runtime Services

## Purpose
This document defines the ownership and boundaries of the main runtime calls in the MCU firmware.

It works together with:
- **`Brewie_SOM_MCU_Protocol_2026-04-01.md`** = communication and control model
- **`Brewie_MCU_Application_Flow_2026-04-01.md`** = top-level state model
- **`Brewie_MCU_Structure_Notes_Updated_2026-04-01.md`** = naming and ownership truth
- **`Brewie_MCU_Roadmap_Updated_2026-04-01.md`** = progress and next steps

---

## Preferred outer loop

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

## Main rules
- runtime calls define the outer firmware skeleton
- runtime layers should mostly produce **facts**, not **decisions**
- the SOM-MCU protocol feeds the supervisor layer with target-state snapshots, not recipe semantics
- `Main.c` should stay thin; `Runtime.c` owns the service helpers
- power-button facts are gathered below the supervisor layer; startup policy belongs to the supervisor layer

---

## 1. `service_fast_tasks()`
**Purpose:**
Keep very frequent, non-blocking service work responsive.

**Owned by:**
- `Runtime.c`

**Owns:**
- `buttons_update()`
- UART RX/TX byte and buffer service
- non-blocking driver completion polling
- lightweight raw event intake

**Must not own:**
- recipe/process logic
- protocol policy
- fault policy
- blocking jobs

**Rule:**
Produce fresh facts quickly.

---

## 2. `service_timed_tasks()`
**Purpose:**
Run regular periodic maintenance without bloating the fast loop.

**Owned by:**
- `Runtime.c`

**Owns:**
- cadence and scheduling
- sensor refresh progression
- medium-weight diagnostics progression
- status/report cadence bookkeeping
- timeout/freshness facts that belong to the MCU itself
- the current temporary pacing/cadence mechanism while the runtime structure is still being assembled

**Must not own:**
- recipe timing
- step sequencing
- high-level machine intent

**Rule:**
Maintain machine facts over time.

---

## 3. `comms_update()`
**Purpose:**
Own the SOM-MCU communication path as a bounded translation layer.

**Owns:**
- frame recognition
- CRC validation
- message-type decode
- `SEQ` handling
- `ACK` / `NACK` generation
- heartbeat acceptance
- control-snapshot acceptance into a compact supervisor-facing request structure
- periodic transmit progression for `STATUS_REPORT`
- transmit progression for `FAULT_REPORT`

**Publishes upward:**
- latest accepted `CONTROL_SNAPSHOT` request
- heartbeat-alive facts
- shutdown request
- fault-clear request
- protocol error / health facts

**Must not own:**
- direct actuator poking
- recipe meaning
- final safety decisions
- direct driver-policy bypass

**Rule:**
Communication feeds the supervisor layer; it does not bypass it.

### Important protocol interpretation rule
`CONTROL_SNAPSHOT` means:
- the current target state the SOM now wants the MCU to work from

It does **not** mean:
- a recipe step
- a script
- a timed phase owned by the MCU

---

## 4. `faults_update()`
**Purpose:**
Own the central fault-evaluation layer.

**Owns:**
- collect fault-related facts from modules and timed services
- evaluate active/inactive fault conditions
- maintain latch/clearability behavior
- build the current central fault picture
- raise fault-state change information for `FAULT_REPORT`

**Must not own:**
- full machine behavior
- protocol formatting
- recipe/process policy

**Rule:**
Build one central fault picture before the supervisor layer reacts.

---

## 5. `supervisor_update()`
**Purpose:**
Own top-level MCU behavior.

**Owns:**
- top-level MCU state (`STANDBY`, `BOOT`, `ACTIVE`, `FAULT`, `SHUTDOWN`)
- waiting for a user power-button request while in `STANDBY`
- startup sequencing behavior that begins after the power-button request
- acceptance/rejection of supervisor-level requests according to current state and safety rules
- current accepted target snapshot from the SOM
- top-level shutdown behavior
- requested outputs published toward `outputs_apply()`

**Must not own:**
- recipe logic
- manual-service workflow logic from the SOM UI
- low-level ADC ownership
- raw UART parsing

**Rule:**
The supervisor layer is mainly a control-context and safety layer, not a brewing workflow engine.

---

## 6. `outputs_apply()`
**Purpose:**
Commit final allowed hardware outputs.

**Owns:**
- translate current requested outputs into actual hardware calls
- enforce final safe gating
- preserve MCU-owned policy for things that should not be directly protocol-driven

**Examples of MCU-owned policy here or below it:**
- servo-rail enable/disable around actual valve motion
- LED policy that is deliberately MCU-owned
- fan policy that is deliberately MCU-owned
- keeping startup rails off until the supervisor has left `STANDBY`

**Must not own:**
- recipe meaning
- protocol decode
- fault classification

**Rule:**
Final hardware commit happens only after supervisor/fault gating.
