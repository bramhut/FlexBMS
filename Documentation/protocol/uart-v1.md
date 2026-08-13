# FlexBMS STM32--ESP32 UART v1 contract

This is the canonical byte-level contract for the isolated UART between the
STM32G491 BMS and ESP32-C3 Gateway. Implementations, tests, and any new
network-facing features must follow this document. The STM32 is the battery and
HV safety authority; the Gateway is never a safety authority.

## Link and framing

- USART1, isolated, 1,000,000 bit/s, 8-N-1, full duplex, no flow control.
- The legacy `*!` Companion protocol remains USB-only and is not carried over
  this UART.
- Each endpoint sends an empty `HEARTBEAT` every 500 ms. A peer is lost after
  1.5 s without a complete CRC-valid frame. Invalid bytes and invalid frames do
  not refresh that timer.
- All multi-byte values are little-endian. Encode/decode fields individually;
  never transmit native C/C++ structs. The maximum payload is 512 bytes.
- `SEQUENCE = 0` is reserved for heartbeat, telemetry, and events. A Gateway
  service request uses 1--255 and the STM32 response echoes it. The Gateway
  has one service request in flight at a time.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | `MAGIC = 0x46 0x42` (`FB`) |
| 2 | 1 | `VERSION = 0x01` |
| 3 | 1 | `TYPE` |
| 4 | 1 | `SEQUENCE` |
| 5 | 2 | `LENGTH`, payload `uint16` |
| 7 | n | `PAYLOAD` |
| 7+n | 4 | `CRC32`, `uint32` |

CRC is CRC-32/ISO-HDLC: polynomial `0x04C11DB7` (reflected
`0xEDB88320`), reflected input/output, initial value `0xFFFFFFFF`, final XOR
`0xFFFFFFFF`. It covers magic, header, and payload--not the CRC field--and the
transmitted CRC word is little-endian. The standard check is
`CRC32("123456789") = 0xCBF43926`.

The receiver rejects an invalid version, length, or CRC, discards the first
magic byte, and resumes magic scanning. There is no escaping or byte stuffing.

## Message types

| ID | Type | Direction |
|---:|---|---|
| `0x01` | `HEARTBEAT` | Both |
| `0x02` | `STATUS` | STM32 to Gateway |
| `0x03` | `PACK` | STM32 to Gateway |
| `0x04` | `CELL` | STM32 to Gateway |
| `0x05` | `TEMPERATURE` | STM32 to Gateway |
| `0x10` | `SERVICE_REQUEST` | Gateway to STM32 |
| `0x11` | `SERVICE_RESPONSE` | STM32 to Gateway |
| `0x12` | `EVENT` | STM32 to Gateway |

## Telemetry payloads

`HEARTBEAT` has an empty payload.

### `STATUS` — 17 bytes

```text
bms_state:u8 | hv_state:u8 | flags:u16 | slave_count:u8 |
bms_active_faults:u16 | bms_latched_faults:u16 |
hv_active_faults:u16 | hv_latched_faults:u16 | uptime_ms:u32
```

`flags`: bit 0 BMS HV-ready; bit 1 charging allowed; bit 2 run request
asserted; bit 3 complete measurements fresh; bit 4 Gateway peer alive; bits
5--15 are zero.

`slave_count` is the variable configured BMS monitor-chain count. A production
module has two monitor slaves, but a single-slave development chain is valid.
The Gateway must use the reported count and must not reject an odd count.

### `PACK` — 24 bytes

```text
pack_voltage_uV:u32 | pack_current_raw:i16 | soc_raw:u16 |
min_cell_uV:u32 | max_cell_uV:u32 |
min_ntc_raw:u16 | max_ntc_raw:u16 | min_ic_raw:u16 | max_ic_raw:u16
```

Voltages are microvolts. Current is signed amperes ×64; positive is charging.
`soc_raw` uses the existing BCC range (`0 = -100%`, `65535 = 200%`):
`percent = 100 * (soc_raw / 65535 * 3 - 1)`. NTC conversion is
`raw / 65535 * 120 - 20` °C. IC raw is decikelvin: `raw / 10 - 273.15` °C.

### `CELL` — 51 bytes per configured slave

```text
slave_index:u8 | balance_mask:u16 | cell_voltage_uV[12]:u32
```

`slave_index` is zero-based. `module_index = slave_index >> 1` and
`slave_in_module = slave_index & 1`. Balance bits 0--11 represent cells 0--11;
bits 12--15 are zero. Each slave has exactly 12 cells. With one development
slave, only index 0 is sent; it maps to module 0/slave 0 and does not imply a
partner frame.

### `TEMPERATURE` — 11 bytes per configured slave

```text
slave_index:u8 | ntc_raw[4]:u16 | ic_temp_raw:u16
```

Each packet includes four NTC readings and one IC reading. Both endpoint
implementations must enforce the four-NTC-per-slave configuration at compile
time.

The STM32 sends `STATUS` and a complete `PACK`/`CELL`/`TEMPERATURE` snapshot
after fresh measurement data, no more than once every 500 ms. It sends `EVENT`
immediately on change. The Gateway treats all measurement telemetry as invalid
while `STATUS` says measurements are not fresh.

## States, faults, and events

| BMS state | Value |
|---|---:|
| `DEVICE_INITIALIZATION` | 0 |
| `REGISTER_INITIALIZATION` | 1 |
| `PERFORMING_DIAGNOSTICS` | 2 |
| `RUNNING` | 3 |
| `PANIC` | 4 |

