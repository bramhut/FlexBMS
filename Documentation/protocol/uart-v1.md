# FlexBMS framed BMS protocol v1

This is the canonical byte-level contract for BMS telemetry and named services.
The STM32G491 uses the identical frames on the isolated UART to the ESP32-C3
Gateway and on direct USB CDC to the Web Serial Companion. Implementations,
tests, and any new network-facing features must follow this document. The
STM32 is the battery and HV safety authority; the Gateway is never a safety
authority.

## Gateway delivery and Companion requests

### Monitor-IC thermal fault

The BCC status filter preserves FAULT2.IC_TSD_FLT for the dedicated STM32
IC-temperature/latched thermal fault path. Only the generic integrity-fault
classification excludes that bit to avoid duplicating the same event; any
simultaneous integrity faults remain effective. Hardware IC thermal shutdown
and existing hard cell-temperature limits are unchanged. No software IC-temperature
derating is added.

### Cell-temperature limits and inverter enforcement

See [the temperature policy](../architecture/temperature-derating-plan.md) for
the approved curves and validation status. Cold charge limits use the minimum
busbar NTC minus the configured allowance; hot limits use the maximum unadjusted
NTC. Capacity-scaled C-rate limits combine with voltage limits and the system
ceiling before recovery slew. Hardware UT flags no longer impose a separate
fixed 5 degC charging cutoff. Independent 60 degC cell and IC thermal trips remain.

With HV in RUN, STM32 compares fresh signed pack current against the final
0.1 A downward-quantized directional allowance. A violation lasting 5 s latches
`INVERTER_CURRENT_LIMIT`, forces HV safe-off and clears the run request. At zero
allowance the tolerance is 1 A; otherwise it is max(2 A, 10% of allowance).
Changing limits do not reset a continuing violation. Missing samples do not
accrue dwell or count as compliance. Existing immediate absolute overcurrent
protection is independent. The last violation is diagnostic RAM state, not a
persistent record, and remains available after fault acknowledgement.

### Balancing current qualification

New balancing pulses require 30 continuous seconds of valid measurements with
pack current between -0.100 A and C/50 (6.28 A at 314 Ah). An excursion resets
qualification but allows an already-running 30-second pulse to finish. The next
pulse waits for qualification again; continuously qualifying current does not
add a delay between pulses. BMS safety, freshness, fault and enable checks remain
independent and can stop balancing immediately. Failed measurement/current-fault
reads and monitor reinitialization reset qualification. Voltage selection remains
3.400 V minimum, 10 mV start difference and 5 mV stop difference.

### Slave current-measurement load matching

STM32 static setting `EQUALIZE_CURRENT_MEASUREMENT_LOAD` in `bcc/UserSettings.h`
defaults to true. When a current-sensing slave is configured, initialization
enables `SYS_CFG1.I_MEAS_EN` on every slave and checks its readback. The same
policy applies on reinitialization. Selecting no current sensor disables the
measurement circuit on all slaves. ADC conversion settings remain common.

Only the configured shunt-connected slave retains `CURRENT_SENSING_ENABLED`:
it alone supplies pack current, current protections, Ah/SoC integration and
current/shunt diagnostics. Other slaves' unused ISENSE inputs are assumed
shorted together to their local IC ground; their current results are not used.
No extra SoC backup registers or persistent settings are introduced. Existing
monitor-current compensation, self-discharge and balancing accounting are
unchanged; series slave count does not multiply the pack-Ah deduction.

This matches measurement-circuit operating modes, not all board consumption.
It increases electronics consumption and does not immediately remove an
existing imbalance. Hardware validation: check enable-bit readback, confirm
unchanged pack-current behavior, then compare per-slave incremental balancing
mAh over comparable intervals after a balanced full charge. “None” and a
non-first current-source selection should also be checked on a safe bench.

