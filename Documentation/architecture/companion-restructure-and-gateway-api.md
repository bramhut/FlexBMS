# FlexBMS Companion restructure and Gateway API specification

## Status and purpose

This specification defines the first FlexBMS-only Companion release. It is the
implementation contract for moving the maintained BMS Companion source into
this repository, creating the Gateway maintenance-page build, and preserving
direct USB/Web Serial service access.

The canonical STM32--Gateway wire contract is
[`Documentation/protocol/uart-v1.md`](../protocol/uart-v1.md). This document
does not redefine UART framing or BMS safety behaviour. It defines the
Companion source layout, browser-facing Gateway API, USB Companion additions,
and release/validation requirements.

## Ownership and scope

The STM32 remains the sole owner of measurements, fault evaluation, HV
permission, contactor control, and final validation of every request. A
successful `set_run_request(true)` response acknowledges only the STM32-owned
intent; it never promises that the HV path will close.

The Gateway owns local HTTP/WebSocket transport, conversion between the browser
API and UART v1, and serialisation of named service requests. The browser,
Wi-Fi, MQTT, and Gateway do not form part of the safety chain. Browser loss or
Gateway loss must not alter the BMS safety decision.

The first release includes:

- live BMS/HV monitoring, including cells, balancing, and temperatures;
- browser-local CSV logging;
- named BMS services: run request, fault clear, device-time readback, and
  read-only register reads;
- Gateway Wi-Fi configuration, scan, and UART diagnostics; and
- a direct USB/Web Serial service fallback using the same named BMS actions.

It excludes charger/configuration writes, raw network commands, a raw terminal,
Electron updater or installer, firmware update UI, Gateway-assisted STM32
updating, and all non-BMS vehicle views. A thin, portable Electron/Chromium
wrapper for the direct USB target is included; it has no native serial-protocol
implementation. STM32 firmware updates remain manual by USB/ST-Link in this release.

## Gateway Wi-Fi configuration and recovery

Wi-Fi is Gateway-local infrastructure. It never crosses UART and cannot alter
STM32 measurement, fault, HV, or contactor ownership. Wi-Fi credentials are
stored in ordinary ESP32 NVS; physical access to the Gateway is therefore
trusted. Passwords are write-only: they are accepted by the Gateway but never
included in browser state, WebSocket messages, diagnostics, or logs.

The Gateway hosts the same committed Companion bundle and WebSocket endpoint
in every available Wi-Fi state. There is no separate provisioning page or HTTP
server. The Gateway is unauthenticated on the trusted local LAN and must not
be Internet-exposed.

With no saved credentials, it starts the open `FlexBMS-Setup-XXYYZZ` AP until
credentials are submitted. The suffix is derived from the Wi-Fi MAC. With saved
credentials it starts in station mode. If it has no station IP for 30 seconds,
it enters recovery AP+STA mode for ten minutes while continuing station
retries. If still offline, the AP stops for a one-minute station-only retry
window, then the recovery cycle repeats. A station IP immediately stops the
setup AP and restores normal station mode.

The setup/recovery AP uses `192.168.4.1`. Its DNS responder maps A lookups to
that address and unknown HTTP paths redirect to the Companion root so common
phone captive-portal checks can open setup automatically. This is best effort:
HTTPS-only checks cannot be redirected, so `http://192.168.4.1` remains the
documented manual address. The AP is intentionally open and temporary. While
it is active, Companion monitoring and Wi-Fi configuration remain available,
but every Gateway-to-STM32 service capability is false.

The Companion offers on-demand nearby-network scanning, rate limited to one
scan per ten seconds, plus manual SSID entry for hidden networks. Saving a
replacement SSID/password validates 1--32 SSID bytes and 0--63 password bytes,
commits NVS, acknowledges acceptance, then restarts networking. It does not
test a candidate network before commit or retain multiple networks; an invalid
replacement follows the normal recovery-AP path.

## Repository migration and layout

The source import is the maintained
`C:\Users\Bram\Documents\Git\companion\webapp` BMS application, not the
obsolete `Software/bms_companion` tree. Import only the BMS-relevant Vue
application, its required generic UI primitives, and the package/tooling files.
Remove the FCCU, ECU, PDB, multi-device selector, HyDriven branding, Electron
main process, Electron updater, downloads view, emulator coupling, and their
dependencies.