| HV state | Value |
|---|---:|
| `OFF` | 0 |
| `SELF_TEST` | 1 |
| `PRECHARGE` | 2 |
| `CONTACTOR_CLOSE` | 3 |
| `RUN` | 4 |

BMS fault bitmap bits 0--15, in order: `INVALID_CONFIG`, `TPL_FAULT`,
`CID_INITIALIZATION_FAULT`, `REGISTER_INITIALIZATION_FAULT`,
`CELL_BALANCING_FAULT`, `DIAGNOSTICS_FAULT`, `OVERVOLTAGE_LIMIT`,
`UNDERVOLTAGE_LIMIT`, `TEMPERATURE_LIMIT`, `OVERCURRENT_LIMIT`,
`IC_TEMPERATURE`, `SOC_LIMIT`, `OPEN_SHORT_FAULT`, `SYSTEM_FAULT`,
`COMMUNICATION_TIMEOUT`, `HV_SUPERVISOR_FAULT`.

HV reason bits 0--6, in order: `SENSOR_DIAGNOSTIC`, `USB_ONLY`,
`BATTERY_VOLTAGE_MISMATCH`, `LOAD_SIDE_ENERGIZED`, `PRECHARGE_TIMEOUT`,
`PRECHARGE_VOLTAGE_LOST`, `CONTACTOR_VOLTAGE_LOST`.

An active reason is present now. A latched reason remains blocking after its
live condition clears until the STM32 accepts a supported clear procedure and
revalidates the system.

`EVENT` is three bytes:

```text
event_id:u8 | value:u16
```

| ID | Event | `value` |
|---:|---|---|
| `0x01` | BMS state changed | New BMS state |
| `0x02` | HV state changed | New HV state |
| `0x03` | BMS active faults changed | New active mask |
| `0x04` | BMS latched faults changed | New latched mask |
| `0x05` | HV active reasons changed | New active mask |
| `0x06` | HV latched reasons changed | New latched mask |
| `0x07` | Measurement freshness changed | `0` or `1` |

`EVENT` is a convenience notification. `STATUS` is authoritative, so a lost
event does not change correctness.

## Services

Service request payload: `service_id:u8 | arguments...`.

Service response payload: `service_id:u8 | result:u8 | response_data...`.

| Result | Value | Meaning |
|---|---:|---|
| `OK` | 0 | STM32 accepted and invoked the request. |
| `DENIED` | 1 | Valid request cannot safely run in the current STM32 state. |
| `INVALID` | 2 | Unknown service, invalid length/argument, or nonexistent slave. |

`OK` means accepted and invoked; `STATUS` and `EVENT` show the resulting state.

| ID | Service | Request arguments | `OK` response data |
|---:|---|---|---|
| `0x01` | `GET_STATUS` | None | 17-byte `STATUS` |
| `0x02` | `SET_RUN_REQUEST` | `requested:u8` (`0` or `1`) | None |
| `0x03` | `CLEAR_FAULTS` | None | None |
| `0x04` | `READ_REGISTER` | `slave_index:u8, register:u8` | `slave_index:u8, register:u8, value:u16` |
| `0x05` | `SET_RTC` | `unix_time_s:u32` UTC | None |
| `0x06` | `GET_DEVICE_INFO` | None | `firmware_version:u32` |
| `0x07` | `ENTER_STM32_BOOTLOADER` | `firmware_version:u32, image_length:u32, image_crc32:u32` | None |

`SET_RUN_REQUEST(0)` immediately removes the request through the STM32 PCC
path. A request of 1 does not guarantee an HV start. `CLEAR_FAULTS` is denied
while run is requested or live HV conditions are unhealthy. The Gateway and
Home Assistant cannot bypass a fault, write BCC registers, or override the HV
supervisor.

`firmware_version` packs `major | (minor << 8) | (patch << 16) |
(build << 24)`.

### STM32 bootloader handoff

`ENTER_STM32_BOOTLOADER` is the only STM32-update operation. The Gateway must
already have staged and CRC-checked the image. The STM32 validates the request
shape and image length; it cannot verify staged bytes it never receives.

The STM32 returns `DENIED` unless run request is off and it can de-energise the
HV path and inhibit normal services. After `OK`, it drains that response, stops
framed UART, and enters the STM32 ROM bootloader. The Gateway then performs ROM
bootloader sync, transfer, readback verification, and `Go` on USART1; these are
not FlexBMS UART frames. On return, the Gateway waits for heartbeat and uses
`GET_DEVICE_INFO` to confirm the expected version. There are no update-data,
acknowledgement, retry, or status frame types.

## Test vectors

Complete frames below include the little-endian CRC.

```text
HEARTBEAT
46 42 01 01 00 00 00 8F 7A FB 7D

GET_STATUS, sequence 0x2A
46 42 01 10 2A 01 00 01 8F 21 B2 4B

SET_RUN_REQUEST(true), sequence 0x2B
46 42 01 10 2B 02 00 02 01 16 26 B1 DC

READ_REGISTER response, sequence 0x2C, slave 3, register 0x20, value 0x1234
46 42 01 11 2C 06 00 04 00 03 20 34 12 1A 45 F6 9C

ENTER_STM32_BOOTLOADER, sequence 0x2D, version 1.2.3 build 4,
image length 131072 bytes, image CRC32 0xA1B2C3D4
46 42 01 10 2D 0D 00 07 01 02 03 04 00 00 02 00 D4 C3 B2 A1 42 C9 9E 95
```
