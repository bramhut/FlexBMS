#let brand = rgb("#155e75")
#let brand-dark = rgb("#164e63")
#let brand-light = rgb("#ecfeff")
#let ink = rgb("#17202a")
#let quiet = rgb("#5f6b76")
#let border = rgb("#d5dde2")
#let panel = rgb("#f6f8fa")
#let planned = rgb("#8a5a00")
#let planned-light = rgb("#fff8dc")

#set document(
  title: "FlexBMS System Overview",
  author: "FlexBMS project",
)

#set page(
  paper: "a4",
  margin: (top: 22mm, bottom: 20mm, x: 20mm),
  header: align(
    right,
    text(size: 8pt, fill: quiet, tracking: 0.4pt)[FLEXBMS / SYSTEM OVERVIEW],
  ),
  footer: context align(
    center,
    text(size: 8pt, fill: quiet)[Page #counter(page).display("1")],
  ),
)

#set text(font: "Arial", size: 9.5pt, fill: ink, lang: "en")
#set par(justify: true, leading: 0.66em)
#set heading(numbering: "1.", outlined: true)
#set list(indent: 15pt, body-indent: 5pt, spacing: 4pt)
#set table(inset: (x: 6pt, y: 5pt), stroke: border)
#show table.cell: it => {
  set par(justify: false)
  it
}
#show heading.where(level: 1): it => block(
  above: 18pt,
  below: 8pt,
  breakable: false,
)[
  #text(size: 16pt, weight: "bold", fill: brand-dark)[#it]
  #v(3pt)
  #line(length: 100%, stroke: 0.8pt + brand)
]
#show heading.where(level: 2): it => block(
  above: 13pt,
  below: 5pt,
  breakable: false,
)[#text(size: 12pt, weight: "bold", fill: brand-dark)[#it]]

#let status(value, kind: "current") = {
  let status-color = if kind == "planned" { planned } else { brand }
  let status-fill = if kind == "planned" { planned-light } else { brand-light }
  box(
    fill: status-fill,
    stroke: 0.5pt + status-color,
    radius: 3pt,
    inset: (x: 5pt, y: 2pt),
    text(size: 7.5pt, weight: "bold", fill: status-color, value),
  )
}

#let callout(title, body, kind: "info") = {
  let callout-color = if kind == "planned" { planned } else { brand }
  let callout-fill = if kind == "planned" { planned-light } else { brand-light }
  block(
    width: 100%,
    fill: callout-fill,
    stroke: (left: 3pt + callout-color),
    radius: 3pt,
    inset: 9pt,
    above: 7pt,
    below: 7pt,
  )[
    #text(weight: "bold", fill: callout-color)[#title]
    #v(2pt)
    #body
  ]
}

#let header-cell(body) = table.cell(
  fill: brand-dark,
  stroke: brand-dark,
  text(fill: white, weight: "bold", body),
)

#align(center)[
  #v(28mm)
  #text(size: 12pt, weight: "semibold", fill: brand, tracking: 1.2pt)[FLEXBMS]
  #v(6mm)
  #text(size: 29pt, weight: "bold", fill: brand-dark)[System Overview]
  #v(3mm)
  #text(size: 14pt, fill: quiet)[Home battery energy storage system]
  #v(16mm)
  #line(length: 70%, stroke: 1pt + brand)
  #v(17mm)

  #table(
    columns: (42mm, 72mm),
    align: (left, left),
    stroke: none,
    inset: (x: 5pt, y: 4pt),
    [*Document date*], [14 August 2026],
    [*System*], [96s LiFePO4 home BESS],
    [*Nominal capacity*], [314 Ah / approximately 96.5 kWh],
    [*Primary controller*], [STM32G491],
    [*Document status*], [Living architecture overview],
  )

  #v(24mm)
  #text(size: 9pt, fill: quiet)[
    This document explains the current FlexBMS system at architecture level.
    Schematics and source code remain authoritative for implementation details.
  ]
]

#pagebreak()

#outline(title: [Contents], depth: 2, indent: auto)

#pagebreak()

= System level

FlexBMS is the battery management and integration platform for a stationary home battery energy storage
system. The battery consists of 96 LiFePO4 cells in series, arranged as four 24-cell modules. At a nominal
cell voltage of 3.2 V and a capacity of 314 Ah, the pack is approximately 307 V and 96.5 kWh.

The system connects the battery to a three-phase hybrid inverter. FlexBMS measures the battery, protects
it, controls the high-voltage connection, and provides the information needed by higher-level energy
management. The battery management system remains the final authority for battery safety.

== System roles

