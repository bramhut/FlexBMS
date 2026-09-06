# FlexBMS framed BMS protocol v1

This is the canonical byte-level contract for BMS telemetry and named services.
The STM32G491 uses the identical frames on the isolated UART to the ESP32-C3
Gateway and on direct USB CDC to the Web Serial Companion. Implementations,
tests, and any new network-facing features must follow this document. The
STM32 is the battery and HV safety authority; the Gateway is never a safety
authority.

## Link and framing

- Isolated UART: USART1, 1,000,000 bit/s, 8-N-1, full duplex, no flow control.
- Direct USB: USB CDC carries the same binary frames. Web Serial opens the CDC
  port at 115,200 bit/s; this setting does not change USB signalling.
- Each Companion link sends an empty `HEARTBEAT` every 500 ms. A peer is lost
  after 1.5 s without a complete CRC-valid frame. USB telemetry starts only
  after a valid USB heartbeat and stops at that timeout. Invalid bytes and
  invalid frames do not refresh either timer.
- USB CDC retains the trusted text command/debug console. The STM32 consumes
  complete `FB` frames before forwarding ordinary text to that console; text
  output may be interleaved with frames and framed decoders must resynchronise.
- The line-oriented `*!` BMS Companion protocol is retired and is not accepted
  as a supported BMS interface.
- All multi-byte values are little-endian. Encode/decode fields individually;
  never transmit native C/C++ structs. The maximum payload is 512 bytes.
- `SEQUENCE = 0` is reserved for heartbeat, telemetry, and events. A Gateway
  or direct-USB Companion service request uses 1--255 and the STM32 response
  echoes it. Each requester keeps at most one service request in flight.

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
| `0x02` | `STATUS` | STM32 to Gateway or direct USB Companion |
| `0x03` | `PACK` | STM32 to Gateway or direct USB Companion |
| `0x04` | `CELL` | STM32 to Gateway or direct USB Companion |
| `0x05` | `TEMPERATURE` | STM32 to Gateway or direct USB Companion |
| `0x06` | `HV_VOLTAGES` | STM32 to Gateway or direct USB Companion |
| `0x07` | `ENERGY` | STM32 to Gateway or direct USB Companion |
| `0x10` | `SERVICE_REQUEST` | Gateway or direct USB Companion to STM32 |
| `0x11` | `SERVICE_RESPONSE` | STM32 to Gateway or direct USB Companion |
| `0x12` | `EVENT` | STM32 to Gateway or direct USB Companion |

## Telemetry payloads

`HEARTBEAT` has an empty payload.

### `STATUS` — 33 bytes

```text
bms_state:u8 | hv_state:u8 | flags:u16 | slave_count:u8 |
bms_active_errors:u32 | bms_latched_errors:u32 |
hv_active_errors:u32 | hv_latched_errors:u32 |
warnings:u32 | uptime_ms:u32 | soc_last_calibration_unix_s:u32
```

`flags`: bit 0 BMS HV-ready; bit 1 charging allowed; bit 2 run request;
bit 3 complete measurements fresh; bit 4 isolated-UART Gateway peer alive;
bit 5 automatic balancing enabled; bit 6 SOC valid; bit 7 current sensing enabled; bit 8
`soc_last_calibration_unix_s` valid. Bits 9--15 are zero. USB heartbeats never
change bit 4.

`slave_count` is the variable configured BMS monitor-chain count. A production
module has eight monitor slaves, but a single-slave development chain is valid.
The Gateway must use the reported count and must not reject an odd count.

`uptime_ms` is the STM32 HAL millisecond tick since its most recent reset. It
is diagnostic telemetry only, resets to zero after a controller restart, and
wraps naturally after about 49.7 days. The STM32 sends STATUS at least every
500 ms while its UART service runs.

### `PACK` — 24 bytes

```text
pack_voltage_uV:u32 | pack_current_raw:i16 | soc_raw:u16 |
min_cell_uV:u32 | max_cell_uV:u32 |
min_ntc_raw:u16 | max_ntc_raw:u16 | min_ic_raw:u16 | max_ic_raw:u16
```

