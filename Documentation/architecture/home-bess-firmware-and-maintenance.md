# Home-BESS firmware and maintenance architecture

## Status and scope

This is the agreed target architecture for FlexBMS Home-BESS software. It is a
design document, not a statement that the described ESP32, update, or web
software exists yet. It does not change the hardware safety chain.

The STM32G491 BMS communicates with the GoodWe GW12K-ET-20 over CAN. The
ESP32-C3 connects to the STM32 through the existing isolated UART link. The
ESP32 may passively observe the shared BMS/GoodWe CAN bus for diagnostics, but
never transmits on it.

## Safety and ownership

| Component | Owns | Does not own |
|---|---|---|
| STM32 BMS | Measurements, fault evaluation, HV permission, contactor safety, GoodWe CAN, final validation of all service requests | Wi-Fi, MQTT, Home Assistant availability |
| ESP32 Gateway | Local network, MQTT, web service, UART transport, ESP32 OTA, STM32 update orchestration | Battery safety decisions and BMS/GoodWe CAN transmission |
| FlexBMS Companion | Independent maintenance UI and named diagnostics/service requests | Raw network commands or safety decisions |
| Home Assistant | Dashboards, notifications, firmware/status display, maintenance-page link | Firmware upload, contactor control, or any safety dependency |

Loss of the ESP32, Wi-Fi, MQTT, Home Assistant, or the browser must never
prevent the STM32 from protecting or isolating the battery.

## Operating request

The STM32 owns one volatile `run_request` flag. Both Companion transports and
the later Home Assistant switch use the same named `setRunRequest(true|false)`
operation; it is intent, never a direct contactor command.

The STM32 may operate the HV path only when `run_request` is true and all BMS
and HV-supervisor conditions are healthy. It is the STM32, not the requesting
client, that advances `OFF -> SELF_TEST -> PRECHARGE -> CONTACTOR_CLOSE -> RUN`.

A blocking fault immediately removes effective HV permission but does not erase
`run_request`. After the user explicitly clears faults and the STM32 completes
its required revalidation, the BMS may resume the startup sequence when the
request is still true. Switching the request off never clears faults.

The flag defaults off only after an STM32 reset. The Gateway serialises
state-changing requests so there is one active writer while any number of
clients may monitor. Telemetry must report the requested state, actual HV
state, and a concise blocking/rejection reason separately.

## Repository layout and release ownership

FlexBMS owns the BMS-only Companion source because it releases alongside the
STM32 and Gateway interfaces. The external HyDriven Companion remains useful
as historical/reference code, but is not a release dependency or submodule.

~~~text
Software/
  Master/                 STM32 BMS firmware
  Gateway/                ESP32 firmware, MQTT, HTTP/WebSocket, OTA, UART bridge
  Companion/              FlexBMS-only Vue maintenance application
    src/                  shared BMS UI and transports
    dist/                 versioned generated bundles consumed by Gateway builds
  protocol/               transport specification, message catalogue, test vectors
Documentation/
  architecture/          this architecture
  operations/            commissioning, update, and recovery procedures
~~~

The obsolete Software/bms_companion directory is legacy code and is not the
source for future Home-BESS Companion work. A Gateway release consumes a
versioned committed Companion bundle so a local Gateway build remains offline
and reproducible.

## Companion: one UI, two builds

The shared UI has a transport interface for connection lifecycle, live messages,
named requests, and declared capabilities. BMS views do not depend on the
physical transport.

~~~text
FlexBMS Companion shared BMS UI
  |- companion-webserial: Chromium Web Serial -> USB -> STM32
  '- companion-gateway:  Browser WebSocket -> ESP32 -> isolated UART -> STM32
~~~

companion-webserial is the direct-service and development fallback. It keeps
the raw terminal so developers can see printf output and issue manual
development commands over physical USB. It has no firmware-update interface.

companion-gateway is the ESP32-hosted local maintenance page. It provides
monitoring, named service actions, Gateway status, and the update workflow. It
does not include a raw terminal and the Gateway rejects unknown or raw requests
even if a browser client attempts to send them.