Gateway Wi-Fi station connections use all-channel scanning and RSSI ordering
within the selected SSID, on initial connection and every retry. Stored-network
priority and fallback-network ordering are unchanged. No AP BSSID is pinned;
this is connection selection, not proactive roaming of an established link.
Gateway status includes nullable `wifi_connection` (BSSID, channel, `rssi_dbm`)
for the current station connection. Companion displays this status sample;
it is not a continuous signal-strength measurement. Serial logs record the AP
on IP acquisition and BSSID/RSSI/reason on disconnect. OTA verification and
rollback timing are unchanged.

The Gateway WebSocket link distinguishes replies/events from replaceable state.
Each client has a bounded 16-message FIFO for replies/events, prioritized over
pending telemetry, plus one latest-value slot each for `hello`, `gateway_status`,
`bms_status`, and `snapshot`. State slots are serviced round-robin. An in-progress
send is never replaced. FIFO saturation explicitly retires the connection rather
than silently discarding an acknowledgement. Slow clients do not block UART input.

Gateway Companion serializes STM32 service requests (at most 16 outstanding
locally). Read-only requests retry explicit `busy` responses at most three times,
after 250, 500, and 750 ms. Commands and ambiguous transport failures are not
automatically retried. Queued requests belong to the original connection and
are not replayed on reconnect. Other clients/internal services can still cause
bounded Busy failures. Per-attempt timeouts start when a request is sent, not
when it is queued.

Configuration remains unknown until successfully read. Failed reads must not
claim blank FLASH or permit saving placeholder values. A confirmed blank/corrupt/
unsupported record may still be replaced using the normal configuration workflow.
These changes do not alter the STM32 UART wire format or persisted configuration.

## Link and framing

Current integration has no deadband: valid coulomb-counter increments contribute
to Ah at all current magnitudes. Polarity correction, active-monitor consumption
compensation, retained-counter validity and 0–200% SoC bounds are unchanged.
Internal current used for energy, calibration and control is not zeroed near
idle. Self-discharge remains a separate model. Companion alone formats currents
below 15 mA as zero; raw telemetry and derived power are not filtered. Current
telemetry uses signed microamps, independently of the finer charge integration.
Removing the deadband does not correct sensor DC offset.