#table(
  columns: (39mm, 1fr, 29mm),
  header-cell([Layer]),
  header-cell([Responsibility]),
  header-cell([Status]),
  [Battery and HV hardware],
  [Stores energy, measures physical quantities, and safely connects the pack to the inverter.],
  [#status("CURRENT")],
  [STM32 BMS],
  [Performs real-time measurement, protection, state handling, and contactor control.],
  [#status("CURRENT")],
  [ESP32 gateway],
  [Provides local BMS maintenance, Wi-Fi provisioning, and telemetry transport while remaining listen-only on CAN.],
  [#status("CURRENT")],
  [Energy management system],
  [Chooses when and how much to charge or discharge within BMS limits.],
  [#status("PLANNED", kind: "planned")],
  [Home Assistant],
  [Provides dashboards, automation, and a user-facing view of the installation.],
  [#status("CURRENT")],
)

== Energy and information flow

#align(center)[
  #block(
    width: 100%,
    fill: panel,
    stroke: border,
    radius: 4pt,
    inset: 12pt,
  )[
    #grid(
      columns: (1fr, 12mm, 1fr, 12mm, 1fr),
      align: center,
      [*Grid, PV and home*],
      [#text("<->")],
      [*Hybrid inverter*],
      [#text("<->")],
      [*FlexBMS battery*],
    )
    #v(8pt)
    #line(length: 100%, stroke: 0.5pt + border)
    #v(8pt)
    #grid(
      columns: (1fr, 10mm, 1fr, 10mm, 1fr),
      align: center,
      [Home Assistant],
      [#text("<->")],
      [EMS],
      [->],
      [Operating request],
    )
    #v(4pt)
    #align(center)[The BMS may reduce or reject any request to protect the battery.]
  ]
]

#callout(
  [Control principle],
  [
    Energy management is advisory; battery protection is mandatory. The EMS may request an operating
    point, but the BMS defines the permitted charge and discharge envelope and can open the contactor.
  ],
)

= Hardware

== Battery assembly

The pack contains 96 series-connected LiFePO4 cells divided into four physical 24s modules. Each module
contains two twelve-cell monitoring sections. A 200 A / 75 mV shunt is located at the negative end of the
pack for current measurement.

#block(breakable: false)[
  #table(
    columns: (45mm, 1fr),
    header-cell([Item]),
    header-cell([Current baseline]),
    [Cell chemistry], [LiFePO4],
    [Series configuration], [96s],
    [Physical modules], [4 x 24s],
    [Cell capacity], [314 Ah],
    [Nominal pack voltage], [307.2 V],
    [Nominal stored energy], [Approximately 96.5 kWh],
    [Current shunt], [200 A / 75 mV],
  )
]

The modules are installed in an unheated garage. Temperature measurement is distributed across the pack.
The current STM32 configuration expects four NTC inputs per battery-monitor IC. Final sensor population
and harness documentation should be kept consistent with the assembled modules.

== Module boards

Each 24s module has one FlexBMS module board with two MC33771C battery cell controllers. Each controller
measures twelve cells, supports passive balancing, reads local temperature sensors, and participates in
the transformer-isolated TPL daisy chain.

Across the complete pack this gives:

- eight MC33771C devices;
- 96 monitored cell voltages;
- up to 32 configured NTC measurements; and
- one current-sensing location on the first, lowest-voltage controller.

== Master controller

The master board is the central interface between the battery, high-voltage switching hardware, service
tools, inverter side, and local network.

Its main functional blocks are:

- STM32G491 real-time controller;
- MC33664 TPL transceiver for the module-board chain;
- ESP32-C3-WROOM-02U connectivity controller;
- isolated CAN and RS485 interfaces;
- isolated measurements of battery-side and load-side high voltage;
- contactor and precharge relay drivers;
- USB for flashing, diagnostics, and the Companion application; and
- power selection between USB and the battery-derived low-voltage supply.

The board contains separate HV, LV, and inverter-side isolated domains. USB-only power supports
development and diagnostics, but does not provide the 12 V rail required to energize the contactor.

== High-voltage path

The main contactor is located in the positive battery path. A separate precharge path limits inrush
current before the contactor closes. The STM32 is the sole software owner of both drivers. It measures
the battery and load sides through separate isolated AMC3330 channels, compares the battery-side result
with the voltage reported by the cell-monitor chain, and observes both active-low diagnostic outputs.

The contactor is pulled in at full drive and then held with 20 kHz PWM. Precharge remains closed during
pull-in and opens only after the contactor drive is established. The hold duty is a firmware
configuration value.

Voltage feedback shows whether the two HV nodes are plausible and whether precharge has progressed. It
does not prove the mechanical position of the contactor: the load-side voltage may remain present after
opening because the inverter DC-link discharge behavior is not yet characterized.

Pack-level protection also includes the main DC fuse and a manual DC isolation device. Exact component
ratings, wiring, creepage, clearance, and protection coordination belong in the hardware design files and
commissioning documentation.

== Physical communications

#block(breakable: false)[
  #table(
    columns: (33mm, 47mm, 1fr),
    header-cell([Interface]),
    header-cell([Connection]),
    header-cell([Purpose]),
    [TPL], [Master to 8 monitor ICs], [Cell, temperature, diagnostic, and current data.],
    [USB], [Service computer to STM32], [Firmware, logs, and Companion access.],
    [CAN], [STM32 and inverter-side domain], [Battery status and inverter communication.],
    [Wi-Fi], [ESP32 to local network], [Local maintenance page, Wi-Fi provisioning, and future telemetry integration.],
    [RS485], [ESP32 to inverter-side equipment], [Fallback or supplementary Modbus connection.],
  )
]

= Software

The software is divided by responsibility. Time-critical protection and actuation remain on the STM32.
Network-facing functions belong on the ESP32. Site optimization belongs in the EMS, with Home Assistant
providing the local user and automation layer.

== STM32

#status("CURRENT")

The STM32 firmware is the real-time BMS. It starts and monitors the MC33771C chain, gathers a complete
battery measurement set, evaluates battery and communication faults, and controls relay permission.

The current battery profile is:

- eight MC33771C devices with twelve cells each;
- four NTC channels per controller on GPIO0 through GPIO3;
- current sensing on CID 1 with a 375 micro-ohm shunt value;
- positive current for charging and negative current for discharging;
- 314 Ah nominal capacity; and
- amp-hour retention in RTC backup registers while backup power is present.