Voltages are microvolts. Current is signed amperes ×64; positive is charging.
`soc_raw` uses the existing BCC range (`0 = -100%`, `65535 = 200%`):
`percent = 100 * (soc_raw / 65535 * 3 - 1)`. The STM32 bounds retained and
reported SOC to 0--100%, so the wider transport representation is not used in
normal operation. NTC conversion is
`raw / 65535 * 120 - 20` °C. IC raw is centikelvin: `raw / 100 - 273.15` °C.

Only use `pack_current_raw` when STATUS bit 7 is set, and `soc_raw` when bit 6
is set. SOC is informational and never commands HV. The estimate is retained
in the CR2032-powered RTC backup domain with a version marker and checksum; an
erased or corrupt value is invalid. Current sensing is deliberately disabled in
the single-slave development configuration, so current and SOC are unavailable
there. Production enables it only on CID 1. Full-charge calibration requires
fresh measurements with no active BMS error, every cell at or above 3.450 V,
current from -0.100 A through C/50 (6.28 A for the 314 Ah default), continuously
for 300 s. Any failed condition restarts the timer; calibration sets SOC to
100%. Coulomb counting is bounded at 0% and 100%, allowing a subsequent
discharge to lower SOC normally without retaining an out-of-range value.

When calibration succeeds with valid STM32 RTC UTC, its `u32` Unix time is
stored with a marker and checksum in reserved backup registers BKP28--BKP30 and
reported in `soc_last_calibration_unix_s` with STATUS bit 8 set. A calibration
before UTC is valid is deliberately reported without a timestamp rather than
with an invented date.

### `HV_VOLTAGES` — 12 bytes

```text
valid_flags:u8 | reserved:u8[3] | bat_plus_uV:u32 | load_plus_uV:u32
```

Bit 0 of `valid_flags` means both values were obtained from one fresh,
coherent STM32 ADC/DMA scan and its ADC reference was valid. Bits 1--7 and the
three reserved bytes are zero. The values are positive microvolts measured by
the isolated AMC3330 battery-side (BAT+) and load-side (LOAD+) channels. When
the valid bit is clear, consumers must show the values as unavailable rather
than treating the zero payload as a measurement.

These diagnostic readings are sent with each normal fresh snapshot. They do
not change PCC thresholds, fault decisions, or STM32 safety ownership.

### `ENERGY` — 17 bytes

```text
flags:u8 | charged_energy_uWh:u64 | discharged_energy_uWh:u64
```

The counters are monotonic, saturating `uint64` totals in micro-watt-hours
(µWh). Positive signed pack current is charging and increments
`charged_energy_uWh`; negative current is discharging and increments
`discharged_energy_uWh`. A counter is integrated from pack voltage in µV,
current in the existing A×64 raw representation, and elapsed time between
complete valid measurements. Invalid measurements and unavailable current
sensing do not contribute energy. Bit 0 of `flags` means the STM32 backup
record is valid; bits 1--7 are reserved. The Gateway converts µWh to kWh for
MQTT/Home Assistant; UART counters remain exact.

The counters use one RTC backup-register record:

| Registers | Contents |
|---|---|
| BKP0–BKP3 | Reserved |
| BKP4–BKP7 | Existing SoC/Ah state |
| BKP8–BKP11 | Charged energy, 64-bit µWh |
| BKP12–BKP15 | Discharged energy, 64-bit µWh |
| BKP16 | Energy-record checksum |
| BKP17 | Energy-record marker/version |
| BKP18–BKP27 | Reserved |
| BKP28–BKP30 | Existing SoC calibration metadata |
| BKP31 | Existing RTC validity marker |

The STM32 writes both counters, then the checksum, and writes the marker last.
An invalid marker/checksum resets both counters to zero and creates a valid
record. This is deliberately a single-slot design; an interrupted update can
lose energy history while the remaining backup registers stay free.

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

The STM32 sends `STATUS` and a complete `HV_VOLTAGES`/`PACK`/`ENERGY`/`CELL`/`TEMPERATURE` snapshot
after fresh measurement data, no more than once every 500 ms. It sends `EVENT`
immediately on change. The Gateway treats all measurement telemetry as invalid
while `STATUS` says measurements are not fresh.

## States, faults, and events

| BMS state | Value |
|---|---:|
| `STARTING` | 0 |
| `READY` | 1 |
| `RUNNING` | 2 |
| `ERROR` | 3 |
| `CRITICAL` | 4 |

