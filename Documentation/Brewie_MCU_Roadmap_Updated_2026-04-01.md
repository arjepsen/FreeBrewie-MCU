# Brewie MCU Roadmap

## Purpose
This document tracks project progress and next steps for the Brewie MCU firmware.

It now reflects:
- a standby-aware top-level MCU state model
- a clearer SOM-MCU protocol direction
- a clearer split between SOM-owned logic and MCU-owned execution/safety

## Companion documents
- **`Brewie_MCU_Pin_Map_Updated_2026-04-01.md`** = hardware truth
- **`Brewie_SOM_MCU_Protocol_2026-04-01.md`** = protocol and control model
- **`Brewie_MCU_Structure_Notes_Updated_2026-04-01.md`** = naming, ownership, and architecture truth
- **`Brewie_MCU_Runtime_Services_2026-04-01.md`** = outer-loop service ownership
- **`Brewie_MCU_Application_Flow_2026-04-01.md`** = top-level state model

---

## Current project stage
The project is still past isolated driver bring-up and is now in the early top-level integration phase.

Current position:
- major hardware modules exist
- protocol direction is now much clearer
- the top-level layer has been renamed from `App` to `Supervisor`
- runtime service helpers have been moved out of `Main.c` into a dedicated `Runtime` layer
- the MCU is now being treated more explicitly as a generic executor/safety layer rather than a brewing-workflow engine
- the state model has been refined again so startup truth is captured better:
  - `STANDBY`
  - `BOOT`
  - `ACTIVE`
  - `FAULT`
  - `SHUTDOWN`

This is a good clarification because it separates:
- MCU alive on dedicated supply
from
- user-requested machine/SOM startup

---

## High-level work breakdown

| Area | Purpose | Current status | Notes |
|---|---|:---:|---|
| Hardware mapping | Confirm pin use and signal naming | ✅ | Tracked mainly in the pin map |
| Core low-level drivers | Make each hardware block usable and testable | 🟡 | Main blocks exist; polish remains |
| Shared infrastructure | Make ADC/timers/SPI/UART coexist cleanly | 🟡 | Still important for integration |
| Protocol/control model | Define SOM↔MCU message model clearly | 🟡 | Now substantially clearer |
| Supervisor structure | Keep MCU top level compact and truthful | 🟡 | Standby-aware state model now adopted |
| Safety and fault handling | Force safe behavior when needed | 🟡 | Still active work |
| Production runtime loop | Assemble the full runtime-loop shape | 🟡 | Direction clear, implementation still maturing |
| Final validation and cleanup | Freeze behavior and docs | ⬜ | Later stage |

---

## Recommended execution order

### 1. Keep the standby-aware state model stable in code and docs
**Goal:** stop supervisor-level startup behavior from drifting into undocumented assumptions.

This means growing code and docs against:
- `STANDBY`
- `BOOT`
- `ACTIVE`
- `FAULT`
- `SHUTDOWN`

Where:
- `STANDBY` = MCU alive, waiting for `POWER_BUTTON`
- `BOOT` = startup sequence after user request, including SOM power-up and heartbeat wait

**Status:** 🟡

---

### 2. Freeze the first protocol document against that startup model
**Goal:** keep the SOM↔MCU control model aligned with the real startup path.

This includes:
- one common frame format
- `SEQ`
- `ACK` / `NACK`
- `HEARTBEAT`
- `CONTROL_SNAPSHOT`
- `STATUS_REPORT`
- `FAULT_REPORT`
- `SHUTDOWN_REQUEST`
- `FAULT_CLEAR_REQUEST`

And the boot interaction now needs to be described as:
- MCU comes up into `STANDBY`
- user requests start
- MCU enters `BOOT`
- MCU powers SOM
- first valid heartbeat causes `BOOT -> ACTIVE`

**Status:** 🟡

---

### 3. Lock down shared-resource ownership in practice
**Goal:** avoid integration bugs caused by modules fighting over hardware resources.

Main areas:
- ADC ownership
- timer ownership
- UART usage split between debug and future protocol traffic
- clear ownership split between `Main`, `Runtime`, `Supervisor`, `Comms`, `Faults`, and `Outputs`

**Status:** 🟡

---

### 4. Build and verify the first real protocol path in code
**Goal:** implement and prove the first working SOM↔MCU framed communication path.

High-level scope:
- receive/validate frames
- decode `CONTROL_SNAPSHOT`
- handle `HEARTBEAT`
- transmit `STATUS_REPORT`
- transmit `FAULT_REPORT`
- handle `ACK` / `NACK`

**Status:** 🟡

---

### 5. Build the first real safety/fault path in code
**Goal:** make the machine safe before higher-level behavior grows much further.

Likely first areas:
- invalid sensor detection
- heartbeat-loss handling
- actuator fault detection
- heater interlocks
- safe fallback behavior

**Status:** 🟡

---

### 6. Flesh out the production runtime structure
**Goal:** keep the intended runtime structure but replace the remaining temporary scaffolding.

This now means:
- keep `Main.c` thin
- keep runtime service helpers in `Runtime.c`
- mature the fast/timed service layers
- mature communication handling
- mature fault handling
- mature supervisor coordination
- mature output application

**Status:** 🟡

---

### 7. System-level validation and cleanup
**Goal:** make firmware coherent, testable, and ready to freeze.

Includes:
- integrated hardware tests
- expected-behavior checks across modules
- cleanup of temporary scaffolding
- final documentation sync

**Status:** ⬜

---

## Practical checklist

### Already established enough for the next step
- [x] hardware pin mapping is substantially built out
- [x] major hardware blocks have named modules
- [x] `ADC.c/.h` is the intended shared ADC owner
- [x] `Supervisor.h/.c` now exists above the hardware modules
- [x] `Runtime.h/.c` now owns the service helpers that should not live in `Main.c`
- [x] the first protocol direction is now clear enough to document
- [x] the startup model is now explicitly user-gated through `STANDBY` and `POWER_BUTTON`

### Still to complete before MCU firmware is finished
- [ ] protocol details frozen enough for code implementation
- [ ] standby-aware state model reflected fully and coherently in code
- [ ] first real framed protocol path implemented and tested
- [ ] first real safety/fault framework implemented
- [ ] temporary runtime scaffolding replaced with production behavior
- [ ] integrated validation completed
- [ ] final documentation wording frozen

---

## Current recommended next focus
At the current stage, the most sensible next focus is:
1. keep docs and code aligned with the new `STANDBY` startup model
2. verify the first real SOM↔MCU protocol path against that startup model
3. build the first real safety/fault path
4. keep `Main.c` thin while the runtime loop is assembled