The import must preserve the FlexBMS changes already present in the maintained
source:

- `bms/lib/messageParsers.ts` names BMS fault bit 15 as
  `HV_SUPERVISOR_FAULT`; and
- `bms/components/PrechargeCard.vue` displays the FlexBMS HV supervisor states
  and errors.

After the imported application passes the validation in this document, remove
the obsolete `Software/bms_companion` directory in the same change. The
separate HyDriven repository remains historical reference only.

The resulting layout is:

~~~text
Software/Companion/
  src/
    app/                  application shell, router, connection header
    bms/                  store, telemetry conversion, dashboard and service views
    transports/           transport interface plus Web Serial and Gateway adapters
    shared/               CSV logging and reusable BMS-neutral UI components
  public/                 FlexBMS-only static assets
  dist/
    gateway/              committed Gateway build and manifest
  package.json
  package-lock.json
  vite.config.ts
  README.md
~~~

`package.json` holds the semantic Companion release version. No version is
derived from the Git branch or a timestamp.

## Build targets and release bundle

One source tree produces two web targets selected at build time, not at runtime:

| Target | Transport | Included capabilities | Excluded capabilities |
|---|---|---|---|
| `webserial` | Chromium Web Serial to STM32 USB | Monitoring, CSV, register reads, device-time readback, fault clear, run request | Gateway NTP status, diagnostics/logs, raw terminal, all update UI |
| `gateway` | WebSocket to the hosting ESP32 Gateway | Monitoring, CSV, register reads, NTP/device-time status, fault clear, run request, and Gateway Wi-Fi state/configuration | Web Serial, raw terminal, diagnostic-log download until that Gateway service exists, and all update UI |

The Gateway build is the only committed generated build. It is written to
`dist/gateway` with relative asset URLs. The Gateway PlatformIO pre-build hook
runs `npm run build:gateway` before compiling firmware, so the generated bundle
and firmware image are always coupled. The Web Serial build is generated from
source for development/release validation and is not committed. `npm run
build:desktop` builds the Web Serial target and packages it as a portable
Windows executable. This wrapper is unsigned, does not install itself, and
uses Chromium's Web Serial implementation rather than a native serial module.

`dist/gateway/manifest.json` is generated by the Gateway build and has this
shape:

```json
{
  "format": 1,
  "companion_version": "MAJOR.MINOR.PATCH",
  "files": [
    { "path": "index.html", "bytes": 0, "sha256": "lowercase hex" }
  ],
  "total_bytes": 0
}
```

`files` lists every file in `dist/gateway` except `manifest.json`, using paths
relative to `dist/gateway`, lexical path order, exact stored byte counts, and
SHA-256 of each stored file. `total_bytes` is their sum. A build fails if the
sum exceeds 512,000 bytes (500 KiB). The Gateway records the manifest version
and serves only the files listed by it; it does not serve arbitrary filesystem
paths.

The current Gateway release compiles the committed files into `GatewayAssets.cpp`,
so a Companion bundle is atomic with its ESP32 application image. It uses no
branding images or bundled fonts. LittleFS remains reserved for a future
separate asset-serving increment and is not mounted by this release.

The release scripts are `npm run build:webserial`, `npm run build:gateway`,
`npm run build:desktop`, and `npm run check`. `build:gateway` regenerates the
committed bundle and its manifest. `check` runs the TypeScript/Vue type check
and unit tests without rewriting source files. `scripts/build-release.ps1`
runs the checks, packages the direct-USB executable, builds Gateway and STM32
firmware, and writes all artifacts with `SHA256SUMS.txt` below
`release/FlexBMS-<version>/`. Its interactive form prompts for a semantic
version and synchronizes a new one to the Companion package files; `-Version`
supports unattended builds. A repeat of an existing version requires explicit
confirmation and creates a timestamped sibling directory instead of
overwriting existing artifacts. It never flashes a device.