Both builds support live BMS monitoring, register reads, RTC sync, fault-clear
requests, the BMS run request, and browser-local CSV logging. The Gateway build additionally offers
Gateway Wi-Fi/link diagnostics, a bounded downloadable Gateway diagnostic log,
and the staged update workflow. Its page prominently identifies the active
connection as either Direct USB to STM32 or Via ESP32 Gateway and disables
capabilities not available on that path.

The FlexBMS build removes HyDriven branding, unrelated vehicle-device views,
and Electron self-update UI. The initial stored Gateway bundle budget is 500
KiB. This is below the measured approximately 940 KiB stored size of the
unmodified multi-device Companion build.

## Gateway web and network service

The ESP32 serves the Gateway Companion bundle and its API on the trusted local
LAN. It uses unauthenticated local HTTP and WebSocket by design; the router
must not expose the device to the Internet. A local page is preferred to an
externally hosted UI because it remains usable without Internet, Home
Assistant, or a separate server.

When unprovisioned, the Gateway provides a temporary setup access point and
setup page. After credentials are stored it uses station mode on the local LAN.

Named Gateway service actions in the first release are:

- read a BMS register;
- synchronise the RTC;
- set the BMS run request;
- submit a fault-clear request; and
- view/download bounded diagnostic logs.

Charger/configuration writes are out of scope. A single deliberate button press
is sufficient for a permitted local action; the STM32 remains responsible for
validating every requested state change. Wi-Fi, browser, MQTT, and Home
Assistant loss have no BMS safety effect in this stage.

## MQTT and Home Assistant

The Gateway uses MQTT Discovery so Home Assistant needs no YAML configuration.
The initial MQTT device and state prefix is `flexbms/home_bess`; Home Assistant
Discovery configuration uses its standard discovery prefix. The intended broker
is the official Mosquitto Broker add-on on Home Assistant OS. The ESP32 uses a
normal broker login, reconnects when the broker returns, and republishes current
state after reconnect. Home Assistant, the broker, and MQTT are not BMS safety
dependencies.

Discovery/configuration and the most recent state values are retained. Gateway
availability uses MQTT last-will/offline status. An unavailable Gateway marks
the Home Assistant entities unavailable but does not alter the STM32 request or
safety state.

Home Assistant receives only compact aggregate telemetry and the maintenance
link; detailed cells, balancing state, registers, raw diagnostics, and the 128
KiB Gateway log remain in Companion. The initial entity catalogue is:

| Entity | Meaning |
|---|---|
| `switch.run_request` | Requested BMS operation only; never a contactor-state guarantee. |
| `sensor.hv_state` and `binary_sensor.hv_active` | Actual STM32 HV-supervisor state. |
| Pack sensors | Voltage, current, SoC, minimum/maximum cell voltage, and minimum/maximum temperature. |
| `binary_sensor.fault_active` | On whenever any active or blocking BMS fault exists. |
| `sensor.active_faults` | Human-readable active fault names, or `No faults`. |
| Link/version sensors | STM32 and Gateway firmware versions, UART health, telemetry freshness, and Gateway availability. |
| Update status sensor | Gateway/STM32 update phase and result, when the corresponding update function exists. |
| Maintenance-page link | Local Gateway Companion page for diagnostics and log download. |

The `run_request` switch reports the STM32-owned retained intent. Separate HV
and fault entities show whether that intent is presently effective. Home
Assistant does not upload images or directly initiate safety-related actions.

## Status LED language

All physical status LEDs are active-low. The Gateway has one yellow LED on
IO1/GPIO17. The STM32 has usable red and green user-LED outputs, independently
PWM controlled. The STM32 yellow LED is the USART1 TX line and must not be used
as an indicator. The current STM32 firmware only performs a continuous colour
sweep and the system overview previously listed its user LED as a TODO; that
sweep is a hardware check, not a status language.