| HV state | Value |
|---|---:|
| `OFF` | 0 |
| `SELF_TEST` | 1 |
| `PRECHARGE` | 2 |
| `CONTACTOR_CLOSE` | 3 |
| `RUN` | 4 |

`CRITICAL` has no bitmap. It represents an unrecoverable condition in the
current software and forces HV safe-off for the remainder of that boot.

BMS ERROR bitmap bits are: 0 `CONFIGURATION_INVALID`, 1
`SLAVE_UNAVAILABLE`, 2 `BCC_DIAGNOSTICS`, 3 `CELL_VOLTAGE_LIMIT`, 4
`THERMAL_LIMIT`, 5 `CURRENT_LIMIT`, 6 `BCC_INTEGRITY`, 7 `ADC_FAULT`, 8
`BALANCING_HARDWARE_FAULT`, 9 `BCC_COMMUNICATION`, and 10 `NO_CONFIG`. Bits
11--31 are reserved and zero. `NO_CONFIG` means that no blank or
version-compatible runtime configuration is available; the STM32 remains in
`CRITICAL` and safe-off, while configuration services remain available.

HV ERROR bitmap bits are: 0 `HV_SENSOR_DIAGNOSTIC`, 1
`BATTERY_VOLTAGE_MISMATCH`, 2 `LOAD_SIDE_ENERGISED`, 3 `PRECHARGE_TIMEOUT`, 4
`PRECHARGE_VOLTAGE_LOST`, and 5 `CONTACTOR_VOLTAGE_LOST`. Bits 6--31 are
reserved and zero.

`warnings` is non-latched. Bits are: 0 `WATCHDOG_RESET`, 1
`STARTUP_DIAGNOSTICS_BYPASSED`, and 2 `BATTERY_VOLTAGE_MISMATCH_OFF`.
Bits 3--31 are reserved and zero. `WATCHDOG_RESET` is a recorded warning that
an accepted acknowledgement clears. `STARTUP_DIAGNOSTICS_BYPASSED` remains
present while startup diagnostics are disabled in runtime configuration; `BATTERY_VOLTAGE_MISMATCH_OFF`
remains present while the measurements disagree.
`WATCHDOG_RESET` is set when this STM32 boot followed an independent or window
watchdog reset. Gateway liveness, CAN condition, near-limit indication, and
disabled automatic balancing are not BMS warnings in this release.

The STM32 compares BAT+ and the slave-reported pack voltage continuously. A
disagreement is an OFF-state warning while no run request is present; a run
request promotes the same condition to the blocking
`BATTERY_VOLTAGE_MISMATCH` HV error. Releasing the request deasserts that
active HV error, retains its latch for acknowledgement, and restores the
warning until the readings agree.

An active ERROR is present now. A latched ERROR remains blocking after its
live condition clears until the STM32 accepts acknowledgement. The STM32 Fault
Manager is the sole owner of active/latched aggregation, acknowledgement, and
immediate HV safe-off. Detection modules may only assert or deassert their
assigned condition.

Startup monitor-chain, register-initialisation, and diagnostics attempts use
their source retry budget before asserting their ERROR input. Once exhausted,
they report and clear through these ordinary ERROR semantics; HV remains `OFF`
until acknowledgement permits another start.

A live TPL/BCC communication loss immediately safe-offs HV and re-enters the
same non-blocking TPL, CID, register, and measurement initialisation path. A
successful complete measurement and fault-status cycle deasserts the active
communication error; its latched error still requires acknowledgement.

The STM32 independent watchdog starts after the required clock and DMA setup,
before every fallible peripheral initialisation, and has a nominal 500 ms
timeout. It is refreshed every 100 ms only when both the PCC loop and BCC task
have progressed; BCC startup retry waits are non-blocking. A subsequent
platform-initialisation failure directly drives the precharge output and
contactor PWM safe before the watchdog reset, with the contactor-driver
pull-down as the hardware fallback. A clock-initialisation failure may occur
before the watchdog is available; the hardware pull-down still holds the
contactor control line safe.

The STM32 ADC is calibrated at startup. TIM7 triggers a three-rank HV scan at
50 Hz: load-side differential voltage, battery-side differential voltage, and
VREFINT. Each rank uses 640.5 ADC clock cycles of acquisition and 256x
oversampling with right-shift 4. DMA publishes complete scans as one snapshot.
The STM32 drives VREF+ from its 2.9 V internal reference buffer. ADC/DMA
errors and a 2.8--3.0 V ADC reference are checked every 20 ms. DMA progress is
stale only after no completed scan for 100 ms. Three consecutive failed checks
assert `ADC_FAULT` and immediately safe-off HV. Calibration is not repeated
during normal operation.