The firmware separates active fault conditions and acknowledgement-required latches. The HV supervisor
receives permission only while the BMS is running without blocking faults and holds one fresh, complete
measurement set from the monitor chain.

Runtime faults keep measurement and monitoring active while preventing relay closure. An acknowledgement
is accepted only after the underlying runtime condition has disappeared.

The STM32 also hosts:

- the dedicated HV supervisor;
- board-level analog and digital I/O;
- CAN communication;
- USB serial and the trusted text command/debug console; and
- the framed BMS v1 service interface over isolated UART and USB CDC.

=== Todo:
High priority:
- Go through CubeMX configuration. Pinout is OK.
- Analog measurement of HV side. Including diagnostic pin checking. ADC configuraion, high accuracy low Fs.
- Precharge handling. Including: 
- Contactor handling. Including: economizing, failure checking using voltages (to be discussed)
- USB VSENSE setup
- Derating on temperature (mainly low temperature charging, high temperature is not that applicable)
- Communication to Goodwe inverter. Exact protocol needs discussion and likely imperical data once we have the inverter ready.
- User LED status language is specified in
  `Documentation/architecture/home-bess-firmware-and-maintenance.md`; firmware
  implementation remains pending.

Medium priority:
- Aux relay control (potentially useful for a future heating system).


== HV supervisor

#status("CURRENT")

The HV supervisor follows `OFF -> SELF_TEST -> PRECHARGE -> CONTACTOR_CLOSE -> RUN`.
The transport-independent `setRunRequest(bool)` operation is implemented through
the framed BMS v1 named service. It provides intent only; it does not command a
contactor. The STM32 accepts operation only when its BMS and HV-supervisor conditions are healthy.

`SELF_TEST` checks BMS permission, measurement freshness, isolated-voltage diagnostics, agreement between
the battery-side and cell-monitor voltages, an unenergized load side, and the available power source.
`PRECHARGE` raises the load-side voltage and requires a stable voltage relationship before contactor
pull-in. `CONTACTOR_CLOSE` transfers from the precharge path to contactor hold drive. `RUN` keeps
monitoring the BMS permission and both HV measurements.

A removed request, lost BMS permission, or implausible HV feedback disables both drivers. A blocking fault
does not erase a true run request: after an explicit user acknowledgement and successful STM32
revalidation, the BMS may resume the startup sequence. The request defaults off after an STM32 reset;
loss of a Gateway or USB Companion transport does not alter it. The STM32 reports requested state,
actual HV state, and any blocking reason separately.

USB-only operation supports service access but cannot energize the HV path. A recorded run request
still requires healthy BCC and HV validation before the STM32 can start. The fitted hardware pull-down
defines the inactive USB-sense level; the STM32 does not enable an internal pull.

#callout(
  [Source of truth],
  [
    Thresholds, timings, fault definitions, CAN identifiers, and register-level behavior are intentionally
    not duplicated here. The STM32 source code and hardware schematics are authoritative.
  ],
)

== ESP32

#status("CURRENT")

The ESP32 is the network and telemetry controller in the isolated inverter-side domain. Its intended role
is to observe BMS and inverter information, publish selected data to the local network, and support
integration with the EMS and Home Assistant.

The ESP32 is not part of the battery safety chain. It is not allowed to transmit commands on the BMS CAN
bus and must remain listen-only there. Loss of the ESP32 or Wi-Fi must not prevent the STM32 from
protecting or isolating the battery.

`Software/Gateway` implements isolated UART v1, Wi-Fi
provisioning/station/recovery, and the local compiled Companion maintenance
page. The Companion provides stale-status diagnostics, fresh BMS monitoring,
CSV logging, register reads, Gateway NTP RTC synchronisation and device-time
readback, fault clear, immediate run-request actions, and station-LAN firmware
updates. MQTT Home Assistant discovery is a current Gateway function; CAN observation remains a separate planned increment. The Gateway uses isolated UART to the STM32 and
remains listen-only on shared BMS/GoodWe CAN. It is not a safety authority. The
detailed transport, local-network, and recovery design is in
`Documentation/architecture/home-bess-firmware-and-maintenance.md`.

After a station reconnect, the Gateway withdraws and re-announces
`flexbms.local`; a temporary mDNS failure is retried. Gateway OTA boots the
inactive slot as pending and confirms it only after the local HTTP/WebSocket
service starts and a station address returns. If that cannot happen within one
minute, the ESP-IDF bootloader returns to the previous slot. Companion compares
a content-derived web bundle identity and reloads its non-cacheable HTML shell
automatically when a new bundle responds.

The local web service is intentionally bounded: it retains TCP capacity for
MQTT and allows one queued telemetry frame per WebSocket. A browser that stops
reading is retired after its first failed asynchronous send; a delayed callback
from an old Companion socket cannot disturb its replacement connection.

The 4 MiB ESP32-C3 partition table uses two 1.5 MiB OTA application slots,
a 512 KiB `bms_update` staging partition, and 384 KiB LittleFS reserved for
future diagnostics/metadata. The committed Companion bundle is currently
compiled into each application image, not served from LittleFS.

The Gateway application is written at the explicit `ota_0` offset in
`Software/Gateway/partitions.csv` (currently `0x20000`), not ESP-IDF's generic
`0x10000` default. Gateway release and normal PlatformIO upload tooling derive
that address from the partition table; release creation verifies an ESP image
magic byte at the derived address in the merged factory image.

