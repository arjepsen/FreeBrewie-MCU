# Brewie SOM-MCU Protocol

## Purpose
This document defines the communication model between the Brewie SOM and the ATmega2560 MCU.

It is intentionally **small, practical, and project-specific**.
It is not meant to be a generic production protocol for many product variants.

The protocol is built around one main idea:
- the **SOM owns high-level intent, timing, UI, recipes, cleaning flows, and manual-service logic**
- the **MCU owns execution, measurements, interlocks, faults, startup gating, and shutdown behavior**

So the MCU does **not** know what a recipe is.
It receives target-state snapshots, executes them if safe, and continuously reports back what the machine is actually doing.

## Companion documents
- **`Brewie_MCU_Pin_Map_Updated_2026-04-01.md`** = hardware truth
- **`Brewie_MCU_Structure_Notes_Updated_2026-04-01.md`** = naming, ownership, and architecture truth
- **`Brewie_MCU_Runtime_Services_2026-04-01.md`** = outer-loop responsibilities
- **`Brewie_MCU_Application_Flow_2026-04-01.md`** = state model and top-level behavior
- **`Brewie_MCU_Roadmap_Updated_2026-04-01.md`** = progress and next steps

---

## Main communication model
There are two broad categories of traffic:

### 1. SOM → MCU target control
The SOM sends the MCU a **`CONTROL_SNAPSHOT`**.
This is a full snapshot of the target hardware state the SOM wants the MCU to work from.

It is **not**:
- recipe semantics
- a script
- timed step logic
- a queue of future actions

It is simply a target state.

### 2. MCU → SOM machine reporting
The MCU sends the SOM:
- periodic **`STATUS_REPORT`** frames
- one-shot **`FAULT_REPORT`** frames when the fault picture changes

So the SOM always receives the current actual machine picture and any important fault transition.

---

## MCU states
The MCU now uses five top-level states:
- `STANDBY`
- `BOOT`
- `ACTIVE`
- `FAULT`
- `SHUTDOWN`

### `STANDBY`
- dedicated MCU supply is present
- MCU initializes its own essentials
- rest of machine rails remain off
- SOM is not started yet
- MCU waits for a user `POWER_BUTTON` request

### `BOOT`
- user has requested startup
- MCU powers the rest of the required rails
- MCU powers the SOM
- MCU does not execute `CONTROL_SNAPSHOT`
- MCU waits for the SOM to come alive

### `ACTIVE`
- normal operating state
- MCU may accept and apply `CONTROL_SNAPSHOT`
- an idle machine is simply an `ACTIVE` control snapshot with targets that happen to be off/inactive

### `FAULT`
- normal control execution is blocked or clamped to safe behavior
- MCU continues reporting
- MCU may accept `FAULT_CLEAR_REQUEST` if clear conditions are met

### `SHUTDOWN`
- MCU performs the controlled shutdown path
- normal control snapshots are no longer accepted
- MCU handles SOM power-down sequencing and any configured shutdown actions

### `STANDBY` → `BOOT`
The MCU enters `BOOT` when:
- mains is present
- the MCU is already alive on its dedicated supply
- and the user presses the `POWER_BUTTON`

### `BOOT` → `ACTIVE`
The MCU enters `ACTIVE` when it receives the first valid `HEARTBEAT` from the SOM.
At that point the MCU starts normal reporting.

---

## Frame format
All protocol messages use the same outer frame format.

```text
SYNC1   1 byte   0xAA
SYNC2   1 byte   0x55
TYPE    1 byte
SEQ     1 byte
LEN     1 byte
DATA    N bytes
CRC8    1 byte
```

Current shared maximum payload length:
- `40` bytes

This matches the current MCU receive buffer limit. The SOM code may allocate a larger local
buffer, but cross-system messages must stay within the shared protocol limit unless the MCU
limit is deliberately changed too.

### CRC coverage
CRC covers:
- `TYPE`
- `SEQ`
- `LEN`
- `DATA`