Each release includes a checksum-verifying `flash-release.ps1`. Its `Stm32`
target requires STM32CubeProgrammer and an ST-Link/SWD connection; its `Gateway`
target requires `esptool.py` and a selected ESP32-C3 USB serial COM port. The
Gateway target writes the complete factory image, including bootloader and
partition table. The factory-image builder reads the application address from
the `ota_0` row in `Software/Gateway/partitions.csv` (currently `0x20000`),
not ESP-IDF's generic `0x10000` default, and verifies an ESP image magic byte
at that address before releasing the artifact. Normal PlatformIO Gateway
uploads use that same partition-derived address. Both targets start after
release-checksum verification.

## Shared application model

### Transport interface

The BMS store and views depend on a single TypeScript transport interface. It
provides connection lifecycle, a complete current snapshot, incremental events,
the declared capabilities, and a `request(service, arguments)` method. Views
must not access `navigator.serial`, `WebSocket`, or framed BMS bytes directly.

The common service names are:

| Service | Arguments | Result data |
|---|---|---|
| `set_run_request` | `requested: boolean` | none |
| `clear_faults` | none | none |
| `get_rtc` | none | `unix_time_s` UTC integer |
| `get_device_info` | none | `firmware_version_packed` little-endian `major.minor.patch.build` bytes |
| `read_register` | `slave_index: number`, `register: number` | `slave_index`, `register`, `value` |

Actions show `Accepted`, `Denied`, `Invalid`, `Busy`, or `Transport error`.
`Accepted` means the STM32 has invoked the request; the following snapshot and
events remain authoritative for its effect. The UI does not invent a BMS
rejection reason when UART v1 supplies only `DENIED`.

The shared status is delivered independently from a complete snapshot, so the
UI can explain a startup fault or stale-measurement state without displaying
invented zero values. The shared snapshot stores the source units from UART v1: microvolts, current
raw amperes times 64, BCC SoC raw, NTC raw, and IC raw. The BMS presentation
layer alone converts them to V, A, percent, and degrees Celsius according to
`uart-v1.md`. A snapshot with `measurements_fresh: false` is displayed as stale
for every measurement value; it is not displayed as live zero data. Gateway
transport also treats a non-healthy UART state as stale immediately, while
retaining the last snapshot only for context.

### Dashboard and service views

The application has a persistent connection header, a BMS dashboard, an
optional Gateway Wi-Fi page, and a Firmware page.

The header reads exactly **Direct USB to STM32** for the Web Serial target and
**Via ESP32 Gateway** for the Gateway target. It shows transport connection
state and telemetry freshness. Gateway mode additionally shows Wi-Fi and UART
link state.

The compact dashboard follows the maintained BMS Companion's card/table
workflow without importing its vehicle, Electron, image, font, or raw-terminal
features. It shows BMS/HV state, requested run state, pack voltage/current/
SoC, active and latched BMS faults, active and latched HV reasons, minimum and
maximum cell/temperature values, all slave cell voltages/balancing, and all
slave temperatures. It includes browser-local CSV logging. The CSV column
names use converted engineering values and retain the reported zero-based slave
index; the header is created once after the first complete fresh snapshot.

Named action controls and the read-only register viewer are embedded in the
dashboard. The run request is an immediate accessible switch: changing it sends
one explicit `set_run_request` request and a denied/failed request restores the
last reported STM32 state. The Gateway obtains NTP time only after station
connection and forwards UTC to the STM32; Companion exposes no manual setter.
The service section reads the STM32 RTC with `get_rtc`, shows the advancing
device time in the browser's locale, and, in Gateway mode, reports the latest
successful NTP-to-STM32 sync or its waiting state. Direct USB states that NTP
status is Gateway-only. The
compact register field descriptions are informational only; the read remains
read-only.

The Firmware page displays the ESP32 Gateway version when connected through the
Gateway and reads the STM32 version through `get_device_info`; direct Web Serial
therefore still shows the STM32 version. Only the Gateway build renders firmware
update controls, and only while connected in station mode without an active
setup/recovery AP. It accepts the single `FlexBMS_bundle.fbu` release package,
shows checkboxes for its STM32 and Gateway members, streams selected members to
the target-specific HTTP endpoints, and reports the Gateway update state as a
step flow with byte progress while uploading, programming, and verifying an
STM32 image.
When both are selected it completes STM32 first and updates the rebooting
Gateway last. Wired release flashing remains the recovery path.