`EVENT` is five bytes:

```text
event_id:u8 | value:u32
```

| ID | Event | `value` |
|---:|---|---|
| `0x01` | BMS state changed | New BMS state |
| `0x02` | HV state changed | New HV state |
| `0x03` | BMS active errors changed | New active mask |
| `0x04` | BMS latched errors changed | New latched mask |
| `0x05` | HV active errors changed | New active mask |
| `0x06` | HV latched errors changed | New latched mask |
| `0x07` | Warnings changed | New warning mask |
| `0x08` | Measurement freshness changed | `0` or `1` |

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
| `BUSY` | 3 | Another named BMS service is still in progress. |
| `USB_HOST_ACTIVE` | 4 | A USB host has enumerated the STM32; it cannot enter the UART ROM bootloader. |

`OK` means accepted and invoked; `STATUS` and `EVENT` show the resulting state.
Only one named service is active across UART and USB at a time. The STM32
returns `BUSY` to the other link and routes delayed responses, such as register
reads, to the link and sequence that originated them.

| ID | Service | Request arguments | `OK` response data |
|---:|---|---|---|
| `0x01` | `GET_STATUS` | None | 33-byte `STATUS` |
| `0x02` | `SET_RUN_REQUEST` | `requested:u8` (`0` or `1`) | None |
| `0x03` | `ACKNOWLEDGE_FAULTS` | None | None |
| `0x04` | `READ_REGISTER` | `slave_index:u8, register:u8` | `slave_index:u8, register:u8, value:u16` |
| `0x05` | `SET_RTC` | `unix_time_s:u32` UTC, 2000--2099 | None |
| `0x06` | `GET_DEVICE_INFO` | None | `firmware_version:u32` |
| `0x07` | `PREPARE_STM32_BOOTLOADER` | `firmware_version:u32, image_length:u32, image_crc32:u32` | None |
| `0x08` | `GET_RTC` | None | `unix_time_s:u32` UTC |
| `0x09` | `SET_BALANCING_ENABLED` | `enabled:u8` (`0` or `1`) | None |
| `0x0A` | `COMMIT_STM32_BOOTLOADER` | None | None (successful request enters ROM immediately) |
| `0x0B` | `GET_CONFIG` | None | 18-byte runtime configuration status and values |
| `0x0C` | `SET_CONFIG` | `slave_count:u8, current_sense_slave:u8, shunt_resistance_uohm:u32, battery_capacity_mah:u32, invert_current:u8, balance_enabled:u8, startup_diagnostics:u8` | None; the STM32 resets after responding `OK` |
| `0x0D` | `GET_DIAGNOSTIC_REPORT` | `slave_index:u8` | Latest startup diagnostic result for that BCC |

The STM32 calendar stores UTC only. It accepts `SET_RTC` only for 2000--2099
and returns `INVALID` for another timestamp or `DENIED` if the hardware write
fails. `GET_RTC` returns `DENIED` until a successful set has marked the backup
domain valid. The Gateway is the normal setter: it obtains NTP after station
connection and forwards the result through this same service slot.

`SET_RUN_REQUEST(0)` immediately removes the request through the STM32 PCC
path. A request of 1 does not guarantee an HV start. `ACKNOWLEDGE_FAULTS` is
denied while any ERROR condition remains active or the BMS is `CRITICAL`. It
is also denied when no inactive BMS/HV ERROR latch or recorded `WATCHDOG_RESET`
warning remains to clear. Accepted acknowledgement clears those states only,
leaving live and configuration warnings visible until their underlying
condition changes; it deliberately preserves the run request. PCC can
therefore restart from `OFF` immediately once the Fault Manager permits it.
The Gateway and Home Assistant cannot bypass a fault, write BCC registers, or
override the HV supervisor.

`SET_BALANCING_ENABLED` controls the STM32-owned automatic balancing enable
gate and persists the value in runtime configuration. It applies immediately
after the flash write succeeds and does not reboot the STM32. An enabled value
does not guarantee that any cell is balancing: the STM32 still requires a
running, fault-free BMS, fresh measurements, and its configured cell-voltage
thresholds. A disabled value inhibits the BCC balancing drivers on the next
BCC loop. New configuration defaults enable balancing.