=== Framed BMS protocol v1 contract

#status("AGREED", kind: "planned")

The canonical byte-level implementation contract is
`Documentation/protocol/uart-v1.md`. The following overview is retained in the
system document; resolve any discrepancy in favour of the Markdown contract.

The STM32G491 BMS and ESP32-C3 Gateway communicate over isolated USART1 at 1 Mbit/s,
8-N-1, full duplex, without hardware flow control. Direct USB CDC uses the same framed bytes for
the Web Serial Companion while retaining the trusted text command/debug console through a byte
dispatcher. The STM32 is the safety authority. UART loss is a communication-health condition
only: it must not force the HV path off or change the current run request. The `*!` BMS Companion
protocol is retired.

Each endpoint sends a zero-payload heartbeat every 500 ms. A peer is declared lost only after
1.5 s without a complete CRC-valid frame. Invalid bytes or frames with a bad CRC do not refresh
the timer. The Gateway and direct USB Companion share one STM32 service slot; a second request
receives `BUSY` rather than being queued.

All multi-byte values are little-endian. Encoders and decoders write and read individual fields;
they must not transmit native C or C++ structs. The maximum payload length is 512 bytes.

==== Frame

```text
offset  size  field
0       2     MAGIC = 0x46 0x42 ("FB")
2       1     VERSION = 0x01
3       1     TYPE
4       1     SEQUENCE
5       2     LENGTH, uint16 payload length
7       n     PAYLOAD
7+n     4     CRC32, uint32
```

CRC is CRC-32/ISO-HDLC: polynomial `0x04C11DB7` (reflected `0xEDB88320`), reflected input
and output, initial value and final xor `0xFFFFFFFF`. It covers the magic, header, and payload,
but not the CRC field; the transmitted CRC word is little-endian. The standard check value is
`CRC32("123456789") = 0xCBF43926`. On an invalid version, length, or CRC, the receiver discards
the first magic byte and resumes magic scanning. The protocol uses no escaping or byte stuffing.

`SEQUENCE = 0` is reserved for heartbeat, telemetry, and events. A Gateway or USB Companion
service request uses a value from 1 through 255; the STM32 response echoes that sequence.

==== Types

#table(
  columns: (auto, auto, auto),
  align: (left, left, left),
  stroke: none,
  inset: (x: 5pt, y: 3pt),
  table.header([*ID*], [*Type*], [*Direction*]),
  [`0x01`], [HEARTBEAT], [both],
  [`0x02`], [STATUS], [STM32 to Gateway or USB Companion],
  [`0x03`], [PACK], [STM32 to Gateway or USB Companion],
  [`0x04`], [CELL], [STM32 to Gateway or USB Companion],
  [`0x05`], [TEMPERATURE], [STM32 to Gateway or USB Companion],
  [`0x06`], [HV_VOLTAGES], [STM32 to Gateway or USB Companion],
  [`0x10`], [SERVICE_REQUEST], [Gateway or USB Companion to STM32],
  [`0x11`], [SERVICE_RESPONSE], [STM32 to Gateway or USB Companion],
  [`0x12`], [EVENT], [STM32 to Gateway or USB Companion],
)

==== Telemetry payloads

`HEARTBEAT` has an empty payload.

`STATUS` is 33 bytes:

```text
bms_state:u8 | hv_state:u8 | flags:u16 | slave_count:u8 |
bms_active_errors:u32 | bms_latched_errors:u32 |
hv_active_errors:u32 | hv_latched_errors:u32 |
warnings:u32 | uptime_ms:u32 | soc_last_calibration_unix_s:u32
```

Status flag bits are: 0 BMS HV-ready, 1 charging allowed, 2 run request asserted, 3 complete
measurements fresh, 4 isolated-UART Gateway peer alive, and 5 balancing request. USB Companion
heartbeats do not affect bit 4. Bit 6 is SoC valid, bit 7 current sensing enabled, and bit 8
the SoC calibration UTC value valid; bits 9--15 are zero. `slave_count` comes from the configured
BMS monitor chain and is variable between builds. A production module contains eight monitor slaves,
but a single-slave development chain is supported during bench work. The Gateway must use the
reported value and must not reject an odd count.

`uptime_ms` is the STM32 HAL millisecond tick since its most recent reset. It is diagnostic
telemetry only, resets to zero after a controller restart, and wraps naturally after about 49.7
days. The STM32 sends STATUS at least every 500 ms while its UART service runs.

`PACK` is 24 bytes:

```text
pack_voltage_uV:u32 | pack_current_raw:i16 | soc_raw:u16 |
min_cell_uV:u32 | max_cell_uV:u32 |
min_ntc_raw:u16 | max_ntc_raw:u16 | min_ic_raw:u16 | max_ic_raw:u16
```

Voltage values are microvolts. Current is signed amperes times 64; positive current is charging.
SoC uses the existing BCC raw range (`0 = -100%`, `65535 = 200%`), so percent is
`100 * (soc_raw / 65535 * 3 - 1)`. The STM32 bounds its retained estimate and
reported SoC to 0--100%, so the wider transport representation is not used in
normal operation. NTC raw values convert as
`raw / 65535 * 120 - 20` degrees C. IC raw values are centikelvin, so degrees C are
`raw / 100 - 273.15`.

