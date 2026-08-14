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

## Gateway Wi-Fi maintenance path

Wi-Fi configuration is an ESP32/Gateway maintenance function, not a battery
control path. The Gateway-hosted Companion can change credentials while it is
on the trusted LAN and can recover through an open, temporary setup AP. Neither
operation is sent to the STM32 or changes its safety ownership.

On first use the Gateway exposes `FlexBMS-Setup-XXYYZZ` at `192.168.4.1` until
configured. After a saved station network fails to acquire an IP for 30
seconds, it exposes the same AP for ten minutes while retrying station mode;
it then retries station-only for one minute and repeats until connected. The
AP offers best-effort captive-portal DNS/HTTP behaviour, although users may
need to open `http://192.168.4.1` manually on HTTPS-only clients.

The AP is deliberately open and credentials reside in ordinary ESP32 NVS.
While it is active, the Companion is monitor and Wi-Fi-configuration only:
Gateway-to-STM32 service controls are disabled. Detailed API and validation
requirements are in the Companion/Gateway specification.

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

## Companion: one UI, two web targets

The shared UI has a transport interface for connection lifecycle, live messages,
named requests, and declared capabilities. BMS views do not depend on the
physical transport.

~~~text
FlexBMS Companion shared BMS UI
  |- companion-webserial: Chromium Web Serial -> USB -> STM32
  |- companion-gateway:  Browser WebSocket -> ESP32 -> isolated UART -> STM32
  '- companion-desktop:  Portable Electron/Chromium wrapper -> Web Serial -> USB -> STM32
~~~

companion-webserial is the direct-service fallback. It exposes the same named
BMS actions as the Gateway build and has no raw-terminal or firmware-update
interface.

companion-desktop packages the direct-USB target as a portable, unsigned
Windows executable. It is not an installer and has no updater or native serial
protocol implementation; Electron provides Chromium and Web Serial only.

companion-gateway is the ESP32-hosted local maintenance page. It provides
monitoring, named service actions, Gateway status, and Wi-Fi configuration. It
does not include a raw terminal and the Gateway rejects unknown or raw requests
even if a browser client attempts to send them.

Both builds support stale-status explanation, live BMS monitoring, register
reads, Gateway NTP RTC sync and device-time readback, fault-clear requests,
the immediate BMS run-request switch, and browser-local CSV logging. The Gateway build additionally offers Gateway
Wi-Fi/link diagnostics. Its page prominently identifies the active connection
as either Direct USB to STM32 or Via ESP32 Gateway and disables capabilities
not available on that path.

The FlexBMS build removes HyDriven branding, unrelated vehicle-device views,
and Electron self-update UI. The initial stored Gateway bundle budget is 500
KiB. This is below the measured approximately 940 KiB stored size of the
unmodified multi-device Companion build. `scripts/build-release.ps1` runs the
Companion checks, packages the desktop executable, builds Gateway firmware,
and places `FlexBMS-Companion.exe`, `FlexBMS-Gateway.bin`,
`FlexBMS-Gateway-factory.bin`, `FlexBMS-STM32.bin`, `flash-release.ps1`, and
`SHA256SUMS.txt` in `release/FlexBMS-<version>/`. The interactive command
prompts for a semantic version and synchronizes a new one to the Companion
package files; `-Version` supports unattended builds. A repeat of an existing
version requires explicit confirmation and creates a timestamped sibling
directory rather than overwriting prior artifacts. It does not flash hardware.

`flash-release.ps1` is copied into every release directory. It verifies all
release checksums before acting, then uses STM32CubeProgrammer over ST-Link/SWD
to write and verify the STM32 application at `0x08000000`. For the ESP32-C3 it
uses `esptool.py` on a chosen COM port to erase and flash the complete Gateway
factory image. Flashing starts immediately after checksum verification; close
serial monitors and manually enter the ESP32-C3 bootloader when necessary.
The factory-image builder takes the Gateway application address from the
`ota_0` row in `Software/Gateway/partitions.csv` (currently `0x20000`), rather
than using ESP-IDF's conventional `0x10000` default. It verifies that the
merged factory image contains the ESP image magic at that exact address before
writing release checksums. Normal PlatformIO Gateway uploads use the same
partition-derived offset.

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
- configure or scan local Wi-Fi through the Gateway when its Wi-Fi capability
  is available.

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

The STM32 red and green status LEDs are active-low. The Gateway has one
active-high yellow `USR_LED` on IO1/GPIO1 (ESP32-C3-WROOM-02U module pin 17).
GPIO17 is not exposed by this module. The STM32 yellow LED is the USART1 TX
line and must not be used as an indicator. The STM32 red/green LEDs implement
the status language below; their active-low PWM handling is local to the
STM32 board-I/O layer.