The sync bytes are not part of the CRC.

### Why one frame format
This keeps:
- parsing simpler
- resynchronization simpler
- debugging simpler
- future additions cleaner

Small messages like `ACK`, `NACK`, and `HEARTBEAT` stay efficient simply because their payloads are small.
They do **not** use a special short-format frame.

---

## `SEQ`
`SEQ` is the rolling sequence counter.

Rules:
- each sender maintains its own rolling `SEQ`
- valid values are `1..255`
- `0` is reserved
- after `255`, wrap to `1`

Purpose:
- correlate `ACK` / `NACK`
- support duplicate detection if needed later
- make logging/debugging clearer

---

## CRC
Use **CRC-8 Dallas/Maxim**.

Reason:
- small code cost
- good enough here

---

## Message types

| TYPE value | Name | Direction | Purpose |
|---:|---|---|---|
| `0x01` | `CONTROL_SNAPSHOT` | SOM → MCU | full requested target-state snapshot |
| `0x02` | `HEARTBEAT` | SOM → MCU | SOM liveness keepalive |
| `0x03` | `STATUS_REPORT` | MCU → SOM | periodic actual/current machine report |
| `0x04` | `FAULT_REPORT` | MCU → SOM | one-shot full fault-state change report |
| `0x05` | `ACK` | either direction | accepted message confirmation |
| `0x06` | `NACK` | either direction | rejected message confirmation |
| `0x07` | `SHUTDOWN_REQUEST` | SOM → MCU | begin controlled shutdown |
| `0x08` | `FAULT_CLEAR_REQUEST` | SOM → MCU | request clearing of clearable faults |

---

## Message definitions

### `CONTROL_SNAPSHOT`
**Direction:** SOM → MCU

**Purpose:**
Defines the target state the SOM now wants the MCU to work from.

**Rules:**
- full snapshot, not partial patch
- replaces the previous accepted target set
- valid only in `ACTIVE`
- requires `ACK` or `NACK`
- contains no recipe/service timing

### `HEARTBEAT`
**Direction:** SOM → MCU

**Purpose:**
Confirms that the SOM is still alive even when control targets are unchanged.

**Rules:**
- never changes hardware targets
- does not require `ACK`
- is the primary MCU liveness indicator for the SOM
- heartbeat loss may be used by the MCU to force safe behavior and decide whether SOM recovery/reboot handling is needed

**Recommended cadence:**
- once per second

**Initial timeout proposal:**
- 3 missed heartbeats ≈ 3 seconds

### `STATUS_REPORT`
**Direction:** MCU → SOM

**Purpose:**
Periodic compact report of the current actual machine state.

**Rules:**
- sent at a fixed interval while in `ACTIVE`, `FAULT`, and `SHUTDOWN`
- may also be useful in `BOOT` later, but that is not required for the first protocol milestone
- does not require `ACK`
- contains current actual/measured values, not only requested targets
- contains a compact active fault bitmap
- detailed/latching/primary fault information uses `FAULT_REPORT`

### `FAULT_REPORT`
**Direction:** MCU → SOM

**Purpose:**
Reports that the fault picture has changed.

**Rules:**
- sent when fault state changes
- does not require `ACK`
- always carries the current full fault picture, not only the delta
- detailed fault information stays separate from the compact active fault bitmap in `STATUS_REPORT`

### `ACK`
**Direction:** either direction, mainly MCU → SOM

**Purpose:**
Confirms that a message was received and accepted.

**Payload:**
- accepted `TYPE`
- accepted `SEQ`

### `NACK`
**Direction:** either direction, mainly MCU → SOM

**Purpose:**
Confirms that a message was received but rejected.

**Payload:**
- rejected `TYPE`
- rejected `SEQ`
- reject reason code

### `SHUTDOWN_REQUEST`
**Direction:** SOM → MCU

**Purpose:**
Requests a controlled shutdown sequence.