Use `pack_current_raw` only when STATUS bit 7 is set and `soc_raw` only when bit 6 is set. SoC is
informational and never commands HV. The estimate is retained in the CR2032-powered RTC backup
domain with a version marker and checksum; an erased or corrupt value is invalid. Current sensing is
deliberately disabled in the single-slave development configuration, so current and SoC are unavailable
there. Production enables it only on CID 1. Full-charge calibration requires fresh measurements with
no active BMS error, every cell at or above 3.450 V, current from -0.100 A through C/50 (6.28 A for the
314 Ah default), continuously for 300 s. Any failed condition restarts the timer; calibration sets SoC to
100%. Coulomb counting is bounded at 0% and 100%, allowing a subsequent discharge to lower SoC
normally without retaining an out-of-range value. When valid UTC is available, the STM32 persists that instant in integrity-protected backup
registers and reports it in `soc_last_calibration_unix_s`; it otherwise reports no calibration time.

`HV_VOLTAGES` is 12 bytes:

```text
valid_flags:u8 | reserved:u8[3] | bat_plus_uV:u32 | load_plus_uV:u32
```

Bit 0 of `valid_flags` means both values came from one fresh, coherent STM32 ADC/DMA scan with
a valid ADC reference. Bits 1--7 and the three reserved bytes are zero. The values are positive
microvolts from the isolated AMC3330 battery-side (BAT+) and load-side (LOAD+) channels. When the
valid bit is clear, consumers show the values as unavailable rather than treating the zero payload
as a measurement. These diagnostic readings are sent with every normal fresh snapshot and do not
change PCC thresholds, fault decisions, or STM32 safety ownership.

`CELL` is 51 bytes, once per configured slave:

```text
slave_index:u8 | balance_mask:u16 | cell_voltage_uV[12]:u32
```

`slave_index` is zero-based. `module_index = slave_index >> 1` and
`slave_in_module = slave_index & 1`. Balance-mask bit 0 represents cell 0 through bit 11 for
cell 11; bits 12--15 are zero. Each slave always has twelve cells. In a single-slave development
chain, only index 0 is sent; it maps to module 0, slave 0, and no partner frame is implied.

`TEMPERATURE` is 11 bytes, once per configured slave:

```text
slave_index:u8 | ntc_raw[4]:u16 | ic_temp_raw:u16
```

The four NTC values and one IC value are included together, making this packet fixed-size. This
matches the installed four-NTC-per-slave configuration and must be enforced as a compile-time
configuration check in both endpoint implementations.

The STM32 sends STATUS and a complete HV_VOLTAGES/PACK/CELL/TEMPERATURE snapshot when fresh measurement data
is available, no more often than once every 500 ms. It sends EVENT immediately on a change.
The Gateway must treat HV_VOLTAGES, PACK, CELL, and TEMPERATURE data as invalid while STATUS says measurements
are not fresh.

==== States, faults, and events

BMS states are `0 STARTING`, `1 READY`, `2 RUNNING`, `3 ERROR`, and `4 CRITICAL`.
HV states are `0 OFF`, `1 SELF_TEST`, `2 PRECHARGE`, `3 CONTACTOR_CLOSE`, and `4 RUN`.
`CRITICAL` has no bitmap: it represents an unrecoverable condition in the current software and
forces HV safe-off for the remainder of that boot.

BMS ERROR bitmap bits are: 0 `CONFIGURATION_INVALID`, 1 `SLAVE_UNAVAILABLE`,
2 `BCC_DIAGNOSTICS`, 3 `CELL_VOLTAGE_LIMIT`, 4 `THERMAL_LIMIT`, 5
`CURRENT_LIMIT`, 6 `BCC_INTEGRITY`, 7 `ADC_FAULT`, 8
`BALANCING_HARDWARE_FAULT`, and 9 `BCC_COMMUNICATION`. Bits 10--31 are
reserved and zero.

HV ERROR bitmap bits are: 0 `HV_SENSOR_DIAGNOSTIC`, 1
`BATTERY_VOLTAGE_MISMATCH`, 2 `LOAD_SIDE_ENERGISED`, 3 `PRECHARGE_TIMEOUT`, 4
`PRECHARGE_VOLTAGE_LOST`, and 5 `CONTACTOR_VOLTAGE_LOST`. Bits 6--31 are
reserved and zero.

`warnings` is non-latched. Bits are: 0 `WATCHDOG_RESET`, 1
`STARTUP_DIAGNOSTICS_BYPASSED`, and 2 `BATTERY_VOLTAGE_MISMATCH_OFF`.
Bits 3--31 are reserved and zero. `WATCHDOG_RESET` is a recorded warning that
an accepted acknowledgement clears. `STARTUP_DIAGNOSTICS_BYPASSED` remains
present while that build configuration is active; `BATTERY_VOLTAGE_MISMATCH_OFF`
remains present while the measurements disagree.
`WATCHDOG_RESET` is set when this STM32 boot followed an independent or window
watchdog reset. Gateway liveness, CAN condition, near-limit indication, and an
inactive balancing request are not BMS warnings in this release.

The STM32 compares BAT+ and the slave-reported pack voltage continuously. A disagreement is an
OFF-state warning while no run request is present; a run request promotes the same condition to
the blocking `BATTERY_VOLTAGE_MISMATCH` HV error. Releasing the request deasserts that active HV
error, retains its latch for acknowledgement, and restores the warning until the readings agree.