## Gateway browser API

The Gateway serves the committed `dist/gateway` bundle at `/` after normal LAN
startup. It exposes a single persistent WebSocket endpoint at `/ws`. Both are
unauthenticated on the trusted local LAN only; the device must not be exposed
to the Internet.

WebSocket payloads are UTF-8 JSON objects. Every object has `v: 1` and a
string `type`. Unknown types, extra service fields, malformed JSON, binary
frames, and payloads over 4096 bytes are rejected and do not reach UART.

### Firmware upload endpoints

`POST /api/firmware/gateway` and `POST /api/firmware/stm32` are station-mode
only endpoints. Their body is the selected raw binary, with
`X-FlexBMS-Target`, `X-FlexBMS-Version`, `X-FlexBMS-Length`, and
`X-FlexBMS-CRC32` headers copied from the selected bundle member manifest. The
Gateway rejects an active setup/recovery AP, concurrent update, malformed
headers, content-length mismatch, target mismatch, oversize image, write
failure, or final CRC mismatch. A successful response is
`{"result":"accepted"}`; the update phase, stage, byte progress, and detail
are then reported in `gateway_status.firmware_update`. The factory image is
never accepted here.

The first server message has this exact shape; `gateway_status` has the fields
defined below.

```json
{
  "v": 1,
  "type": "hello",
  "gateway_version": "MAJOR.MINOR.PATCH",
  "capabilities": {
    "monitor": true,
    "csv_logging": true,
    "set_run_request": true,
    "clear_faults": true,
    "get_rtc": true,
    "get_device_info": true,
    "read_register": true,
    "wifi_configuration": true,
    "diagnostic_log_download": false,
    "raw_terminal": false,
    "firmware_update": false
  },
  "gateway_status": {}
}
```

The Gateway sends these server-to-browser messages:

| Type | Required fields | Meaning |
|---|---|---|
| `hello` | `gateway_version`, `capabilities`, `gateway_status` | First message after connection and whenever capabilities change. |
| `bms_status` | `status` | Current UART v1 `STATUS`, sent even while measurements are stale. |
| `snapshot` | `status`, `pack`, `cells`, `temperatures` | Complete current source-unit BMS view. `status` includes `measurements_fresh`. |
| `event` | `event_id`, `value`, `gateway_uptime_ms` | Direct representation of a UART v1 event; informational only. |
| `gateway_status` | `wifi_state`, optional `wifi_ssid`, `setup_ap`, `uart_state`, `mqtt_state`, `time_sync`, `diagnostic_log` | Local Gateway state, setup AP details, NTP state, and diagnostic-log availability. |
| `service_result` | `request_id`, `service`, `result`, optional `data` | Result for exactly one browser service request. |
| `wifi_configuration_result` | `request_id`, `result` | Accepted or failed local credential persistence/restart request. |
| `wifi_scan_result` | `request_id`, `result`, optional `networks` | Completion of an on-demand nearby-network scan. |

`gateway_status.time_sync` is `{ "state": "waiting_for_network | waiting_for_ntp |
waiting_for_stm32 | synchronized", "last_sync_unix_s": 0 }`. The last-sync
field is omitted until an NTP sample has been accepted by the STM32 and is not
persisted across a Gateway reboot. SNTP uses `0.nl.pool.ntp.org` through
`3.nl.pool.ntp.org`, starts only after station connection, and polls hourly.

`snapshot` has this exact shape. All numeric measurement fields preserve the
source raw units and map directly to the identically named UART v1 fields.