The two controllers will use the same *temporal* language. Exact flash phase
does not need synchronisation: in particular the endpoints cannot synchronise
after the UART has failed. They do use the same visible cadence and the same
meaning for the UART-loss code.

| Pattern | Timing | Common meaning |
|---|---|---|
| Boot acknowledgement | Solid for 1 s | Firmware has started. |
| Waiting | 500 ms on / 500 ms off | Startup or a required prerequisite is not yet ready. |
| Healthy heartbeat | 100 ms on every 2 s | Ready and healthy, but not actively operating. |
| Code 2 | Two 150 ms flashes, 150 ms apart, every 2 s | A non-safety external service is unavailable. |
| Code 3 | Three 150 ms flashes, 150 ms apart, every 2 s | STM32--Gateway UART link unavailable. |
| Update | 4 Hz blink | Firmware update preparation or transfer is in progress. |
| Fatal local failure | Solid after the boot acknowledgement | The local firmware cannot enter normal operation. |

Colour adds STM32-specific context without changing these patterns. The STM32
uses green for healthy and transition/update states, and red for an active
blocking BMS condition. The Gateway uses its single yellow LED for all Gateway
states. In particular, a UART-loss BMS communication fault is three red
flashes; the Gateway shows the same three-flash code in yellow. Any other
active blocking BMS fault is a single red flash every two seconds. The full
fault bitmap is intentionally not encoded into flash counts;
Companion and Home Assistant remain the source of the exact fault names.

The initial state mapping is:

| Controller | State | Indication |
|---|---|---|
| STM32 | Boot / self-test before ready | Green waiting blink after the boot acknowledgement. |
| STM32 | Healthy `OFF`, no run request | Green healthy heartbeat. |
| STM32 | `SELF_TEST`, `PRECHARGE`, or `CONTACTOR_CLOSE` | Green waiting blink. |
| STM32 | `RUN` | Solid green. |
| STM32 | Active blocking fault other than UART loss | Single red flash every 2 s. |
| STM32 | UART-loss fault | Code 3 in red. |
| Gateway | Boot | Yellow boot acknowledgement. |
| Gateway | Wi-Fi setup or connecting to Wi-Fi | Yellow waiting blink. |
| Gateway | Wi-Fi connected, MQTT unavailable | Code 2 in yellow. |
| Gateway | Wi-Fi, MQTT, and STM32 UART healthy | Yellow healthy heartbeat. |
| Gateway | UART heartbeat timeout | Code 3 in yellow. |
| Gateway | Firmware update in progress | Yellow update blink. |

The STM32 LED controller and Gateway LED controller must be non-blocking,
driven from their regular scheduling loops, and apply the highest-priority
active state. For the STM32: fatal local failure, blocking fault, update,
startup transition, `RUN`, then ready-off. For the Gateway: fatal local
failure, update, UART loss, Wi-Fi setup/connecting, MQTT loss, then healthy.
An ordinary BMS fault is not a Gateway fault indication unless it is the
UART-loss condition: the Gateway remains healthy and reports the BMS fault
through Companion and Home Assistant.

Brightness is independently adjustable for every physical LED: STM32 red,
STM32 green, and Gateway yellow. These values are named firmware configuration
constants rather than hard-coded in pattern logic. They define the maximum
duty/drive used by every normal operational pattern and default below full
brightness; commissioning may tune each LED independently. Each controller
inverts its logical on/off pattern to drive its active-low hardware, without
changing its configured brightness. An optional separately capped attention
brightness may be used for boot acknowledgement and fatal-local-failure
indication, also below the hardware maximum.

## Telemetry and event contract

UART telemetry uses raw integer units; the Gateway converts the compact Home
Assistant values to engineering units (V, A, degrees Celsius, and percent) with
sensible display rounding. The initial cadence is:

| Data | STM32 to Gateway | Gateway to MQTT / Home Assistant |
|---|---:|---:|
| BMS/HV state, fault state, and `run_request` | On change and 1 Hz | On change |
| Pack voltage/current/SoC and min/max values | 2 Hz | 1 Hz |
| Per-slave cell voltages and balancing | Every fresh BMS set | Companion only |
| Per-slave temperatures | Every fresh BMS set | Companion only |
| UART health and telemetry freshness | 1 Hz | 1 Hz |
| Firmware versions | On boot/change | Retained |
| Fault, UART, and HV transition events | Immediately | Immediately |

The Gateway publishes individual BMS event messages to MQTT in addition to
maintaining the current `fault_active` and `active_faults` entities. This
supports Home Assistant history and automations without making events a BMS
safety dependency. Events use STM32 RTC Unix time after it has been
synchronised. Before then, they carry uptime and an explicit unknown-time state.

## STM32--ESP32 UART protocol

The isolated UART target is 1 Mbit/s. Two Mbit/s is an allowed later option
only after link validation. Protocol v1 uses a binary, length-framed packet:

```text
MAGIC(2) | VERSION(1) | TYPE(1) | SEQUENCE(1) | LENGTH(2) | PAYLOAD(0..512) | CRC-32(4)
```

All multi-byte fields are little-endian. `MAGIC` is a fixed two-byte
resynchronisation marker. A receiver scans for it after corrupt data and accepts
a candidate only when its length is within the 512-byte payload limit and its
CRC-32 validates the header and payload. `SEQUENCE` is zero for unsolicited
telemetry and nonzero for a service request and its response.

The STM32 supplies raw integer measurements, not floating-point engineering
values: for example millivolts, milliamps, deci-degrees Celsius, and a defined
integer SoC scale. The Gateway, Companion, and Home Assistant convert for
display. This is compact and deterministic and preserves the BMS source data.

The initial type catalogue is:

| Type | Direction | Purpose |
|---|---|---|
| `HEARTBEAT` | Both | Uptime and link liveness. Sent every 500 ms. |
| `STATUS` | STM32 to Gateway | Firmware version, BMS state, `run_request`, actual HV state, fault bitmap, and UART health. |
| `PACK_TELEMETRY` | STM32 to Gateway | Pack voltage/current, SoC, and min/max cell voltage and temperature. |
| `CELL_TELEMETRY` | STM32 to Gateway | Module index, slave index, twelve cell voltages, and balancing bitmap. Two slave frames describe one 24-cell module. |
| `TEMPERATURE_TELEMETRY` | STM32 to Gateway | Module index, slave index, and that slave's sensor temperatures. |
| `SERVICE_REQUEST` | Gateway to STM32 | Named service identifier and its binary payload. |
| `SERVICE_RESPONSE` | STM32 to Gateway | Request sequence, result, reason, and optional data. |
| `EVENT` | STM32 to Gateway | Fault, UART, or HV state transition with timestamp. |
| `UPDATE_*` | Both | Reserved for Stage 2 STM32 update transfer. |

Telemetry frames are unsolicited and unacknowledged; old telemetry may be
dropped under backpressure. A service request has one request in flight and an
explicit response. Its response envelope contains the request sequence, one
result (`OK`, `REJECTED`, `BUSY`, `INVALID`, or `FAILED`), a concise reason code,
and optional response data. The initial named services are:

| Service | Payload / result |
|---|---|
| `GET_STATUS` | Current BMS status and firmware version. |
| `SET_RUN_REQUEST` | One Boolean request; accepted/rejected with effective/block reason. |
| `CLEAR_FAULTS` | Accepted/rejected and revalidation progress. |
| `READ_REGISTER` | CID and register address; returns value or error. |
| `SET_RTC` | Unix seconds; applied or rejected. |

Examples of service reason codes are `BLOCKING_FAULT`, `UART_NOT_READY`,
`HV_NOT_SAFE`, and `UNSUPPORTED`. There is no generic remote-command service.

The traffic categories are:

- **telemetry:** STM32 to Gateway, latest-value semantics; old data may be
  dropped under backpressure;
- **service:** the named request/response operations above, acknowledgements,
  result codes, and reasons for rejection; and