An active ERROR is present now. A latched ERROR remains blocking after its live condition clears
until the STM32 accepts acknowledgement. The STM32 Fault Manager is the sole owner of
active/latched aggregation, acknowledgement, and immediate HV safe-off. Detection modules may
only assert or deassert their assigned condition. Startup monitor-chain, register-initialisation,
and diagnostics attempts use their source retry budget before asserting their ERROR input. Once
exhausted, they use these ordinary ERROR semantics.

A live TPL/BCC communication loss immediately safe-offs HV and re-enters the same non-blocking
TPL, CID, register, and measurement initialisation path. A successful complete measurement and
fault-status cycle deasserts the active communication error; its latched error still requires
acknowledgement.

The independent watchdog starts after the required clock and DMA setup, before
every fallible peripheral initialisation, has a nominal 500 ms timeout, and is
refreshed every 100 ms only while both the PCC loop and BCC task continue to
make progress. BCC initialisation retry waits are non-blocking, so they do not
mask a stalled task. A subsequent platform-initialisation failure directly
disables the precharge output and contactor PWM before the watchdog reset; the
contactor driver pull-down is the hardware fallback. A clock-initialisation
failure may occur before the watchdog is available; the hardware pull-down
still holds the contactor control line safe.

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

#table(
  columns: (auto, 1fr, 1fr),
  align: (left, left, left),
  stroke: none,
  inset: (x: 5pt, y: 3pt),
  table.header([*ID*], [*Event*], [*Value*]),
  [`0x01`], [BMS state changed], [new BMS state],
  [`0x02`], [HV state changed], [new HV state],
  [`0x03`], [BMS active errors changed], [new active mask],
  [`0x04`], [BMS latched errors changed], [new latched mask],
  [`0x05`], [HV active errors changed], [new active mask],
  [`0x06`], [HV latched errors changed], [new latched mask],
  [`0x07`], [warnings changed], [new warning mask],
  [`0x08`], [measurement freshness changed], [`0` or `1`],
)

EVENT is a convenience notification; STATUS is authoritative, so loss of an event is harmless.

==== Services

The service-request payload is `service_id:u8 | arguments...`; the service-response payload is
`service_id:u8 | result:u8 | response_data...`. Result values are `0 OK`, `1 DENIED`,
`2 INVALID`, `3 BUSY`, and `4 USB_HOST_ACTIVE`. `INVALID` covers an unknown service, bad request length, bad argument,
or nonexistent slave index. `DENIED` means a valid request cannot safely be performed in the current
STM32 state. `BUSY` means another named service is still in progress. `USB_HOST_ACTIVE` means an
enumerated USB host prevents the STM32 ROM bootloader from accepting USART1. `OK` means that the STM32
accepted and invoked the requested operation; STATUS and EVENT show the resulting operating state.

#table(
  columns: (auto, auto, 1fr, 1fr),
  align: (left, left, left, left),
  stroke: none,
  inset: (x: 5pt, y: 3pt),
  table.header([*ID*], [*Service*], [*Request arguments*], [*OK response data*]),
  [`0x01`], [GET_STATUS], [none], [33-byte STATUS],
  [`0x02`], [SET_RUN_REQUEST], [`requested:u8` (`0` or `1`)], [none],
  [`0x03`], [ACKNOWLEDGE_FAULTS], [none], [none],
  [`0x04`], [READ_REGISTER], [`slave_index:u8, register:u8`], [`slave_index:u8, register:u8, value:u16`],
  [`0x05`], [SET_RTC], [`unix_time_s:u32` UTC], [none],
  [`0x06`], [GET_DEVICE_INFO], [none], [`firmware_version:u32`],
  [`0x07`], [PREPARE_STM32_BOOTLOADER], [`firmware_version:u32, image_length:u32, image_crc32:u32`], [none],
  [`0x08`], [GET_RTC], [none], [`unix_time_s:u32` UTC],
  [`0x09`], [SET_BALANCING_REQUEST], [`requested:u8` (`0` or `1`)], [none],
  [`0x0A`], [COMMIT_STM32_BOOTLOADER], [none], [none (successful request enters ROM immediately)],
)

`SET_RUN_REQUEST(0)` immediately removes the request through the STM32 PCC path. A request of 1
does not guarantee an HV start: the STM32 alone decides whether BMS and HV checks permit the
sequence. `ACKNOWLEDGE_FAULTS` is denied while any ERROR condition remains active or the BMS is
`CRITICAL`. It is also denied when no inactive BMS/HV ERROR latch or recorded `WATCHDOG_RESET`
warning remains to clear. Accepted acknowledgement clears those states only, leaving live and
configuration warnings visible until their underlying condition changes; it deliberately preserves
the run request. PCC can therefore restart from `OFF` immediately once the Fault Manager permits
it. Neither the Gateway nor Home Assistant can bypass a fault, write BCC registers, or override the
HV supervisor. `firmware_version` is packed as
`major | (minor << 8) | (patch << 16) | (build << 24)`.

`SET_BALANCING_REQUEST` is an additional AND gate: a request of 1 does not guarantee
balancing. The STM32 requires a running, fault-free BMS plus configured cell-voltage
thresholds. A request of 0 disables the BCC balancing drivers on the next BCC loop. This
development build defaults the volatile request to 0 after every reset; change and document
that default explicitly for the production balancing enablement.