The two controllers use the same *temporal* language and, while the UART is
healthy, the Gateway follows the STM32 phase. Each STM32 `STATUS` carries its
uptime every 500 ms or faster; the Gateway extrapolates that timestamp between
frames and uses it for the shared 2-second patterns. This keeps common pattern
edges aligned within normal UART and scheduling latency, while correcting clock
drift without a new wire format. On UART loss the Gateway falls back to its
local phase, so the two endpoints cannot remain synchronised during code 3.
Different controller states may deliberately use different patterns.

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
brightness; commissioning may tune each LED independently. The STM32
controller inverts its logical on/off pattern for its active-low hardware; the
Gateway drives its active-high LED directly. An optional separately capped
attention brightness may be used for boot acknowledgement and
fatal-local-failure indication, also below the hardware maximum.

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

## Framed BMS protocol

The canonical byte-level specification is
[`Documentation/protocol/uart-v1.md`](../protocol/uart-v1.md). It defines the
framing, CRC, numeric IDs, payload layouts, integer scales, fault/event
mappings, service result values, ROM-bootloader handoff, and test vectors. It
supersedes the earlier provisional UART description that appeared here.

The isolated UART link is 1 Mbit/s. Direct USB CDC uses the exact same framed
protocol for the standalone Web Serial Companion, while retaining the trusted
text command/debug console through a byte dispatcher. Telemetry is
unsolicited/latest-value data; one named STM32 service is active across both
links and has an explicit response. The STM32 sends raw integer values and
remains the safety authority. UART loss is a communication-health condition
only; it does not force the HV path off or change the run request.

## Firmware updates

`scripts/build-release.ps1` writes one `FlexBMS_bundle.fbu` OTA package. Its
`FBU1` header contains a compact JSON manifest followed by the STM32 and
Gateway raw images. The manifest identifies each target, semantic version, byte
length, and CRC-32. The station LAN is trusted: there are no release signatures
or login checks. CRC-32 detects corrupted storage and transfer; it is not an
authentication mechanism.

### ESP32 OTA

Only the station-LAN Gateway Companion page exposes firmware controls. The
operator selects `FlexBMS_bundle.fbu` and checks the STM32 BMS and/or ESP32
Gateway image shown by its manifest. Companion streams each selected raw image
to the target-specific Gateway endpoint; it completes STM32 first and the
rebooting Gateway last. Provisioning and recovery APs never expose update
controls. The Gateway checks the declared length and CRC-32 while streaming a
Gateway image into the inactive OTA slot, selects it for boot, acknowledges the
browser, and reboots. It marks itself healthy once normal Gateway startup
reaches the main loop. Manual USB factory flashing remains the recovery path;
no extra rollback policy is added.

The fitted ESP32-C3-WROOM-02U-N4 has 4 MiB flash. `Software/Gateway/partitions.csv`
defines the accepted layout. Its two 1.5 MiB OTA application slots accommodate
the Gateway image and its committed, compiled Companion bundle with room for
the 500 KiB bundle limit. The table is:

| Partition | Offset | Size | Purpose |
|---|---:|---:|---|
| `nvs` | `0x9000` | 24 KiB | Wi-Fi credentials and small update metadata. |
| `otadata` | `0xF000` | 8 KiB | Selects the booted OTA slot. |
| `phy_init` | `0x11000` | 4 KiB | Wi-Fi PHY initialisation data. |
| `ota_0` | `0x20000` | 1.5 MiB | One ESP32 firmware image and its compiled Companion bundle. |
| `ota_1` | `0x1A0000` | 1.5 MiB | The other ESP32 firmware image and its compiled Companion bundle. |
| `bms_update` | `0x320000` | 512 KiB | Raw staging area for one complete STM32 image. |
| `littlefs` | `0x3A0000` | 384 KiB | Reserved future diagnostics and file metadata. |

ESP32 OTA streams a verified image directly to the inactive application slot;
it does not need a separate download partition. STM32 flash is at most 512 KiB
and is staged in the dedicated raw partition. The Gateway verifies its expected
length and CRC-32 before beginning the internal UART transfer.

The Companion bundle is compiled into both OTA application images, which keeps
each Gateway firmware/UI release atomic. The 1.5 MiB slots support the
512,000-byte bundle limit without LittleFS serving. `FlexBMS-Gateway.bin` is
the OTA artifact; the complete factory image remains for wired recovery only.

The application image must be written at `ota_0`'s explicit offset, currently
`0x20000`. `0x10000` is only ESP-IDF's generic default and is not an
application partition in this layout. Release tooling reads the CSV rather
than duplicating this value, so a future partition-table change also changes
the factory-image application address.

### STM32 update

The Gateway stages `FlexBMS-STM32.bin` in `bms_update` and checks its length
and CRC-32 before asking the STM32 to enter update mode. The STM32 accepts only
from safe-off: run request off, HV outputs inactive, and normal services
inhibited. It replies before jumping to the immutable system-memory bootloader
on USART1 (PA9/PA10).

The ESP32 temporarily switches UART1 from framed 1 Mbit/s 8-N-1 to the STM32
ROM USART protocol, synchronises, verifies bootloader ID, erases application
flash, writes chunks, reads them back for CRC-32 verification, and issues `Go`
at the application address. It restores framed UART and reports completion only
after a new STM32 heartbeat. ESP32 reset/power loss during this rare service
operation is accepted as a manual ST-Link recovery case; BMS outputs remain
de-energised during the handoff.