```json
{
  "v": 1,
  "type": "snapshot",
  "status": {
    "bms_state": 0,
    "hv_state": 0,
    "flags": 0,
    "slave_count": 0,
    "bms_active_faults": 0,
    "bms_latched_faults": 0,
    "hv_active_faults": 0,
    "hv_latched_faults": 0,
    "uptime_ms": 0,
    "measurements_fresh": false,
    "run_request": false
  },
  "pack": {
    "pack_voltage_uV": 0,
    "pack_current_raw": 0,
    "soc_raw": 0,
    "min_cell_uV": 0,
    "max_cell_uV": 0,
    "min_ntc_raw": 0,
    "max_ntc_raw": 0,
    "min_ic_raw": 0,
    "max_ic_raw": 0
  },
  "cells": [
    { "slave_index": 0, "balance_mask": 0, "cell_voltage_uV": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] }
  ],
  "temperatures": [
    { "slave_index": 0, "ntc_raw": [0, 0, 0, 0], "ic_temp_raw": 0 }
  ]
}
```

`bms_status.status` has exactly the same fields as `snapshot.status`. It is
sent after `hello` when a status is already known and after every valid UART
`STATUS` frame. This lets the browser display the current BMS state and fault
bits while waiting for a complete fresh measurement set.

`cells` contains exactly twelve `cell_voltage_uV` values for each reported
slave. `temperatures` contains exactly four `ntc_raw` values for each reported
slave. `measurements_fresh` and `run_request` duplicate UART `STATUS` flag
bits 3 and 2 respectively for direct UI gating; `flags` remains present as the
source bitmask. A `snapshot` is sent immediately after `hello` only when a
complete fresh view is already available, and after every complete fresh UART
snapshot. UART `STATUS` remains authoritative over missed `event` messages.

`gateway_status` uses these closed values:

- `wifi_state`: `provisioning`, `connecting`, `connected`, `recovery`, or
  `unavailable`;
- `wifi_ssid`: optional configured/connected station SSID, never a password;
- `setup_ap`: `{ "active": boolean }` and, when active, its `ssid` and
  `address` (`192.168.4.1`);
- `uart_state`: `healthy`, `lost`, or `starting`;
- `mqtt_state`: `unavailable`, `connecting`, `connected`, or `lost`; and
- `diagnostic_log`: an object containing `available: boolean` and `bytes`.

Diagnostic-log download is reserved but not implemented in the current
Gateway release: `diagnostic_log.available` and
`capabilities.diagnostic_log_download` are both false, and
`GET /api/diagnostic-log` returns `404`. A future bounded-log implementation
must keep that fixed endpoint, use a `text/plain` attachment, accept no query
parameters or paths, and enable the UI only when both fields are true.

The Gateway accepts the following narrow browser-to-server messages. No raw
network command is accepted.

```json
{
  "v": 1,
  "type": "service",
  "request_id": "opaque client string up to 64 ASCII characters",
  "service": "set_run_request | clear_faults | get_rtc | get_device_info | read_register",
  "arguments": {}
}
```

`arguments` must exactly match the common service table. Numbers are JSON
integers: `slave_index` and `register` are 0--255. `set_run_request` requires exactly
`{ "requested": true | false }`; services with no arguments require `{}`.
When the open setup/recovery AP is active, valid BMS service requests receive
`denied` locally and do not reach UART.

Wi-Fi configuration is not a BMS service and never reaches UART:

```json
{
  "v": 1,
  "type": "wifi_configure",
  "request_id": "opaque client string up to 64 ASCII characters",
  "ssid": "network name",
  "password": "write-only password"
}
```

It requires exactly the fields shown. A valid request receives
`wifi_configuration_result` with `result: "accepted"` before the Gateway
restarts Wi-Fi; local persistence/scheduling failure returns `"error"`.

```json
{ "v": 1, "type": "wifi_scan", "request_id": "opaque client string up to 64 ASCII characters" }
```

The scan request requires exactly those fields. Its eventual
`wifi_scan_result` is `ok`, `busy`, `rate_limited`, or `unavailable`; `ok`
contains `networks`, each with `ssid`, `rssi`, and `secure`.

The result object has this exact shape. `data` is omitted for every service
except a successful `read_register`, `get_rtc`, or `get_device_info`.

```json
{
  "v": 1,
  "type": "service_result",
  "request_id": "same opaque client string",
  "service": "read_register",
  "result": "ok | denied | invalid | busy | transport_error",
  "data": { "slave_index": 0, "register": 0, "value": 0 }
}
```