`PREPARE_STM32_BOOTLOADER` and `COMMIT_STM32_BOOTLOADER` form the STM32-update handoff.
The Gateway must already have staged and CRC-checked the image. The STM32 validates the prepare
request shape and image length; `firmware_version` and `image_crc32` identify the Gateway-staged
image but cannot be verified by an STM32 that does not receive its bytes. It returns `DENIED` unless
the run request is off and it can de-energise the HV path. It returns `USB_HOST_ACTIVE` when its USB
CDC device is enumerated. Prepare immediately holds HV safely off and blocks normal services, then
returns `OK`. The prepared state expires after 3 seconds unless the Gateway sends commit. Commit has
no response: after successfully transmitting it, the Gateway owns USART1 and immediately changes to
ROM settings. If prepare or commit transmission fails, the STM32 remains in (or returns to) normal BMS
operation; the Gateway reports failure rather than waiting indefinitely. It then performs ROM bootloader
sync, transfer, readback verification, and `Go` directly on USART1; these are not FlexBMS UART
messages. On return, the Gateway waits for an application heartbeat before reporting completion.
Any failure after erase has begun requires wired STM32 recovery; the Gateway rejects
further STM32 OTA attempts until it observes an application heartbeat. There are deliberately no
update-data, retry, or status frame types.

==== Test vectors

The following complete frames are hexadecimal, including the little-endian CRC:

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

=== Pinout
The Gateway uses an ESP32-C3-WROOM-02U-N4. The module pin names and numbers
below follow the Espressif datasheet; net names follow the Master schematic.
GPIO17 is used internally for module flash and is not exposed.

#table(
  columns: (16mm, 27mm, 43mm, 64mm),
  align: (left, left, left, left),
  inset: 4pt,
  stroke: (x: 0.25pt, y: 0.25pt),
  table.header(
    header-cell([*Module pin*]),
    header-cell([*GPIO / module name*]),
    header-cell([*Net*]),
    header-cell([*Use*]),
  ),
  [1], [3V3], [`p3v3_INV`], [3.3 V supply],
  [2], [EN], [`EN`], [Chip enable],
  [3], [GPIO4 / IO4], [`CAN_TX`], [CAN controller transmit],
  [4], [GPIO5 / IO5], [`CAN_RX`], [CAN controller receive],
  [5], [GPIO6 / IO6], [`RS485_RO`], [RS-485 receiver output],
  [6], [GPIO7 / IO7], [`RS485_DI`], [RS-485 driver input],
  [7], [GPIO8 / IO8], [`IO8/STRAP`], [Strapping pin],
  [8], [GPIO9 / IO9], [`IO9/BOOT`], [Boot-mode strap],
  [9; EPAD 19], [GND], [`GND_INV`], [Ground and module ground pad],
  [10], [GPIO10 / IO10], [`ESP_VBUS_SENSE`], [USB VBUS sense],
  [11], [GPIO20 / RXD], [--], [Not connected; UART0 RX unused],
  [12], [GPIO21 / TXD], [--], [Not connected; UART0 TX unused],
  [13], [GPIO18 / IO18], [`USB_ESP32_N`], [USB D-],
  [14], [GPIO19 / IO19], [`USB_ESP32_P`], [USB D+],
  [15], [GPIO3 / IO3], [`UART_ESP_RX`], [STM32 USART1 TX input],
  [16], [GPIO2 / IO2], [`UART_ESP_TX`], [STM32 USART1 RX output; strapping pin],
  [17], [GPIO1 / IO1], [`USR_LED`], [Gateway user/status LED],
  [18], [GPIO0 / IO0], [`RS485_EN`], [RS-485 driver enable],
)


#block(breakable: false)[
  == Energy management system

  #status("PLANNED", kind: "planned")

  The EMS is the supervisory optimization layer. It should combine battery availability with site demand,
  PV production, inverter limits, dynamic electricity prices, EV charging, and other flexible loads.
]

The EMS may request charging or discharging, but it must operate inside limits supplied by the BMS and
inverter. It must tolerate missing telemetry and fall back to a safe, predictable operating policy.

The EMS implementation and deployment location are not yet defined in this repository. Its first
interface should be a small, explicit contract containing battery state, available charge/discharge
power, requested power, data freshness, and reason/status information.

== Home Assistant integration

#status("CURRENT")

Home Assistant is the intended user-facing integration point for the installation. It can provide:

- battery, inverter, PV, grid, and load dashboards;
- fault and availability notifications;
- manual operating-mode selection;
- automation around dynamic prices and flexible loads; and
- long-term energy and temperature history.

Home Assistant must not become a safety dependency. If Home Assistant is unavailable, the STM32 must
continue to protect the battery and the EMS must remain within the last valid limits or stop requesting
power.

The Gateway publishes one retained MQTT Device Discovery record and retained compact BMS state below a
MAC-derived `flexbms/flexbms_XXYYZZ` prefix. It exposes pack voltage/current/power, SoC when valid,
cell and temperature extrema, BMS/HV state, active faults, freshness, UART health, and the STM32-owned
run-request switch. Minimum and maximum cell voltage display in V with three decimals; cell delta displays
as whole mV. It also exposes separate diagnostic uptime values for the ESP32 Gateway and STM32 BMS
controller. `ON` and `OFF` requests share the existing single Gateway-to-STM32
service slot; the next STM32 status is authoritative and never promises contactor closure. Broker settings
(host, port, username, write-only password) are configured only through the trusted station-LAN Companion
page. The binary freshness, run-state, fault, and UART-health entities publish canonical MQTT `ON`/`OFF`
states. Fault acknowledgement, balancing, firmware upload, registers, raw diagnostics, and inverter/EMS
power control remain outside Home Assistant MQTT v1.