Counter acquisition reads fresh latched counters at each current-processing pass,
with a timestamp taken at acquisition rather than at later publication. The first
reading after startup/reset/diagnostics establishes a baseline. Unsigned modular
subtraction handles counter/time wrap without signed overflow. Reject intervals
over 1 s, zero elapsed time with changed counters, zero sample progress, over
8000 samples, or impossible accumulator deltas. At 0.6 uV/LSB and the specified
150 mV input range, 8000 samples bound the accumulator delta below 2^31. The
16-bit ADC configuration is required for the 1 s sample-wrap bound. See NXP
[MC33771C datasheet, sections 8.4 and 9.7](https://www.nxp.com/docs/en/data-sheet/MC33771C.pdf).
Rejected intervals rebaseline and increment a RAM diagnostic counter, logged
with CID. Their unknown Ah is omitted (not estimated); the retained SoC anchor
is preserved. Instantaneous current still serves protection while rebaselining,
but energy integration does not bridge these invalid intervals.

Energy accounting uses uV, uA and microseconds with exact integer quotient and
remainder arithmetic, avoiding overflow of a naive three-factor product. The
existing charged/discharged uWh totals and RTC record remain compatible; the
new fractional remainder is RAM-only, with less than 1 uWh lost per restart per
direction. No additional flash writes, self-discharge tuning or offset learning
are introduced. GoodWe conversion remains at its existing CAN boundary.

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
| `0x08` | `GOODWE_CAN_DIAGNOSTICS` | STM32 to Gateway or direct USB Companion |
| `0x09` | `SOC_CALIBRATION` | STM32 to Gateway or direct USB Companion |
| `0x0A` | `BALANCING_CHARGE` | STM32 to Gateway or direct USB Companion |
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
`soc_last_calibration_unix_s` valid; bit 9 reset transfer breadcrumb valid;
bit 10 watchdog diagnostic valid. Bits 11--15 are zero. USB heartbeats never
change bit 4.

`slave_count` is the variable configured BMS monitor-chain count. A production
module has eight monitor slaves, but a single-slave development chain is valid.
The Gateway must use the reported count and must not reject an odd count.

`uptime_ms` is the STM32 HAL millisecond tick since its most recent reset. It
is diagnostic telemetry only, resets to zero after a controller restart, and
wraps naturally after about 49.7 days. The STM32 sends STATUS at least every
500 ms while its UART service runs.

### `PACK` — schema 2, 27 bytes

```text
schema_version:u8 (=2) | pack_voltage_uV:u32 | pack_current_uA:i32 | soc_raw:u16 |
min_cell_uV:u32 | max_cell_uV:u32 |
min_ntc_raw:u16 | max_ntc_raw:u16 | min_ic_raw:u16 | max_ic_raw:u16
```

Voltages are microvolts. Current is signed microamps; positive is charging.
Legacy 24-byte payloads and unknown schema versions are rejected. Outer framing
and service messages remain version 1 so firmware handoff remains possible
during a coordinated upgrade. Update both STM32 and Gateway; mixed versions do
not provide compatible PACK telemetry. Gateway JSON uses `pack_current_uA`;
Companion rejects snapshots lacking it. MQTT keeps its amperes-valued entity.
STM32 rounds/saturates at the i32 conversion boundary, with no change to Ah
integration precision. Companion display rounding is independent of telemetry.
`soc_raw` uses the existing BCC range (`0 = -100%`, `65535 = 200%`):
`percent = 100 * (soc_raw / 65535 * 3 - 1)`. The retained estimate and
informational telemetry can reach 200%; the inverter-facing integer is capped
at 99% until a full-charge calibration succeeds, then at 100%. NTC conversion is
`raw / 65535 * 120 - 20` °C. IC raw is centikelvin: `raw / 100 - 273.15` °C.

Only use `pack_current_uA` when STATUS bit 7 is set, and `soc_raw` when bit 6
is set. SOC is informational and never commands HV. The estimate is retained
in the CR2032-powered RTC backup domain with a version marker and checksum; an
erased or corrupt value is invalid. The configured current-sensing slave is the
only authoritative pack-current source. Enabling the current-measurement hardware
on the other slaves equalizes monitor consumption; their readings are not summed
or averaged into pack current. Full-charge calibration requires
fresh measurements with no active BMS error, every cell at or above 3.450 V,
current from -0.100 A through C/50 (6.28 A for the 314 Ah default), continuously
for 300 s. Any failed condition restarts the timer; calibration sets SOC to
100%. Coulomb counting is bounded at 0% and the 200% transport limit, preserving
over-100% drift until the next calibration. Full-charge confirmation is cleared
when the retained estimate falls below 99.5%; a new uninterrupted qualification
period is then required before the inverter receives 100% again. Confirmation
is deliberately not retained across STM32 restarts.

When calibration succeeds with valid STM32 RTC UTC, its `u32` Unix time is
stored with a marker and checksum in reserved backup registers BKP28--BKP30 and
reported in `soc_last_calibration_unix_s` with STATUS bit 8 set. A calibration
before UTC is valid is deliberately reported without a timestamp rather than
with an invented date.

### `GOODWE_CAN_DIAGNOSTICS` — schema 2, 167 bytes

Schema 2 retains the 156-byte schema-1 layout with byte 0 set to 2. Receivers
also accept schema 1 with exactly 156 bytes. Unknown schemas, incorrect lengths
and event directions greater than 2 are rejected.

The shared prefix contains 40 header bytes, seven 8-byte transmit-frame records
and three 20-byte receive-frame records:

```text
schema:u8 | protocol:u8 | enabled_flags:u8 | reserved:u8 |
tx_cycles:u32 | unavailable_cycles:u32 | tx_failures:u32 |
last_failure_ms:u32 | last_failure_id:u16 | reported_current_deciA:i16 |
reported_voltage_deciV:u16 | tx_error_count:u8 | rx_error_count:u8 |
error_logging_count:u8 | last_error_code:u8 | activity:u8 | bus_flags:u8 |
hal_error_code:u32 | tx_fifo_free:u32
```

Enabled flag bits 0/1 are request 0x45A and compatibility 0x460. Bus flag bits
0/1/2 are error-passive, warning and bus-off. Transmit records contain
`success_count:u32 | last_success_ms:u32`; receive records contain
`count:u32 | last_seen_ms:u32 | length:u8 | data:u8[8] | reserved:u8[3]`.
Record identifiers/order remain those defined by the GoodWe codec.

Schema 2 appends:

| Offset | Field | Meaning |
| --- | --- | --- |
| 156 | `violation_direction:u8` | 0 none, 1 charge, 2 discharge |
| 157 | `violation_current_ma:i32` | Signed measured current; positive charging |
| 161 | `violation_limit_deci_a:u16` | Enforced directional allowance, magnitude |
| 163 | `violation_uptime_ms:u32` | STM32 uptime at the event; wraps after about 49.7 days |

The Gateway exposes these four names in `goodwe_can` diagnostics; direct USB
uses the same fields. Companion displays the last event only when direction
is nonzero. It does not interpret a legacy schema's missing event as a fault.

### `SOC_CALIBRATION` — 16-byte legacy or 34-byte current schema

```text
schema:u8 (=2) | flags:u8 | pre_soc_raw:u16 | unix_time_s:u32 |
previous_unix_time_s:u32 | qualifying_dwell_ms:u32 |
self_discharge_rate_tenth_percent_per_30_days:u8 | reserved:u8 |
self_discharge_equivalent_current_uA:u32 |
self_discharge_accumulated_since_calibration_uAh:u64 |
last_calibration_self_discharge_mAh:u32
```

Schema 1 remains accepted for older transmitters. In schema 2, flag bit 0 says
the calibration record is valid; bit 1 says its pre-calibration SoC is valid;
bit 2 says the current SoC counter is valid; bit 3 says the live modeled-loss
interval has been complete since the last calibration; bit 4 says the saved
self-discharge amount for the last calibration is available; and bit 5 says
that saved interval was complete. Bits 6--7 and the reserved byte are zero.
Zero Unix times mean the corresponding RTC time was unavailable. The applied
SoC correction is `100 - percent(pre_soc_raw)`. The live self-discharge amount
is RAM-only since the last calibration and is marked incomplete after an STM32
restart, because powered-off time and the lost pre-restart total are not
estimated. When the SoC counter is invalid, no loss is integrated; Companion
shows the interval amount as unavailable.

Self-discharge configuration is a percentage of configured nominal capacity
per 30 days, encoded in tenths of a percent (`0..50`, where zero disables it).
The equivalent current is rounded to the nearest µA. STM32 uses integer
fixed-point arithmetic:

```text
numerator = capacity_mAh × rate_tenths × elapsed_ms + carried_remainder
loss_uAh = numerator / 2,592,000,000
carried_remainder = numerator % 2,592,000,000
```

The division remainder makes accumulated whole-µAh loss exactly invariant to
loop segmentation. Under the validated maximum capacity of 10,000,000 mAh,
maximum rate of 50, and maximum uint32 millisecond interval, the product plus
remainder fits uint64. Each whole µAh is applied directly to the existing RTC
retained Ah counter; conversion to that legacy double counter has a separately
bounded rounding error. The model runs on the task loop even through BCC
measurement/communication failures, as long as SoC remains valid. It runs
while charging, discharging, or idle, follows existing SoC bounds and
calibration, and does not change reported current, power, or energy.

The live interval total and fractional division remainder are RAM-only. A
restart clears them and marks the interval incomplete; there is no power-off
catch-up. At calibration, the amount is rounded to the nearest mAh and stored
alongside the previous calibration detail in BKP26, protected by the extended
checksum. The old SCD1 marker/checksum remains readable. New SCD2/SCD3 markers
identify complete/incomplete extended records; BKP27 remains reserved. The
Companion configuration rate is stored in the existing dual-slot STM32 flash
configuration record, not periodically written.

The calibration portion of schema 2 retains the schema 1 fields: flag bit 0
means the backup record is valid; bit 1 means the pre-calibration SoC estimate
is valid. Zero Unix times mean the corresponding RTC time was unavailable. If
both times are valid, their difference is the time since the previous
calibration. The dwell field is the actual continuous qualifying time before
the most recent calibration. Legacy schema-1 frames remain accepted.

### `BALANCING_CHARGE` — 2 + 4 × cell_count bytes

```text
slave_index:u8 | cell_count:u8 | estimated_balancing_mAh:u32[cell_count]
```

The payload contains one cumulative estimated balancing charge value per cell
for that slave, in milliamp-hours, with up to 14 cells. It is diagnostic data:
it does not modify SoC, is not persisted, and resets when the STM32 restarts.
The estimate uses the most recent measured cell voltage and the existing
22.8 Ω nominal effective bleed-path resistance. Charge accrues for confirmed
enabled intervals. The STM32 excludes measurement pauses and stops accrual at
the configured pulse timeout or a successful balancing disable. Intervals
around failed or uncertain commands are omitted. Sequential command boundaries
are timestamped at millisecond resolution and can slightly undercount on-time;
this is an estimate, not a physical current measurement. Fractional mAh is
retained internally; the transmitted value is the whole-mAh total. In the
Gateway WebSocket `snapshot`, each `cells[]` entry may include an optional
`balancing_mAh` array indexed like that slave's cells. Receivers treat this
field as optional and never delay ordinary snapshots while waiting for it.

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
| BKP18–BKP19 | Reset diagnostics |
| BKP20–BKP25 | Most recent SoC calibration detail record |
| BKP26 | Modeled self-discharge at the last calibration |
| BKP27 | Reserved |
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
partner frame. `balance_mask` reports the cells selected for the current
balancing pulse after the BCC driver-status check succeeds. It is deliberately
stable across brief measurement pauses and therefore does not represent the
instantaneous state of the bleed switches.

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
`BALANCING_HARDWARE_FAULT`, 9 `BCC_COMMUNICATION`, 10 `NO_CONFIG`, and
11 `INVERTER_CURRENT_LIMIT`. Bits 12--31 are reserved and zero. `NO_CONFIG` means that no blank or
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
| `0x0B` | `GET_CONFIG` | None | 20-byte runtime configuration status and values |
| `0x0C` | `SET_CONFIG` | `slave_count:u8, current_sense_slave:u8, shunt_resistance_uohm:u32, battery_capacity_mah:u32, invert_current:u8, balance_enabled:u8, startup_diagnostics:u8, self_discharge_tenth_percent_per_30_days:u8, cold_allowance_deci_c:u8` | None; the STM32 resets after responding `OK` |
| `0x0D` | `GET_DIAGNOSTIC_REPORT` | `slave_index:u8` | Latest startup diagnostic result for that BCC |
| `0x0E` | `GET_SAFETY_LIMITS` | None | Versioned read-only safety policy; 174 data bytes in schema 1 |

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
`invert_current:u8`, `balance_enabled:u8`, `startup_diagnostics:u8`, and
`self_discharge_tenth_percent_per_30_days:u8`, and `cold_allowance_deci_c:u8`.
Legacy 18-byte responses default the rate to zero; both 18/19-byte responses
default the allowance to 30 (3 degC). When no usable record exists, the value fields contain
the compile-time factory defaults for editing and resubmission. The STM32
accepts 1--32 slaves. The wire format remains milliamp-hours; Companion
displays and edits this value in amp-hours with 0.001 Ah precision. The
configuration schema version is currently 5. Version-2 records are accepted
with startup diagnostics enabled as the safe legacy default; version-2 and
version-3 records load self-discharge as zero. Versions 2-4 load cold allowance
as 30; version 4 retains its self-discharge rate. They migrate to version 5 on
the next explicit save, without boot-time flash writes or changes to slot size.
Rate values are 0--50 in 0.1% steps per 30 days, default zero. Cold allowance
is 0--100 in 0.1 degC steps, default 30, stored in reserved-word bits 16--23.
Legacy `SET_CONFIG` requests with 13 or 14 argument bytes retain omitted values
from the current stored configuration (or defaults when no valid record exists).
Current requests have 15 argument bytes, excluding the service ID. `GET_CONFIG`
responses have 22 payload bytes including service ID and result. Both transports
and the Gateway API carry `cold_allowance_deci_c` unchanged as an integer.

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

## Read-only safety policy (GET_SAFETY_LIMITS, 0x0E)

The request contains only the service ID. Success data is
`schema:u8 (=1) | field_count:u8 (=43) | values:i32[43]`, little-endian,
174 data bytes / 176 complete service-response payload bytes. Fields have the
fixed order below. Unknown schema, count or length must not be presented as
valid policy. There are no request-side settings and no flash write.

```text
 0 capacity_mah
 1 cold_allowance_deci_c
 2 charge_derate_mv
 3 charge_stop_mv
 4 discharge_derate_mv
 5 discharge_stop_mv
 6 cell_ov_mv
 7 cell_uv_mv
 8 cell_ot_deci_c
 9 charge_max_ma
10 discharge_max_ma
11 recovery_permille_per_s
12 cold_stop_deci_c
13 cold_recover_deci_c
14 cold_t0_deci_c
15 cold_c0_permille
16 cold_t1_deci_c
17 cold_c1_permille
18 cold_t2_deci_c
19 cold_c2_permille
20 cold_t3_deci_c
21 cold_c3_permille
22 hot_start_deci_c
23 hot_stop_deci_c
24 hot_recover_deci_c
25 hot_c_permille
26 zero_tolerance_ma
27 limit_margin_ma
28 limit_margin_permille
29 violation_dwell_ms
30 bal_min_mv
31 bal_start_delta_mv
32 bal_stop_delta_mv
33 bal_min_ma
34 bal_max_c_permille
35 bal_qualify_ms
36 bal_pulse_ms
37 bal_max_cells
38 communication_timeout_ms
39 firmware_version
40 configuration_valid
41 cell_ov_programmed_uv
42 cell_uv_programmed_uv
```

Suffixes specify integer units: mAh, deci-degrees Celsius, millivolts,
microvolts, milliamps, milliseconds and permille. C-rate permille 20 is 0.020C;
recovery permille 100 is 10% of the directional system ceiling per second.
Firmware version uses the existing packed version representation.
`configuration_valid` means the runtime configuration was accepted at startup,
not that HV is enabled or all measurements/hardware are healthy.

Values originate from active STM32 static/runtime settings and the constants
used by the thermal, current-limit and balancing controllers. No client table
supplies operational defaults. Hardware voltage fields include BCC 19.5 mV
quantization of the programmed threshold; these are calculated initialization
values, not a register readback. Cell OT remains the nominal thermistor threshold.
Schema 1 also specifies IC protection as the hardware IC thermal-shutdown fault,
without an additional software IC-temperature threshold; cold-discharge derating
is absent. Policy semantics must be versioned if changed incompatibly.

Gateway service name: `get_safety_limits`, arguments: `{}`.
Success data: `{"safety_limits_payload":[...174 bytes...]}`. The Gateway forwards
opaque bytes rather than maintaining a second policy decoder; Companion uses
the same strict decoder for Gateway and direct USB. The response-copy buffer
supports all 174 bytes. Unsupported firmware returns its normal service error;
Companion shows unavailable, not assumed settings.

Companion fetches on first opening Safety limits, caches within the mounted
Diagnostics view, and invalidates on disconnect, controller restart, transport
change or view destruction. Refresh clears previous values before requesting
new ones; generation checks discard late replies from a previous connection.
Limits are reference settings, not current live derating outputs.