`GET_CONFIG` returns `reason:u8` (`0` valid, `1` blank, `2` version mismatch,
`3` corrupt), `expected_version:u16`, `stored_version:u16`, followed by
`slave_count:u8`, `current_sense_slave:u8` (`0` means none),
`shunt_resistance_uohm:u32`, `battery_capacity_mah:u32`, and
`invert_current:u8`, `balance_enabled:u8`, and `startup_diagnostics:u8`. When no usable record exists, the value fields contain
the compile-time factory defaults for editing and resubmission. The STM32
accepts 1--32 slaves. The wire format remains milliamp-hours; Companion
displays and edits this value in amp-hours with 0.001 Ah precision. The
configuration schema version is currently 3. Version-2 records are accepted
with startup diagnostics enabled as the safe legacy default and are upgraded
to version 3 when next saved through `SET_CONFIG`.

`startup_diagnostics` defaults to 1. Setting it to 0 skips only the startup
BCC diagnostics routine for debugging or bench testing; the STM32 continues
to enforce all other measurement, fault, balancing, and HV safety checks.

`GET_DIAGNOSTIC_REPORT` returns `slave_index:u8`, `cid:u8`,
`failed_checks:u16`, `status_code:u8`, and `failed_diagnostic:u8`. The twelve
`failed_checks` bits correspond, in order, to `ADC1VER`, `OVUVVER`, `OVUVDET`,
`CTXOPEN`, `CELLVOLT`, `CONNRES`, `CTXLEAK`, `CURRMEAS`, `SHUNTNOTCONN`,
`GPIOXOTUT`, `GPIOXOPEN`, and `CBXOPEN`. A non-zero `status_code` identifies a
BCC communication or diagnostic-driver failure; `failed_diagnostic` identifies
the check that was executing when it occurred. The report is held in RAM until
the next startup diagnostic run.

`SET_CONFIG` is accepted only while the HV system is safely off. The STM32
validates the complete candidate, stores it in its reserved dual-slot FLASH
area, and applies it after reset. Runtime configuration is separate from the
SoC/Ah counter and RTC metadata retained in the backup domain. The final 4 KiB
of STM32 application FLASH is reserved for the two configuration slots and
must not be erased by firmware update tooling.

`firmware_version` packs `major | (minor << 8) | (patch << 16) |
(build << 24)`.

### STM32 bootloader handoff

`PREPARE_STM32_BOOTLOADER` and `COMMIT_STM32_BOOTLOADER` form the STM32-update
handoff. The Gateway must already have staged and CRC-checked the image. The
STM32 validates the prepare request shape and image length; it cannot verify
staged bytes it never receives.

The STM32 returns `DENIED` unless run request is off and it can de-energise the
HV path. It returns `USB_HOST_ACTIVE` if its USB CDC device is enumerated by a
host. `PREPARE_STM32_BOOTLOADER` immediately holds HV safely off and blocks
normal services, then returns `OK`. Its prepared state expires after 3 seconds
unless the Gateway sends `COMMIT_STM32_BOOTLOADER`. The commit has no response:
after successfully transmitting it, the Gateway owns USART1 and immediately
changes to ROM settings. If prepare or commit transmission fails, the STM32
remains in (or returns to) normal BMS operation; the Gateway reports the failure
instead of waiting indefinitely. It then performs ROM
bootloader sync, transfer, readback verification, and `Go` on USART1; these are
not FlexBMS UART frames. On return, the Gateway waits for an application
heartbeat before reporting completion. Any failure after erase has
begun requires wired STM32 recovery; the Gateway rejects further STM32 OTA
attempts until it observes an application heartbeat. There are no update-data,
retry, or status frame types in UART v1.

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

PREPARE_STM32_BOOTLOADER, sequence 0x2D, version 1.2.3 build 4,
image length 131072 bytes, image CRC32 0xA1B2C3D4
46 42 01 10 2D 0D 00 07 01 02 03 04 00 00 02 00 D4 C3 B2 A1 42 C9 9E 95

COMMIT_STM32_BOOTLOADER, sequence 0x2E
46 42 01 10 2E 01 00 0A 50 6F 02 53
```