== Companion application

#status("CURRENT")

`Software/Companion` is the FlexBMS-owned BMS-only application with two web targets: a direct USB
Web Serial target for independent service diagnostics and an ESP32-hosted WebSocket target for local
maintenance. A portable, unsigned Windows executable packages the direct-USB target in a thin
Electron/Chromium wrapper. It has no installer, updater, or native serial-protocol implementation.

Both targets provide stale-status explanation, live fresh telemetry, CSV logging, named service requests,
device-time readback, and read-only register inspection. The Gateway obtains UTC
from NTP and shows its latest successful RTC sync in the browser locale; direct USB
shows that NTP status is Gateway-only. The Gateway target also shows Wi-Fi and UART state and, only in connected station
mode, single-bundle firmware updates. The direct-USB target exposes neither
a raw terminal nor firmware-update UI; Gateway requests use an explicit allowlist.

`scripts/build-release.ps1` interactively confirms the release version, updates the Companion package
version when a new one is entered, and builds the Companion, Gateway, and STM32 artifacts with checksums
and one #raw("FlexBMS_bundle.fbu") OTA package containing both targets, versions, byte lengths, and CRC-32 values.
`-Version` supports unattended use. Each release includes `flash-release.ps1`: ST-Link/SWD flashes the
STM32 image, while a selected ESP32-C3 COM port flashes a complete Gateway factory image. Both actions
start immediately after checksum verification. Repeating an existing version requires an explicit choice
and creates a timestamped output without overwriting the prior release.

= Operating concept

== Start-up

At start-up, the STM32 validates its configuration and initializes the complete TPL chain. It assigns
each battery monitor a chain address, configures its registers, runs diagnostics, and obtains a fresh
measurement set. Both HV outputs remain off. After BMS validation, the separate HV supervisor can run
its self-test and precharge sequence only when a new run request is supplied.

== Normal operation

During normal operation the STM32 repeatedly measures the pack and evaluates its safety state. Battery
limits and status are made available to the surrounding system. The inverter converts energy between the
DC battery and the AC installation, while the future EMS decides the desired operating point.

== Faulted operation

A battery, measurement, or communication fault immediately removes relay permission. Monitoring
continues where possible so that the live condition can be observed. Faults remain latched until they are
acknowledged through the supported service path and the BMS has successfully revalidated the battery
chain.

The BMS exposes the HV supervisor as one aggregate blocking fault while the supervisor retains separate
active, latched, and historical HV reasons. The Companion clear command can clear an HV latch only when
the live HV conditions are healthy. The BMS then repeats chain initialization, diagnostics, and a complete
fault-checked measurement cycle before the latch is released. If the retained run request is true, the BMS
may then resume its normal startup sequence. Historical reasons remain available until reboot.

Configuration and initial TPL setup faults require a reboot. The ESP32, EMS, and Home Assistant cannot
override a blocking STM32 fault.

= Interfaces and ownership

#block(breakable: false)[
  #table(
    columns: (38mm, 45mm, 1fr),
    header-cell([Information or action]),
    header-cell([Owner]),
    header-cell([Consumer]),
    [Cell voltages and temperatures], [STM32 BMS], [Diagnostics, inverter limits, telemetry],
    [Battery current and SoC], [STM32 BMS], [Inverter, EMS, Home Assistant],
    [Battery safety limits], [STM32 BMS], [Inverter and EMS],
    [Contactor and precharge], [STM32 BMS], [Physical HV path],
    [Inverter operating state], [Inverter], [ESP32 / EMS / Home Assistant],
    [Requested charge/discharge power], [EMS], [Inverter-facing control interface],
    [Dashboards and automation], [Home Assistant], [User and household services],
  )
]

The protocol between these layers should carry explicit validity and freshness information. A stale value
must never be interpreted as a current permission to charge, discharge, or close the contactor.

= Current project structure

#table(
  columns: (48mm, 1fr),
  header-cell([Repository area]),
  header-cell([Contents]),
  [`Hardware/Master`], [Master schematic, PCB, harnesses, and production data.],
  [`Hardware/ModuleBoard`], [Battery module-board schematic, PCB, and production data.],
  [`Software/Master`], [STM32G491 firmware and platform configuration.],
  [`Software/Companion`], [Current FlexBMS BMS maintenance UI with direct USB, Gateway, and portable Windows package outputs.],
  [`Software/Gateway`], [ESP32 UART v1, Wi-Fi provisioning/recovery, compiled Companion serving, Home Assistant MQTT discovery, and OTA; CAN observation remains planned.],
  [`Documentation/protocol/uart-v1.md`], [Canonical framed BMS protocol for Gateway UART and direct USB CDC, including test vectors.],
  [`Simulations`], [Electrical simulation files used during hardware development.],
  [`Documentation`], [This system overview and its Typst build setup.],
)

The repository contains the initial ESP32 Gateway and FlexBMS Companion
implementations. EMS and Home Assistant remain planned.

= Open topics

This overview is intentionally incomplete. The next useful documentation additions are:

- confirmed as-built module, NTC, and high-voltage topology;
- final inverter selection, protocol compatibility, and commissioning sequence;
- system-level operating, fault, recovery, and shutdown policy;
- commissioning of the ESP32 telemetry, local-network, and update architecture; and
- EMS and Home Assistant interfaces, fallback behavior, and user permissions.