The Gateway permits one UART service request in flight for all clients. It
returns `busy` rather than queueing a second request. It maps UART v1 results
`OK`, `DENIED`, `INVALID`, and `BUSY` to JSON `ok`, `denied`, `invalid`, and
`busy`; it uses
`transport_error` for unavailable UART, timeout, malformed response, or local
Gateway failure. `service_result.data` is present only for a successful
`read_register`, `get_rtc`, or `get_device_info` and has the fields in the
common service table.

The `capabilities` object in `hello` has Boolean keys
`monitor`, `csv_logging`, `set_run_request`, `clear_faults`, `get_rtc`, `get_device_info`,
`read_register`, `diagnostic_log_download`, `raw_terminal`, and
`firmware_update`, plus `wifi_configuration`. In normal connected station mode
the BMS-service keys, `wifi_configuration`, and `firmware_update` are true. In
provisioning or recovery AP mode, `wifi_configuration` stays true but every
BMS-service key and `firmware_update` are false. `raw_terminal` remains false;
`diagnostic_log_download` is false until the bounded Gateway log exists.

The Gateway build reconnects automatically after abnormal WebSocket closure,
starting at one second and doubling to a maximum of ten seconds. It clears
connection state while disconnected, sends no browser-originated recovery
commands, and waits for a new `hello` followed by a `snapshot` before treating
telemetry as current.

## Direct USB/Web Serial protocol parity

The Web Serial target uses the exact `FB` v1 frame protocol from
`uart-v1.md`, not a line-oriented USB variant. It sends an empty heartbeat every
500 ms, validates CRC and fragmented frames, and correlates named service
responses by v1 sequence. The STM32 publishes the same status, complete
snapshot, event, and service payloads to the active direct-USB link as it does
to Gateway UART.

USB CDC still carries the trusted engineering text command/debug console. The
STM32 byte dispatcher consumes complete framed Companion traffic before it
reaches that console; the Web Serial decoder ignores text and resynchronises on
valid frames. The UI exposes no raw terminal in either build. One STM32 service
is active globally across USB and Gateway UART; the other transport receives
v1 `BUSY` and does not queue work. The retired `*!` BMS messages are not a
supported fallback.

## Validation and acceptance

The implementation is accepted only when all of the following are true:

1. The BMS-only import retains the two identified FlexBMS fixes; no HyDriven,
   vehicle-device, Electron updater, or native serial dependency remains in
   `Software/Companion`. The portable Electron wrapper is limited to the
   direct-USB Web Serial target.
2. `npm ci`, `npm run check`, `npm run build:webserial`, `npm run build:gateway`,
and `npm run build:desktop` succeed from `Software/Companion`.
3. `dist/gateway/manifest.json` matches every committed Gateway asset, reports
   at most 512,000 bytes, and is regenerated deterministically by a clean
   Gateway build.
4. Unit tests cover raw-unit conversion including centikelvin IC temperature,
   stale-status rendering, NTP-sync status and advancing
   device-time display, transport capability gating, service-result rendering,
   and Gateway reconnect timing.
5. Gateway API parsing rejects malformed/extra-field Wi-Fi messages and invalid
   SSID/password lengths; it accepts only exact configuration and scan shapes.
6. Gateway validation covers no-credential provisioning, 30-second station
   failure, ten-minute AP+STA recovery, one-minute station-only retry, station
   success, scan rate limiting, credential-change restart, captive DNS/HTTP
   routing, and AP-mode BMS-service denial without UART transmission.
7. Target-hardware testing confirms actual ESP32-C3 AP+STA reconnect behaviour
   and captive-portal prompts on representative Android, iOS, Windows, and
   macOS clients. HTTPS-only portal probes may require the documented manual
   address.
8. Gateway tests decode the UART v1 test vectors, validate the `bms_status`
   message shape, and prove malformed or raw-frame browser traffic cannot
   reach UART.
9. STM32 tests cover USB `0x1E` run-request handling and `0x1F` results for
   every named service, including denied and invalid cases.
10. A manual browser check confirms stale-status explanation, live telemetry,
    CSV creation, register read, NTP RTC sync/status, fault clear, and run request.
    Hardware operation and contactor behaviour are not claimed validated by
    browser/unit tests alone.