- **update:** ordered acknowledged image chunks, retries, and transfer CRCs.

The Gateway is required for normal BMS operation. Each endpoint expects a
heartbeat every 500 ms; after three missed intervals (1.5 s) UART communication
is lost. This includes an ESP32 reboot or unavailable ESP32 and raises a
manually-cleared BMS communication error that disables the effective HV state.
It does not clear a true `run_request`; after explicit fault clear and STM32
revalidation, normal startup may resume. No other network availability loss is
currently a warning or error.

Existing line-oriented, `*!`-prefixed Companion messages remain supported only
on the STM32 USB service path. The Gateway uses the clean binary messages above;
it does not encapsulate legacy Companion payloads. The exact field layout,
integer scales, service payloads, and test vectors belong in `Software/protocol`
before implementation.

## Firmware updates

Images have a small manifest with target, hardware revision, semantic
version/build identifier, byte length, and CRC-32. The trusted-LAN design does
not require release signatures. CRC-32 detects corrupted storage and transfer;
it is not an authentication mechanism.

### ESP32 OTA

The Gateway writes a verified ESP32 image to its inactive OTA application slot,
sets it pending, reboots, and reports the booted or fallback version. It retains
network provisioning/configuration separately from the application slots.
An image is accepted as healthy once it reaches the Gateway main loop; automatic
functional self-test and rollback policy are deliberately out of scope because
manual ESP32 reflashing is an accepted recovery path.

The fitted ESP32-C3-WROOM-02U-N4 has 4 MiB flash. The initial custom partition
target is approximately two 1.25 MiB application slots and 1.44 MiB LittleFS,
plus bootloader, partition, NVS, and PHY overhead. This is provisional until
the actual Gateway image is measured.

LittleFS holds the Gateway Companion bundle, diagnostics, update metadata, and
one staged STM32 image. STM32 flash is at most 512 KiB and likely materially
smaller, so a BMS-only 500 KiB web-bundle budget leaves useful filesystem
headroom. No partition table is final until real Gateway and Companion sizes
have been measured.

### STM32 update: two stages

**Stage 1** implements Gateway telemetry/MQTT, the Gateway Companion page,
ESP32 OTA, and the direct Web-Serial fallback. STM32 updates remain manual by
USB/ST-Link. Gateway firmware uses ESP-IDF. The Gateway retains a 128 KiB
circular diagnostic log for download through the maintenance page.

**Stage 2** adds remote STM32 updating. The Gateway stages the image, validates
target/size/CRC, and asks the STM32 to enter update mode. The STM32 accepts only
from a confirmed safe-off state: HV outputs inactive, no update in progress,
and external run/service actions inhibited. It then makes the same safe state
explicit before jumping from its application into the immutable STM32 system
memory bootloader on USART1 (PA9/PA10).

The ESP32 transfers image chunks using the STM32 ROM bootloader and verifies
the programmed image. It then issues the ROM bootloader Go command to start
the new application; ESP32 hardware reset control is intentionally absent. The
application must fully reinitialise peripherals after this jump.

There is no current STM32 firmware-update feature. The system-memory jump,
safe entry state machine, transfer implementation, and post-update health
report all require implementation and bench validation. If the STM32
application cannot run or update fails, manual ST-Link recovery is the accepted
fallback. A lost ESP32/Wi-Fi link during the process must leave BMS outputs
de-energised.

## Implementation gates

Before enabling Stage 2 in an installation:

1. Verify the STM32 software jump to the ROM bootloader on the actual board.
2. Verify ROM USART transfer, image verification, and Go into the application.
3. Demonstrate safe output state during normal transfer, interrupted transfer,
   failed CRC, Gateway reset, and Wi-Fi loss.
4. Measure actual ESP32 application, BMS-only web bundle, and STM32 image
   sizes; then lock the partition table.
5. Preserve direct USB/Web-Serial diagnostics and manual ST-Link recovery as
   independently tested service paths.