**Rules:**
- requires `ACK` or `NACK`
- if accepted, MCU enters `SHUTDOWN`
- normal `CONTROL_SNAPSHOT` handling stops

### `FAULT_CLEAR_REQUEST`
**Direction:** SOM → MCU

**Purpose:**
Requests clearing of clearable faults/latches.

**Rules:**
- requires `ACK` or `NACK`
- `ACK` means the request was accepted for evaluation, not that all faults were cleared
- active or non-clearable faults must remain set

---

## Which messages require `ACK` / `NACK`

### Requires `ACK` / `NACK`
- `CONTROL_SNAPSHOT`
- `SHUTDOWN_REQUEST`
- `FAULT_CLEAR_REQUEST`

### No `ACK` / `NACK` required
- `HEARTBEAT`
- `STATUS_REPORT`
- `FAULT_REPORT`

---

## Payload byte layouts

All multi-byte integer fields use little-endian byte order.

Signed temperature fields use signed 16-bit integer values in tenths of a degree Celsius.
For example, `201` means `20.1 C`.

### `CONTROL_SNAPSHOT` payload

Payload length:
- `16` bytes

```text
BYTE(S)   FIELD
0         mash_target_c
1         boil_target_c
2         mash_pump_setpoint
3         boil_pump_setpoint
4         solenoid_state_bits
5..15     valve_command[11]
```

#### `solenoid_state_bits`

```text
BIT 0   BREW_INLET
BIT 1   COOLING_INLET
```

All other bits are currently invalid and should cause `NACK` with `bad payload`.

#### `valve_command`

The 11 valve command bytes correspond to valve IDs 1 through 11 in order.

Each value must map to a known MCU valve position value. Unknown valve position values
should cause `NACK` with `bad payload`.

### `HEARTBEAT` payload

Payload length:
- `0` bytes

### `STATUS_REPORT` payload

Payload length:
- `27` bytes

```text
BYTE(S)   FIELD
0         mash_target_c
1         boil_target_c
2..3      mash_temp_c_x10      i16 little-endian
4..5      boil_temp_c_x10      i16 little-endian
6         mash_pump_setpoint
7         boil_pump_setpoint
8         mash_pump_running
9         boil_pump_running
10..11    pressure_count       u16 little-endian
12        solenoid_state_bits
13        status_bits
14..15    fault_flags          u16 little-endian
16..26    valve_state[11]
```

#### `status_bits`

```text
BIT 0   mash_temp_valid
BIT 1   boil_temp_valid
BIT 2   pressure_valid
BIT 3   heartbeat_alive
```

Temperature fields are reserved in the first status-report payload, but the current MCU code
keeps the temperature-valid bits clear until temperature refresh is moved to a non-blocking
timed-service cache.

#### `fault_flags`

`fault_flags` is the compact active fault bitmap from the MCU central fault layer.

Use `FAULT_REPORT` for the fuller fault picture, including latched flags and primary reason.

#### `valve_state`

The 11 valve state bytes correspond to valve IDs 1 through 11 in order.

### `FAULT_REPORT` payload

Payload length:
- `5` bytes

```text
BYTE(S)   FIELD
0..1      active_fault_flags    u16 little-endian
2..3      latched_fault_flags   u16 little-endian
4         primary_reason
```

### `ACK` payload

Payload length:
- `2` bytes

```text
BYTE(S)   FIELD
0         acked_type
1         acked_seq
```

### `NACK` payload

Payload length:
- `3` bytes

```text
BYTE(S)   FIELD
0         rejected_type
1         rejected_seq
2         reason
```

### `SHUTDOWN_REQUEST` payload

Payload length:
- `0` bytes

### `FAULT_CLEAR_REQUEST` payload

Payload length:
- `0` bytes

## `NACK` reason codes
- `0x01` = unknown type
- `0x02` = bad length
- `0x03` = bad payload
- `0x04` = invalid state
- `0x05` = safety rejected
- `0x06` = busy
- `0x07` = unclearable fault
